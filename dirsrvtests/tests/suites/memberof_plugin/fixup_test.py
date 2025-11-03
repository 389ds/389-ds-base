# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import logging
import pytest
import os
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccounts, nsUserAccounts
from lib389.idm.group import Groups
from lib389 import DEFAULT_SUFFIX
from lib389.topologies import topology_st


pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

group_name = 'group'
member_name = 'member'


@pytest.fixture(scope="function")
def memberof(topology_st, request):
    memberof = MemberOfPlugin(topology_st.standalone)
    memberof.enable()
    topology_st.standalone.restart()

    def fin():
        memberof.disable()
        topology_st.standalone.restart()

    request.addfinalizer(fin)

    return memberof

@pytest.fixture(scope="function")
def prepare_entries(topology_st, request):
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    users = nsUserAccounts(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('Create group cn=group,ou=groups,{}'.format(DEFAULT_SUFFIX))
    if groups.exists(group_name):
        test_group = groups.get(group_name)
        test_group.delete()

    properties = {
        'cn': group_name,
        'description': group_name
    }
    test_group = groups.create(properties=properties)

    log.info('Create user cn=member,ou=People,{}'.format(DEFAULT_SUFFIX))
    if users.exists(member_name):
        member = users.get(member_name)
        member.delete()

    properties_member = {
        'uid': member_name,
        'cn': member_name,
        'displayName': member_name,
        'uidNumber': '1000',
        'gidNumber': '1000',
        'homeDirectory': '/home/{}'.format(member_name)
    }
    member = users.create(properties=properties_member)

    def fin():
        log.info('Delete test group')
        if test_group.exists():
            test_group.delete()
        log.info('Delete member user')
        if member.exists():
            member.delete()

    request.addfinalizer(fin)


def test_fixup_task_repair_entry_erroneously_set_as_member(topology_st, prepare_entries):
    """ Test memberOf fixup task

    :id: 7e5ef63a-36c1-4cb9-9f0a-f92e4d1937ca
    :setup: Standalone instance
    :steps:
        1. Create group entry: cn=group,ou=groups,dc=example,dc=com
        2. Create member user entry: uid=member,ou=people,dc=example,dc=com
        3. Update member user entry to have objectclass nsMemberOf
        4. Update member user entry to contain attribute memberOf of the group entry
        5. Enable MemberOf Plugin
        6. Verify the member user entry still has memberOf attribute set
        7. Verify the group entry does not have member attribute set
        8. Run the fixupmemberof task
        9. Verify the member user no longer contains memberOf attribute
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
    """

    memberof = MemberOfPlugin(topology_st.standalone)

    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    group = groups.get(group_name)

    users = nsUserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    member = users.get(member_name)

    log.info('Add nsMemberOf objectclass to member entry')
    member.add('objectclass', 'nsMemberOf')

    log.info('Add memberOf attribute to member entry')
    member.add('memberOf', group.dn)
    assert member.present('memberof', group.dn)
    assert not group.present('member', member.dn)

    log.info('Enable memberOf plugin and restart')
    memberof.enable()
    topology_st.standalone.restart()

    log.info('Verify the memberOf attribute is still present')
    assert member.present('memberof', group.dn)
    assert not group.present('member', member.dn)

    log.info('Run the fixup task')
    task = memberof.fixup(basedn=DEFAULT_SUFFIX)

    # Wait for task to complete before reading entries
    task.wait()

    log.info('Verify the memberOf attribute is no longer present')
    try:
        assert not member.present('memberof', group.dn)
        assert not group.present('member', member.dn)
    finally:
        log.info('Disable memberOf plugin and restart')
        memberof.disable()
        topology_st.standalone.restart()


def test_fixup_task_limit(topology_st):
    """Test only one fixup task is allowed at one time

    :id: 2bb49a10-fca9-4d89-9a7a-34c2ba4baadc
    :setup: Standalone Instance
    :steps:
        1. Add some users and groups
        2. Enable memberOf Plugin
        3. Add fixup task
        4. Add second task
        5. Add a third task after first task completes
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Second task should fail
        5. Success
    """

    # Create group with members
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={'cn': 'test'})

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    # Turn on access log buffering to speed up user creation
    buffering = topology_st.standalone.config.get_attr_val_utf8('nsslapd-accesslog-logbuffering')
    topology_st.standalone.config.set('nsslapd-accesslog-logbuffering', 'on')
    for idx in range(6000):
        user = users.create(properties={
            'uid': 'testuser%s' % idx,
            'cn' : 'testuser%s' % idx,
            'sn' : 'user%s' % idx,
            'uidNumber' : '%s' % (1000 + idx),
            'gidNumber' : '%s' % (1000 + idx),
            'homeDirectory' : '/home/testuser%s' % idx
        })
        group.add('member', user.dn)

    # Restore access log buffering
    topology_st.standalone.config.set('nsslapd-accesslog-logbuffering', buffering)

    # Configure memberOf plugin
    memberof = MemberOfPlugin(topology_st.standalone)
    memberof.enable()
    topology_st.standalone.restart()

    # Add first task
    task = memberof.fixup(DEFAULT_SUFFIX)

    # Add second task which should fail
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        memberof.fixup(DEFAULT_SUFFIX)

    # Add second task but on different suffix which should be allowed
    memberof.fixup("ou=people," + DEFAULT_SUFFIX)

    # Wait for first task to complete
    task.wait()

    # Add new task which should be allowed now
    memberof.fixup(DEFAULT_SUFFIX)


