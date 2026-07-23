# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
from test389.topologies import topology_st as topo
from lib389.dseldif import DSEldif

log = logging.getLogger(__name__)


def test_sync_plugin(topo):
    """Test that the replication plugin name is updated to the new name at
    server startup.

    :id: c275f870-aa32-49bf-bfd3-d18b9c3ae250
    :setup: Standalone Instance
    :steps:
        1. Stop Server
        2. add nsslapd-pluginarg0 in content sync config entry
        3. Start server
        4. Verify nsslapd-pluginarg0 is removed
        5. Verify syncrepl-max-concurrent is added
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    SYNC_PLUGIN_DN = "cn=Content Synchronization,cn=plugins,cn=config"
    REPL_PLUGIN_NAME = "Multisupplier Replication Plugin"
    SYNC_ATTR_OLD_NAME = "nsslapd-pluginarg0"
    SYNC_ATTR_NEW_NAME = "syncrepl-max-concurrent"
    SYNC_ATTR_VAL = "5"

    # Stop the server
    topo.standalone.stop()

    # Edit sync plugin in dse.ldif
    dse_ldif = DSEldif(topo.standalone)
    dse_ldif.replace(SYNC_PLUGIN_DN, SYNC_ATTR_OLD_NAME, SYNC_ATTR_VAL)

    topo.standalone.restart()
    dse_ldif = DSEldif(topo.standalone)
    pluginarg0 = dse_ldif.get(SYNC_PLUGIN_DN, SYNC_ATTR_OLD_NAME, single=True)
    assert pluginarg0 == None
    max_concurrent = dse_ldif.get(SYNC_PLUGIN_DN, SYNC_ATTR_NEW_NAME, single=True)
    assert int(max_concurrent) == int(SYNC_ATTR_VAL)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

