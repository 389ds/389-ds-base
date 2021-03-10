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
from lib389._constants import *
from lib389.topologies import topology_st as topo

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

    # Set backup directory to LDIF directory
    topo.standalone.config.replace('nsslapd-bakdir', ldif_dir)

    # Stop the server and take a backup
    topo.standalone.stop()
    topo.standalone.db2bak(None)

    # Verify backup was written to LDIF directory
    backups = os.listdir(ldif_dir)
    assert len(backups)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