def test_fixup_task_invalid_basedn(topology_st, memberof):
    """Test memberOf fixup task with invalid basedn

    :id: dce9b898-119d-42b8-a236-1130e59bfe18
    :setup: Standalone Instance with memberOf plugin enabled
    :steps:
        1. Clear error logs to ensure clean testing
        2. Run fixup task with invalid/non-existent basedn
        3. Verify task completes but logs appropriate error
        4. Check error log for "Failed to get be backend" message
    :expectedresults:
        1. Error logs should be cleared successfully
        2. Task should be created successfully
        3. Task should complete
        4. Error should be logged about invalid backend
    """

    # Clear error logs to ensure clean testing
    log.info('Clearing error logs before test')
    topology_st.standalone.deleteErrorLogs()

    # Try to run fixup task with invalid basedn
    invalid_basedn = 'dc=testdb,dc=com'
    log.info(f'Running fixup task with invalid basedn: {invalid_basedn}')

    # The task should be created successfully even with invalid basedn
    task = memberof.fixup(basedn=invalid_basedn)

    # Wait for task to complete
    task.wait()

    # Check that appropriate error was logged
    log.info('Checking error log for backend error message')
    assert topology_st.standalone.ds_error_log.match('.*Failed to get be backend.*')


def test_fixup_task_invalid_filter(topology_st, memberof):
    """Test memberOf fixup task with invalid search filter

    :id: dde9e893-119d-42c8-a236-1190e56bfe98
    :setup: Standalone Instance with memberOf plugin enabled
    :steps:
        1. Clear error logs to ensure clean testing
        2. Run fixup task with invalid/malformed search filter
        3. Verify task completes but logs appropriate error
        4. Check error log for "Bad search filter" message
    :expectedresults:
        1. Error logs should be cleared successfully
        2. Task should be created successfully
        3. Task should complete
        4. Error should be logged about invalid filter
    """

    # Clear error logs to ensure clean testing
    log.info('Clearing error logs before test')
    topology_st.standalone.deleteErrorLogs()

    # Try to run fixup task with invalid filter (missing closing parenthesis)
    invalid_filter = '(objectClass=person'
    log.info(f'Running fixup task with invalid filter: {invalid_filter}')

    # The task should be created successfully even with invalid filter
    task = memberof.fixup(basedn=DEFAULT_SUFFIX, _filter=invalid_filter)

    # Wait for task to complete
    task.wait()

    # Check that appropriate error was logged
    log.info('Checking error log for filter error message')
    assert topology_st.standalone.ds_error_log.match('.*Bad search filter.*')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

