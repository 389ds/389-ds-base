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
import logging
from . import check_membership
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccounts
from lib389.idm.group import Group, Groups

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1


@pytest.fixture(scope='function')
def config_memberof(topology_st, request):
    """ Configure the MemberOf plugin with memberofskipnested=on """
    memberof = MemberOfPlugin(topology_st.standalone)
    memberof.enable()
    memberof.enable_skipnested()
    topology_st.standalone.restart()

    def fin():
        """ Disable the MemberOf plugin and set memberofskipnested=off """
        memberof.disable_skipnested()
        memberof.disable()
        topology_st.standalone.restart()
    
    request.addfinalizer(fin)


def test_memberof_skipnested(topology_st, config_memberof, user1,
                             group1, group2, group3, supergroup):
    """Test that memberOf plugin respects memberofskipnested setting for nested groups

    :id: c7831e82-1ed9-11ef-9262-482ae39447e5
    :setup: Standalone Instance with memberOf plugin enabled and memberofskipnested: on
    :steps:
        1. Verify user is not initially a member of any groups
        2. Add user to group1 and group2 directly
        3. Add group1 to supergroup (creating nested group structure)
        4. Verify group1 has memberOf value of supergroup
        5. Verify user has memberOf values for group1 and group2 (direct membership)
        6. Verify user does NOT have memberOf value for supergroup (due to memberofskipnested=on)
        7. Add user to group3 and verify all memberships
        8. Delete group2 and verify membership updates
        9. Delete supergroup and verify final membership state
        10. Verify deleted groups no longer exist in directory
    :expectedresults:
        1. User should not be member of any groups initially
        2. User should be successfully added to group1 and group2
        3. Nested group structure should be created (group1 in supergroup)
        4. Group1 should have memberOf attribute pointing to supergroup
        5. User should have memberOf attributes for direct memberships only (group1 and group2)
        6. User should NOT have memberOf attribute for supergroup (skipnested behavior)
        7. User should gain membership to group3 while retaining others
        8. User should lose membership to deleted group2, retain others
        9. User should retain memberships to existing groups after supergroup deletion
        10. Deleted groups should no longer exist in directory
    """

    # Verify that the user is not a member of the groups
    check_membership(user1, group1.dn, False)
    check_membership(user1, group2.dn, False)

    # Add the user to group1
    group1.add_member(user1.dn)
    group2.add_member(user1.dn)

    # Add group1 to group2 and thus creating a nested group, verify membership
    supergroup.add_member(group1.dn)
    check_membership(group1, supergroup.dn, True)
    
    # Verify that the user has memberof value of group1 and group2, but not supergroup or group3
    check_membership(user1, group1.dn, True)
    check_membership(user1, group2.dn, True)
    check_membership(user1, supergroup.dn, False)
    check_membership(user1, group3.dn, False)

    # Add the user to group3, verify that memberships were updated correctly
    group3.add_member(user1.dn)
    check_membership(user1, group3.dn, True)
    check_membership(user1, group1.dn, True)
    check_membership(user1, group2.dn, True)
    check_membership(user1, supergroup.dn, False)

    # Delete group2
    group2.delete()

    # Verify that the memberships were updated correctly
    check_membership(user1, group1.dn, True)
    check_membership(user1, group2.dn, False)
    check_membership(user1, group3.dn, True)
    check_membership(user1, supergroup.dn, False)

    # Delete supergroup
    supergroup.delete()

    # Verify that the memberships remain correct
    check_membership(user1, group1.dn, True)
    check_membership(user1, group2.dn, False)
    check_membership(user1, group3.dn, True)
    check_membership(user1, supergroup.dn, False)

    # Additional verification: ensure the deleted groups no longer exist
    # This should not raise an exception since groups is deleted
    try:
        deleted_group = Group(topology_st.standalone, group2.dn)
        assert not deleted_group.exists(), "Group2 should be deleted"
        deleted_group = Group(topology_st.standalone, supergroup.dn)
        assert not deleted_group.exists(), "Supergroup should be deleted"
    except ldap.NO_SUCH_OBJECT:
        # Expected behavior - groups are deleted
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
