# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import time
import logging
import pytest
from lib389.topologies import topology_st as topo
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def find_memberof(topo, user, group_dn, find_result=True):
    """Check if a user is a member of a group."""
    found = group_dn.lower() in user.get_attr_vals_utf8_l('memberof')
    print(user.get_attr_vals_utf8_l('memberof'))

    if find_result:
        assert found, f"{user.dn} is not a member of {group_dn}"
    else:
        assert not found, f"{user.dn} is unexpectedly a member of {group_dn}"

@pytest.fixture(scope="module")
def memberof_setup(topo):
    """Set up the MemberOf plugin and create necessary entries."""
    memberof = MemberOfPlugin(topo.standalone)
    memberof.enable()
    memberof.set('memberofgroupattr', 'member')
    topo.standalone.restart()

    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    groups.create(properties={'cn': 'group_1_lvl_1'})
    groups.create(properties={'cn': 'group_lvl_2'})
    groups.create(properties={'cn': 'group_2_lvl_1'})

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    users.create(properties={
        'uid': 'a_user',
        'cn': 'A User',
        'sn': 'User',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/a_user'
    })

    return topo

def test_nested_groups_weird_error(memberof_setup):
    """Test MemberOf plugin functionality with nested groups to ensure no 'Weird' errors occur

    :id: d0ed5547-92e4-4ae7-818b-e2114109d266
    :setup: Standalone instance with MemberOf plugin enabled and configured
    :steps:
        1. Create nested group structure (group_lvl_2 contains group_1_lvl_1)
        2. Add user to group_lvl_2
        3. Add user to group_1_lvl_1
        4. Add user to group_2_lvl_1
        5. Verify group memberships
    :expectedresults:
        1. Nested group structure is created without errors
        2. User is added to group_lvl_2 without 'Weird' errors in the log
        3. User is added to group_1_lvl_1 without 'Weird' errors in the log
        4. User is added to group_2_lvl_1 without 'Weird' errors in the log
        5. User's group memberships are correct for all groups
    """
    topo = memberof_setup

    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group_lvl_2 = groups.get('group_lvl_2')
    group_1_lvl_1 = groups.get('group_1_lvl_1')
    group_2_lvl_1 = groups.get('group_2_lvl_1')

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.get('a_user')

    # Make group_1_lvl_1 memberof group_lvl_2
    group_lvl_2.add('member', group_1_lvl_1.dn)
    find_memberof(topo, group_1_lvl_1, group_lvl_2.dn)

    # Make USER_DN memberof GROUP_LVL_2_DN
    group_lvl_2.add('member', user.dn)

    # Check for errors in the log
    assert not topo.standalone.ds_error_log.match('.*memberof_fix_memberof_callback - Weird.*')

    # Make USER_DN memberof GROUP_1_LVL_1_DN
    group_1_lvl_1.add('member', user.dn)
    assert not topo.standalone.ds_error_log.match('.*memberof_fix_memberof_callback - Weird.*')

    # Make USER_DN memberof GROUP_2_LVL_1_DN
    group_2_lvl_1.add('member', user.dn)
    assert not topo.standalone.ds_error_log.match('.*memberof_fix_memberof_callback - Weird.*')

    # Additional controls
    find_memberof(topo, user, group_lvl_2.dn)
    find_memberof(topo, user, group_1_lvl_1.dn)
    find_memberof(topo, user, group_2_lvl_1.dn)
    find_memberof(topo, group_1_lvl_1, group_lvl_2.dn)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
