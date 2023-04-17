# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import logging
import pytest
import time
import subprocess
import os
from lib389.topologies import topology_st as topo
from lib389.backend import Backends, DatabaseConfig
from lib389.dseldif import DSEldif
from lib389.plugins import Plugins

log = logging.getLogger(__name__)


def test_upgradednformat(topo):
    """Test the upgradednformat does not crash the server

    :id: 5f138d97-9384-4c9b-ad66-dc902b61de99
    :setup: Standalone Instance
    :steps:
        1. Check we are running in BDB else skip the test
        2. Stop Server
        3. launch upgradednformat subcommand
        4. Check messages of completion are logged in error logs
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    # Are we using BDB?
    topo.standalone.restart()
    db_config = DatabaseConfig(topo.standalone)
    db_lib = db_config.get_db_lib()

    # Check for library specific attributes
    if db_lib == 'bdb':
        pass
    elif db_lib == 'mdb':
        # upgradednformat only applicable with BDB
        return
    else:
        # Unknown - the server would probably fail to start but check it anyway
        log.fatal(f'Unknown backend library: {db_lib}')
        assert False

    # Stop the server
    topo.standalone.stop()

    # Launch upgradednformat command
    cmd = ["/usr/sbin/ns-slapd", "upgradednformat", "-D", topo.standalone.ds_paths.config_dir, "-n", "userroot", "-a", "%s/userRoot" % topo.standalone.dbdir]
    log.debug(f"DEBUG: Running {cmd}")
    try:
        output = subprocess.check_output(cmd, universal_newlines=True, stderr=subprocess.STDOUT)
    except:
        pass

    # likely useless as write to error log is in-sync
    time.sleep(2)

    # Check for successful completion
    assert topo.standalone.ds_error_log.match('.*Start upgrade dn format.*')
    assert topo.standalone.ds_error_log.match('.*upgradednformat - Instance userroot in.*is up-to-date.*')

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

