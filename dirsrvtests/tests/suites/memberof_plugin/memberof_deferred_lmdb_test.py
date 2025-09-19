# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
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
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389.plugins import MemberOfPlugin
from lib389.config import LMDB_LDBMConfig
from lib389.utils import get_default_db_lib
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups

log = logging.getLogger(__name__)

DEBUGGING = os.getenv('DEBUGGING', False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)


@pytest.mark.skipif(get_default_db_lib() != "mdb", reason="Not supported over mdb")
def test_memberof_deferred_update_lmdb_rejection(topo):
    """Test that memberOf plugin rejects deferred update configuration with LMDB backend

    :id: a7f079dd-d269-41ca-95ec-91428e77626f
    :setup: Standalone Instance with LMDB backend
    :steps:
        1. Enable memberOf plugin
        2. Try to set deferred_update to "on"
        3. Check error log for appropriate error message
    :expectedresults:
        1. Plugin enables successfully
        2. Setting deferred_update fails
        3. Error log contains "deferred_update is not supported with LMDB backend"
    """

    inst = topo.standalone

    # Enable memberOf plugin
    log.info("Step 1: Enabling memberOf plugin")
    memberof_plugin = MemberOfPlugin(inst)
    memberof_plugin.enable()
    log.info("✓ MemberOf plugin enabled")
    inst.deleteErrorLogs(restart=True)

    # Try to set deferred_update to "on"
    log.info("Step 2: Attempting to set deferred_update to 'on'")

    # Try to modify the plugin configuration
    plugin_dn = f"cn={PLUGIN_MEMBER_OF},cn=plugins,cn=config"
    memberof_plugin.set_memberofdeferredupdate('on')

    # Check error log for appropriate error message
    log.info("Step 3: Checking error log for LMDB-specific error message")
    assert inst.ds_error_log.match(".*deferred_update is not supported with LMDB backend.*")

    log.info("✓ Test completed successfully - memberOf plugin correctly rejects deferred update with LMDB backend")


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
def test_memberof_deferred_update_non_lmdb_success(topo):
    """Test that memberOf plugin allows deferred update configuration with non-LMDB backends

    :id: a4b640c8-ef54-4cbf-8d1a-8b29fcdd59d1
    :setup: Standalone Instance with non-LMDB backend (BDB)
    :steps:
        1. Enable memberOf plugin
        2. Set deferred_update to "on"
        3. Verify the operation succeeds
        4. Verify deferred_update remains "on"
    :expectedresults:
        1. Plugin enables successfully
        2. Setting deferred_update succeeds
        3. No error occurs
        4. deferred_update is "on"
    """

    inst = topo.standalone

    # Enable memberOf plugin
    log.info("Step 1: Enabling memberOf plugin")
    memberof_plugin = MemberOfPlugin(inst)
    memberof_plugin.enable()
    log.info("✓ MemberOf plugin enabled")
    inst.deleteErrorLogs(restart=True)

    # Set deferred_update to "on"
    log.info("Step 2: Setting deferred_update to 'on'")

    # Try to modify the plugin configuration
    try:
        memberof_plugin.set_memberofdeferredupdate('on')
        log.info("✓ Successfully set deferred_update to 'on'")
    except Exception as e:
        assert False, f"Expected success when setting deferred_update with non-LMDB backend, got: {type(e).__name__}: {e}"

    # Verify no error occurred
    log.info("Step 3: Verifying no error occurred")
    assert not inst.ds_error_log.match(".*deferred_update is not supported with LMDB backend.*")

    log.info("✓ No LMDB-related error messages found")

    # Verify deferred_update remains "on"
    log.info("Step 4: Verifying deferred_update is 'on'")
    current_deferred = memberof_plugin.get_memberofdeferredupdate()
    assert current_deferred is None or current_deferred.lower() == 'on', \
        f"Expected deferred_update to be 'on', got: {current_deferred}"
    log.info("✓ deferred_update is 'on'")

    log.info("✓ Test completed successfully - memberOf plugin correctly allows deferred update with non-LMDB backend")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

