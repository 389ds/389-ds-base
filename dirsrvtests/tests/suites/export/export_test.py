# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import pytest
import subprocess
from lib389.topologies import topology_st as topo
from lib389._constants import DEFAULT_SUFFIX, DEFAULT_BENAME
from lib389.utils import *
from lib389.paths import Paths
from lib389.cli_base import FakeArgs
from lib389.cli_ctl.dbtasks import dbtasks_db2ldif

pytestmark = pytest.mark.tier1


def run_db2ldif_and_clear_logs(topology, instance, backend, ldif, output_msg, encrypt=False, repl=False):
    args = FakeArgs()
    args.instance = instance.serverid
    args.backend = backend
    args.encrypted = encrypt
    args.replication = repl
    args.ldif = ldif

    dbtasks_db2ldif(instance, topology.logcap.log, args)

    log.info('checking output msg')
    if not topology.logcap.contains(output_msg):
        log.error('The output message is not the expected one')
        assert False

    log.info('Clear the log')
    topology.logcap.flush()


@pytest.mark.bz1806978
@pytest.mark.ds51188
@pytest.mark.skipif(ds_is_older("1.3.10", "1.4.2"), reason="Not implemented")
def test_dbtasks_db2ldif_with_non_accessible_ldif_file_path(topo):
    """Export with dsctl db2ldif, giving a ldif file path which can't be accessed by the user (dirsrv by default)

    :id: 511e7702-7685-4951-9966-38f402d6214b
    :setup: Standalone Instance - entries imported in the db
    :steps:
        1. Stop the server
        2. Launch db2ldif with an non accessible ldif file path
        3. Catch the reported error code
        4. Check that an appropriate error was returned
    :expectedresults:
        1. Operation successful
        2. Operation properly fails, without crashing
        3. An error code different from 139 (segmentation fault) should be reported
        4. "The LDIF file location does not exist" is returned
    """
    export_ldif = '/tmp/nonexistent/export.ldif'

    log.info("Stopping the instance...")
    topo.standalone.stop()

    log.info("Performing an offline export to a non accessible ldif file path - should fail properly")
    expected_output="The LDIF file location does not exist"
    with pytest.raises(ValueError) as e:
        run_db2ldif_and_clear_logs(topo, topo.standalone, DEFAULT_BENAME, export_ldif, expected_output)
    assert "The LDIF file location does not exist" in str(e.value)

    log.info("Restarting the instance...")
    topo.standalone.start()


@pytest.mark.bz1806978
@pytest.mark.ds51188
@pytest.mark.skipif(ds_is_older("1.4.3.8"), reason="bz1806978 not fixed")
def test_db2ldif_cli_with_non_accessible_ldif_file_path(topo):
    """Export with ns-slapd db2ldif, giving a ldif file path which can't be accessed by the user (dirsrv by default)

    :id: ca91eda7-27b1-4750-a013-531a63d3f5b0
    :setup: Standalone Instance - entries imported in the db
    :steps:
        1. Stop the server
        2. Launch db2ldif with an non accessible ldif file path
        3. Catch the reported error code
        4. Check that an appropriate error was returned
    :expectedresults:
        1. Operation successful
        2. Operation properly fails, without crashing
        3. An error code different from 139 (segmentation fault) should be reported
        4. "The LDIF file location does not exist" is returned
    """
    export_ldif = '/tmp/nonexistent/export.ldif'
    db2ldif_cmd = os.path.join(topo.standalone.ds_paths.sbin_dir, 'dsctl')

    log.info("Stopping the instance...")
    topo.standalone.stop()

    log.info("Performing an offline export to a non accessible ldif file path - should fail properly")
    try:
        subprocess.check_output([db2ldif_cmd, topo.standalone.serverid, 'db2ldif', 'userroot', export_ldif])
    except subprocess.CalledProcessError as e:
        if format(e.returncode) == '139':
            log.error('db2ldif had a Segmentation fault (core dumped)')
            assert False

    log.info("Restarting the instance...")
    topo.standalone.start()


@pytest.mark.bz1860291
@pytest.mark.xfail(reason="bug 1860291")
@pytest.mark.skipif(ds_is_older("1.3.10", "1.4.2"), reason="Not implemented")
def test_dbtasks_db2ldif_with_non_accessible_ldif_file_path_output(topo):
    """Export with db2ldif, giving a ldif file path which can't be accessed by the user (dirsrv by default)

    :id: fcc63387-e650-40a7-b643-baa68c190037
    :setup: Standalone Instance - entries imported in the db
    :steps:
        1. Stop the server
        2. Launch db2ldif with a non accessible ldif file path
        3. check the error reported in the command output
    :expectedresults:
        1. Operation successful
        2. Operation properly fails
        3. An clear error message is reported as output of the cli
    """
    export_ldif = '/tmp/nonexistent/export.ldif'

    log.info("Stopping the instance...")
    topo.standalone.stop()

    log.info("Performing an offline export to a non accessible ldif file path - should fail and output a clear error message")
    expected_output="No such file or directory"
    run_db2ldif_and_clear_logs(topo, topo.standalone, DEFAULT_BENAME, export_ldif, expected_output)
    # This test will possibly have to be updated with the error message reported after bz1860291 fix

    log.info("Restarting the instance...")
    topo.standalone.start()
