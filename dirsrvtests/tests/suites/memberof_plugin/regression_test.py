# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import time
import ldap
import subprocess
from random import sample
from lib389.utils import ds_is_older, ensure_list_bytes, ensure_bytes
from lib389.topologies import topology_m1h1c1 as topo
from lib389._constants import *
from lib389.plugins import MemberOfPlugin
from lib389 import agreement, Entry
from lib389.idm.user import UserAccount, UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.group import Groups, Group
from lib389.topologies import topology_m2 as topo_m2
from lib389.replica import Replicas

# Skip on older versions
pytestmark = pytest.mark.skipif(ds_is_older('1.3.7'), reason="Not implemented")

USER_CN = 'user_'
GROUP_CN = 'group1'


DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def add_users(topo_m2, users_num, suffix):
    """Add users to the default suffix
    Return the list of added user DNs.
    """
    users_list = []
    users = UserAccounts(topo_m2.ms["master1"], suffix, rdn=None)
    log.info('Adding %d users' % users_num)
    for num in sample(range(1000), users_num):
        num_ran = int(round(num))
        USER_NAME = 'test%05d' % num_ran
        user = users.create(properties={
            'uid': USER_NAME,
            'sn': USER_NAME,
            'cn': USER_NAME,
            'uidNumber': '%s' % num_ran,
            'gidNumber': '%s' % num_ran,
            'homeDirectory': '/home/%s' % USER_NAME,
            'mail': '%s@redhat.com' % USER_NAME,
            'userpassword': 'pass%s' % num_ran,
        })
        users_list.append(user)
    return users_list


def config_memberof(server):
    # Configure fractional to prevent total init to send memberof
    memberof = MemberOfPlugin(server)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    server.restart()
    ents = server.agreement.list(suffix=DEFAULT_SUFFIX)
    for ent in ents:
        log.info('update %s to add nsDS5ReplicatedAttributeListTotal' % ent.dn)
        server.agreement.setProperties(agmnt_dn=ents[0].dn,
                                       properties={RA_FRAC_EXCLUDE:'(objectclass=*) $ EXCLUDE memberOf',
                                                   RA_FRAC_EXCLUDE_TOTAL_UPDATE:'(objectclass=*) $ EXCLUDE '})


def send_updates_now(server):
    ents = server.agreement.list(suffix=DEFAULT_SUFFIX)
    for ent in ents:
        server.agreement.pause(ent.dn)
        server.agreement.resume(ent.dn)


def _find_memberof(server, member_dn, group_dn):
    #To get the specific server's (M1, C1 and H1) user and group
    user = UserAccount(server, member_dn)
    assert user.exists()
    group = Group(server, group_dn)
    assert group.exists()

    #test that the user entry should have memberof attribute with sepecified group dn value
    assert group._dn in user.get_attr_vals_utf8('memberOf')


