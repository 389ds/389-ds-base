# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import pytest
import os
import time
from lib389.topologies import topology_st as topo
from lib389.paths import Paths

log = logging.getLogger(__name__)

def test_custom_path(topo):
    """Test that a custom path, backup directory, is correctly used by lib389
    when the server is stopped.

    :id: 8659e209-ee83-477e-8183-1d2f555669ea
    :setup: Standalone Instance
    :steps:
        1. Get the LDIF directory
        2. Change the server's backup directory to the LDIF directory
        3. Stop the server, and perform a backup
        4. Backup was written to LDIF directory
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    # Get LDIF dir
    ldif_dir = topo.standalone.get_ldif_dir()
    bak_dir = topo.standalone.get_bak_dir()
    log.info("ldif dir: " + ldif_dir + " items: " + str(len(os.listdir(ldif_dir))))
    log.info("bak dir: " + bak_dir + " items: " + str(len(os.listdir(bak_dir))))

    # Set backup directory to LDIF directory
    topo.standalone.config.replace('nsslapd-bakdir', ldif_dir)
    time.sleep(.5)

    # Stop the server and take a backup
    inst = topo.standalone
    inst.stop()
    # Refresh ds_paths
    inst.ds_paths = Paths(inst.serverid, instance=inst, local=inst.isLocal)

    time.sleep(.5)
    inst.db2bak(None)  # Bug, bak dir is being pulled from defaults.inf, and not from config

    # Verify backup was written to LDIF directory
    log.info("AFTER: ldif dir (new bak dir): " + ldif_dir + " items: " + str(len(os.listdir(ldif_dir))))
    log.info("AFTER: bak dir: " + bak_dir + " items: " + str(len(os.listdir(bak_dir))))

    assert len(os.listdir(ldif_dir))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
