# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import os
import ldap
import time
import logging
from lib389.utils import ensure_str
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.group import Group, Groups

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1


@pytest.fixture(scope='function')
def group1(topology_st, request):
    groupname = 'group1'
    log.info('Create group1')
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    if groups.exists(groupname):
        groups.get(groupname).delete()

    group1 = groups.create(properties={'cn': groupname})

    def fin():
        if group1.exists():
            log.info('Delete group1')
            group1.delete()

    request.addfinalizer(fin)

    return group1


@pytest.fixture(scope='function')
def group2(topology_st, request):
    groupname = 'group2'
    log.info('Create group2')
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    if groups.exists(groupname):
        groups.get(groupname).delete()

    group2 = groups.create(properties={'cn': groupname})

    def fin():
        if group2.exists():
            log.info('Delete group2')
            group2.delete()

    request.addfinalizer(fin)

    return group2


@pytest.fixture(scope='function')
def user1(topology_st, request):
    username = 'user1'
    log.info('Create user1')
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    if users.exists(username):
        users.get(username).delete()

    user1 = users.create(properties={'cn': username,
                                     'uid': username,
                                     'sn': username,
                                     'uidNumber': '1',
                                     'gidNumber': '11',
                                     'homeDirectory': f'/home/{username}'})

    def fin():
        if user1.exists():
            log.info('Delete user1')
            user1.delete()

    request.addfinalizer(fin)

    return user1


@pytest.fixture(scope='function')
def config_memberof(topology_st, request):
    """ Configure the MemberOf plugin with memberofskipnested=on """
    memberof = MemberOfPlugin(topology_st.standalone)
    memberof.enable()
    memberof.set('memberofskipnested', 'on')
    topology_st.standalone.restart()

    def fin():
        """ Disable the MemberOf plugin and set memberofskipnested=off """
        memberof.disable()
        memberof.set('memberofskipnested', 'off')
        topology_st.standalone.restart()
    
    request.addfinalizer(fin)


def check_membership(user, group_dn, find_result=True):
    """Check if a user has memberOf attribute for the specified group"""
    memberof_values = user.get_attr_vals_utf8_l('memberof')
    found = group_dn.lower() in memberof_values
    
    if find_result:
        assert found, f"User {user.dn} should be a member of {group_dn}"
    else:
        assert not found, f"User {user.dn} should NOT be a member of {group_dn}"


def test_memberof_skipnested(topology_st, config_memberof, user1, group1, group2):
    """Test that memberOf plugin respects memberofskipnested setting for nested groups

    :id: c7831e82-1ed9-11ef-9262-482ae39447e5
    :setup: Standalone Instance with memberOf plugin enabled and memberofskipnested: on
    :steps:
        1. Verify user is not initially a member of any groups
        2. Add user to group1
        3. Add group1 to group2 (creating nested group structure)
        4. Verify user has memberOf value for group1 only
        5. Verify group1 has memberOf value for group2
        6. Verify user does NOT have memberOf value for group2 (due to memberofskipnested=on)
        7. Delete group2
        8. Verify user still has memberOf value for group1
        9. Verify group1 no longer has memberOf value for deleted group2
        10. Verify group2 is properly deleted
    :expectedresults:
        1. User should not be member of any groups initially
        2. User should be successfully added to group1
        3. Nested group structure should be created successfully
        4. User should have memberOf attribute pointing to group1
        5. Group1 should have memberOf attribute pointing to group2
        6. User should NOT have memberOf attribute pointing to group2 (skipnested behavior)
        7. Group2 should be deleted successfully
        8. User should retain memberOf attribute for group1
        9. Group1 should lose memberOf attribute for deleted group2
        10. Group2 should no longer exist in directory
    """

    # Verify that the user is not a member of the groups
    check_membership(user1, group1.dn, False)
    check_membership(user1, group2.dn, False)

    # Add the user to group1
    group1.add_member(user1.dn)

    # Add group1 to group2 and thus creating a nested group
    group2.add_member(group1.dn)
    
    # Verify that the user has memberof value of group1
    check_membership(user1, group1.dn, True)
    # Verify that group1 has memberof value of group2
    check_membership(group1, group2.dn, True)
    # Verify that the user is not a member of group2 due to memberofskipnested=on
    check_membership(user1, group2.dn, False)

    # Delete group2
    group2.delete()

    # Verify that the user has memberof value of group1
    check_membership(user1, group1.dn, True)
    # Verify group2 membership values are removed
    check_membership(group1, group2.dn, False)
    check_membership(user1, group2.dn, False)

    # Additional verification: ensure the user is no longer referenced in deleted group
    # This should not raise an exception since group2 is deleted
    try:
        deleted_group = Group(topology_st.standalone, group2.dn)
        assert not deleted_group.exists(), "Group2 should be deleted"
    except ldap.NO_SUCH_OBJECT:
        # Expected behavior - group is deleted
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