@pytest.mark.bz1352121
def test_memberof_with_repl(topo):
    """Test that we allowed to enable MemberOf plugin in dedicated consumer

    :id: 60c11636-55a1-4704-9e09-2c6bcc828de4
    :setup: 1 Master - 1 Hub - 1 Consumer
    :steps:
        1. Configure replication to EXCLUDE memberof
        2. Enable memberof plugin
        3. Create users/groups
        4. Make user_0 member of group_0
        5. Checks that user_0 is memberof group_0 on M,H,C
        6. Make group_0 member of group_1 (nest group)
        7. Checks that user_0 is memberof group_0 and group_1 on M,H,C
        8. Check group_0 is memberof group_1 on M,H,C
        9. Remove group_0 from group_1
        10. Check group_0 and user_0 are NOT memberof group_1 on M,H,C
        11. Remove user_0 from group_0
        12. Check user_0 is not memberof group_0 and group_1 on M,H,C
        13. Disable memberof on C
        14. make user_0 member of group_1
        15. Checks that user_0 is memberof group_0 on M,H but not on C
        16. Enable memberof on C
        17. Checks that user_0 is memberof group_0 on M,H but not on C
        18. Run memberof fixup task
        19. Checks that user_0 is memberof group_0 on M,H,C
    :expectedresults:
        1. Configuration should be successful
        2. Plugin should be enabled
        3. Users and groups should be created
        4. user_0 should be member of group_0
        5. user_0 should be memberof group_0 on M,H,C
        6. group_0 should be member of group_1
        7. user_0 should be memberof group_0 and group_1 on M,H,C
        8. group_0 should be memberof group_1 on M,H,C
        9. group_0 from group_1 removal should be successful
        10. group_0 and user_0 should not be memberof group_1 on M,H,C
        11. user_0 from group_0 remove should be successful
        12. user_0 should not be memberof group_0 and group_1 on M,H,C
        13. memberof should be disabled on C
        14. user_0 should be member of group_1
        15. user_0 should be memberof group_0 on M,H and should not on C
        16. Enable memberof on C should be successful
        17. user_0 should be memberof group_0 on M,H should not on C
        18. memberof fixup task should be successful
        19. user_0 should be memberof group_0 on M,H,C
    """

    M1 = topo.ms["master1"]
    H1 = topo.hs["hub1"]
    C1 = topo.cs["consumer1"]

    # Step 1 & 2
    M1.config.enable_log('audit')
    config_memberof(M1)
    M1.restart()

    H1.config.enable_log('audit')
    config_memberof(H1)
    H1.restart()

    C1.config.enable_log('audit')
    config_memberof(C1)
    C1.restart()

    #Declare lists of users and groups
    test_users = []
    test_groups = []

    # Step 3
    #In for loop create users and add them in the user list
    #it creates user_0 to user_9 (range is fun)
    for i in range(10):
        CN = '%s%d' % (USER_CN, i)
        users = UserAccounts(M1, SUFFIX)
        user_props = TEST_USER_PROPERTIES.copy()
        user_props.update({'uid': CN, 'cn': CN, 'sn': '_%s' % CN})
        testuser = users.create(properties=user_props)
        time.sleep(2)
        test_users.append(testuser)

    #In for loop create groups and add them to the group list
    #it creates group_0 to group_2 (range is fun)
    for i in range(3):
        CN = '%s%d' % (GROUP_CN, i)
        groups = Groups(M1, SUFFIX)
        testgroup = groups.create(properties={'cn' : CN})
        time.sleep(2)
        test_groups.append(testgroup)

    # Step 4
    #Now start testing by adding differnt user to differn group
    if not ds_is_older('1.3.7'):
           test_groups[0].remove('objectClass', 'nsMemberOf')

    member_dn = test_users[0].dn
    grp0_dn = test_groups[0].dn
    grp1_dn = test_groups[1].dn

    test_groups[0].add_member(member_dn)
    time.sleep(5)

    # Step 5
    for i in [M1, H1, C1]:
        _find_memberof(i, member_dn, grp0_dn)

    # Step 6
    test_groups[1].add_member(test_groups[0].dn)
    time.sleep(5)

    # Step 7
    for i in [grp0_dn, grp1_dn]:
        for inst in [M1, H1, C1]:
             _find_memberof(inst, member_dn, i)

    # Step 8
    for i in [M1, H1, C1]:
        _find_memberof(i, grp0_dn, grp1_dn)

    # Step 9
    test_groups[1].remove_member(test_groups[0].dn)
    time.sleep(5)

    # Step 10
    # For negative testcase, we are using assertionerror
    for inst in [M1, H1, C1]:
        for i in [grp0_dn, member_dn]:
             with pytest.raises(AssertionError):
                _find_memberof(inst, i, grp1_dn)

    # Step 11
    test_groups[0].remove_member(member_dn)
    time.sleep(5)

    # Step 12
    for inst in [M1, H1, C1]:
        for grp in [grp0_dn, grp1_dn]:
            with pytest.raises(AssertionError):
                _find_memberof(inst, member_dn, grp)

    # Step 13
    C1.plugins.disable(name=PLUGIN_MEMBER_OF)
    C1.restart()

    # Step 14
    test_groups[0].add_member(member_dn)
    time.sleep(5)

    # Step 15
    for i in [M1, H1]:
        _find_memberof(i, member_dn, grp0_dn)
    with pytest.raises(AssertionError):
        _find_memberof(C1, member_dn, grp0_dn)

    # Step 16
    memberof = MemberOfPlugin(C1)
    memberof.enable()
    C1.restart()

    # Step 17
    for i in [M1, H1]:
        _find_memberof(i, member_dn, grp0_dn)
    with pytest.raises(AssertionError):
        _find_memberof(C1, member_dn, grp0_dn)

    # Step 18
    memberof.fixup(SUFFIX)
    time.sleep(5)

    # Step 19
    for i in [M1, H1, C1]:
        _find_memberof(i, member_dn, grp0_dn)


@pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")
def test_scheme_violation_errors_logged(topo):
    """Check that ERR messages are verbose enough, if a member entry
    doesn't have the appropriate objectclass to support 'memberof' attribute

    :id: e2af0aaa-447e-4e85-a5ce-57ae66260d0b
    :setup: Standalone instance
    :steps:
        1. Enable memberofPlugin and set autoaddoc to nsMemberOf
        2. Restart the instance
        3. Add a user without nsMemberOf attribute
        4. Create a group and add the user to the group
        5. Check that user has memberOf attribute
        6. Check the error log for ".*oc_check_allowed_sv.*USER_DN.*memberOf.*not allowed.*"
           and ".*schema violation caught - repair operation.*" patterns
    :expectedresults:
        1. Should be successful
        2. Should be successful
        3. Should be successful
        4. Should be successful
        5. User should have the attribute
        6. Errors should be logged
    """

    inst = topo.ms["master1"]
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    inst.restart()

    users = UserAccounts(inst, SUFFIX)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': USER_CN, 'cn': USER_CN, 'sn': USER_CN})
    testuser = users.create(properties=user_props)
    testuser.remove('objectclass', 'nsMemberOf')

    groups = Groups(inst, SUFFIX)
    testgroup = groups.create(properties={'cn': GROUP_CN})

    testgroup.add('member', testuser.dn)

    user_memberof_attr = testuser.get_attr_val_utf8('memberof')
    assert user_memberof_attr
    log.info('memberOf attr value - '.format(user_memberof_attr))

    pattern = ".*oc_check_allowed_sv.*{}.*memberOf.*not allowed.*".format(testuser.dn)
    log.info("pattern = %s" % pattern)
    assert inst.ds_error_log.match(pattern)

    pattern = ".*schema violation caught - repair operation.*"
    assert inst.ds_error_log.match(pattern)


@pytest.mark.bz1192099
def test_memberof_with_changelog_reset(topo_m2):
    """Test that replication does not break, after DS stop-start, due to changelog reset

    :id: 60c11636-55a1-4704-9e09-2c6bcc828de4
    :setup: 2 Masters
    :steps:
        1. On M1 and M2, Enable memberof
        2. On M1, add 999 entries allowing memberof
        3. On M1, add a group with these 999 entries as members
        4. Stop M1 in between,
           when add the group memerof is called and before it is finished the
           add, so step 4 should be executed after memberof has started and
           before the add has finished
        5. Check that replication is working fine
    :expectedresults:
        1. memberof should be enabled
        2. Entries should be added
        3. Add operation should start
        4. M1 should be stopped
        5. Replication should be working fine
    """
    m1 = topo_m2.ms["master1"]
    m2 = topo_m2.ms["master2"]

    log.info("Configure memberof on M1 and M2")
    memberof = MemberOfPlugin(m1)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    m1.restart()

    memberof = MemberOfPlugin(m2)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    m2.restart()

    log.info("On M1, add 999 test entries allowing memberof")
    users_list = add_users(topo_m2, 999, DEFAULT_SUFFIX)

    log.info("On M1, add a group with these 999 entries as members")
    dic_of_attributes = {'cn': ensure_bytes('testgroup'),
                         'objectclass': ensure_list_bytes(['top', 'groupOfNames'])}

    for user in users_list:
        dic_of_attributes.setdefault('member',[])
        dic_of_attributes['member'].append(user.dn)

    log.info('Adding the test group using async function')
    groupdn = 'cn=testgroup,%s' % DEFAULT_SUFFIX
    m1.add(Entry((groupdn, dic_of_attributes)))

    #shutdown the server in-between adding the group
    m1.stop()

    #start the server
    m1.start()

    log.info("Check the log messages for error")
    error_msg = "ERR - NSMMReplicationPlugin - ruv_compare_ruv"
    assert not m1.ds_error_log.match(error_msg)

    log.info("Check that the replication is working fine both ways, M1 <-> M2")
    replicas_m1 = Replicas(m1)
    replicas_m2 = Replicas(m2)
    replicas_m1.test(DEFAULT_SUFFIX, m2)
    replicas_m2.test(DEFAULT_SUFFIX, m1)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)


