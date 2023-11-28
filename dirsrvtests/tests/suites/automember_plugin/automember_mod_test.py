# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
import ldap
import logging
import pytest
import os
import time
from lib389.utils import ds_is_older
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import AutoMembershipPlugin, AutoMembershipDefinitions
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.topologies import topology_st as topo

# Skip on older versions
pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")]

MAX_TRIES = 10

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def automember_fixture(topo, request):
    # Create group
    groups = []
    group_obj = Groups(topo.standalone, DEFAULT_SUFFIX)
    groups.append(group_obj.create(properties={'cn': 'testgroup'}))
    groups.append(group_obj.create(properties={'cn': 'testgroup2'}))
    groups.append(group_obj.create(properties={'cn': 'testgroup3'}))

    # Create test user
    user_accts = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = user_accts.create_test_user()

    # Create extra users
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for i in range(0, 100):
        users.create_test_user(uid=i)

    # Create automember definitions and regex rules
    automember_prop = {
        'cn': 'testgroup_definition',
        'autoMemberScope': DEFAULT_SUFFIX,
        'autoMemberFilter': 'objectclass=posixaccount',
        'autoMemberDefaultGroup': groups[0].dn,
        'autoMemberGroupingAttr': 'member:dn',
    }
    automembers = AutoMembershipDefinitions(topo.standalone)
    auto_def = automembers.create(properties=automember_prop)
    auto_def.add_regex_rule("regex1", groups[1].dn, include_regex=['cn=mark.*'])
    auto_def.add_regex_rule("regex2", groups[2].dn, include_regex=['cn=simon.*'])

    # Enable plugin
    automemberplugin = AutoMembershipPlugin(topo.standalone)
    automemberplugin.enable()
    topo.standalone.restart()

    return user, groups


def test_mods(automember_fixture, topo):
    """Modify the user so that it is added to the various automember groups

    :id: 28a2b070-7f16-4905-8831-c80fa6441693
    :setup: Standalone Instance
    :steps:
        1. Update user that should add it to group[0]
        2. Update user that should add it to group[1]
        3. Update user that should add it to group[2]
        4. Update user that should add it to group[0]
        5. Test rebuild task adds user to group[1]
        6. Test rebuild task cleanups groups and only adds it to group[1]
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    (user, groups) = automember_fixture

    # Update user which should go into group[0]
    user.replace('cn', 'whatever')
    assert groups[0].is_member(user.dn)
    if groups[1].is_member(user.dn):
        assert False
    if groups[2].is_member(user.dn):
        assert False

    # Update user0 which should go into group[1]
    user.replace('cn', 'mark')
    assert groups[1].is_member(user.dn)
    if groups[0].is_member(user.dn):
        assert False
    if groups[2].is_member(user.dn):
        assert False

    # Update user which should go into group[2]
    user.replace('cn', 'simon')
    assert groups[2].is_member(user.dn)
    if groups[0].is_member(user.dn):
        assert False
    if groups[1].is_member(user.dn):
        assert False

    # Update user which should go back into group[0] (full circle)
    user.replace('cn', 'whatever')
    assert groups[0].is_member(user.dn)
    if groups[1].is_member(user.dn):
        assert False
    if groups[2].is_member(user.dn):
        assert False

    #
    # Test rebuild task.  First disable plugin
    #
    automemberplugin = AutoMembershipPlugin(topo.standalone)
    automemberplugin.disable()
    topo.standalone.restart()

    # Make change that would move the entry from group[0] to group[1]
    user.replace('cn', 'mark')

    # Enable plugin
    automemberplugin.enable()
    topo.standalone.restart()

    # Run rebuild task (no cleanup)
    task = automemberplugin.fixup(DEFAULT_SUFFIX, "objectclass=posixaccount")
    for try in range(1,MAX_TRIES+1):
        try:
            # test only one fixup task is allowed at a time
            task2 = automemberplugin.fixup(DEFAULT_SUFFIX, "objectclass=top")
            # But sometime first task finished before we start the second one
            assert task.get_attr_val_utf8('nsTaskExitCode') == '0'
            task=task2
            assert try < MAX_TRIES
            continue
        except ldap.UNWILLING_TO_PERFORM:
            break
    task.wait()

    # Test membership (user should still be in groups[0])
    assert groups[1].is_member(user.dn)
    if not groups[0].is_member(user.dn):
        assert False

    # Run rebuild task with cleanup
    task = automemberplugin.fixup(DEFAULT_SUFFIX, "objectclass=posixaccount", cleanup=True)
    task.wait()

    # Test membership (user should only be in groups[1])
    assert groups[1].is_member(user.dn)
    if groups[0].is_member(user.dn):
        assert False
    if groups[2].is_member(user.dn):
        assert False

    # Success
    log.info("Test PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
