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
import logging
from . import check_membership
from lib389.utils import ds_is_older
from lib389.topologies import topology_st
from lib389._constants import LOG_PLUGIN, LOG_DEFAULT
from lib389.plugins import MemberOfPlugin

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.3.7'), reason="Not implemented")]


def test_memberof_modrdn(topology_st, user1, group1):
    """Test that memberOf plugin correctly handles modrdn operations on groups

    :id: a4c7f8e9-2f1a-11ef-8a3c-482ae39447e5
    :setup: Standalone Instance with user1 and group1 fixtures
    :steps:
        1. Enable the memberOf plugin and restart the server
        2. Verify user is not initially a member of the group
        3. Add user to the group
        4. Verify user is now a member of the group
        5. Enable plugin logging to capture modrdn operations
        6. Perform modrdn operation on group (rename to itself)
        7. Verify user is still a member of the group after rename
        8. Verify memberOf plugin logs skip message for identical src/dst rename
    :expectedresults:
        1. MemberOf plugin should be enabled successfully
        2. User should not have memberOf attribute initially
        3. User should be added to group successfully
        4. User should have memberOf attribute pointing to group
        5. Plugin logging should be enabled successfully
        6. Modrdn operation should complete without errors
        7. User should retain memberOf attribute after rename operation
        8. Plugin should log that modrdn was skipped due to identical src/dst
    """
    
    # Enable the MemberOf plugin
    memberof = MemberOfPlugin(topology_st.standalone)
    memberof.enable()
    topology_st.standalone.restart()

    # Verify that the user is not a member of the group
    check_membership(user1, group1.dn, False)
    # Add the user to the group
    group1.add_member(user1.dn)
    # Verify that the user is a member of the group
    check_membership(user1, group1.dn, True)

    # Enable the plugin log to capture memberof modrdn callback notification
    topology_st.standalone.config.loglevel(vals=[LOG_PLUGIN, LOG_DEFAULT], service='error')

    # Rename the group on itself
    group1.rename('cn=group1')
    
    # Verify that the user is still a member of the group
    check_membership(user1, group1.dn, True)

    # Verify that the memberof modrdn callback notification is logged
    assert topology_st.standalone.ds_error_log.match('.*Skip modrdn operation because src/dst identical.*')



if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])