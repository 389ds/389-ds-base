# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# Test the cli tools from the dsctl command for correct behaviour.

import os
import pytest
from lib389.cli_ctl.dbtasks import dbtasks_db2index, dbtasks_db2bak, dbtasks_db2ldif, dbtasks_ldif2db, dbtasks_bak2db

from lib389.cli_base import LogCapture, FakeArgs
from lib389.tests.cli import topology, topology_be_latest

from lib389.utils import ds_is_older
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")

def test_db2index(topology):
    pass

def test_db2bak_bak2db(topology_be_latest):
    standalone = topology_be_latest.standalone
    standalone.stop()
    args = FakeArgs()
    args.archive = os.path.join(standalone.get_bak_dir(), "testdb2bak")
    # Stop the instance
    dbtasks_db2bak(standalone, topology_be_latest.logcap.log, args)
    # Assert none.
    assert topology_be_latest.logcap.contains("db2bak successful")
    topology_be_latest.logcap.flush()
    # We can re-use the same arguments
    dbtasks_bak2db(standalone, topology_be_latest.logcap.log, args)
    # Assert none.
    assert topology_be_latest.logcap.contains("bak2db successful")

def test_ldif2db_db2ldif_no_repl(topology_be_latest):
    standalone = topology_be_latest.standalone
    standalone.stop()
    args = FakeArgs()
    args.backend = 'userRoot'
    args.ldif = os.path.join(standalone.get_ldif_dir(), "test.ldif")
    args.encrypted = False
    args.replication = False
    # Stop the instance
    dbtasks_db2ldif(standalone, topology_be_latest.logcap.log, args)
    # Assert none.
    assert topology_be_latest.logcap.contains("db2ldif successful")
    topology_be_latest.logcap.flush()
    # We can re-use the same arguments
    dbtasks_ldif2db(standalone, topology_be_latest.logcap.log, args)
    # Assert none.
    assert topology_be_latest.logcap.contains("ldif2db successful")

def test_ldif2db_db2ldif_repl(topology_be_latest):
    standalone = topology_be_latest.standalone
    standalone.stop()
    args = FakeArgs()
    args.backend = 'userRoot'
    args.ldif = os.path.join(standalone.get_ldif_dir(), "test.ldif")
    args.encrypted = False
    args.replication = False
    args.archive = os.path.join(standalone.get_ldif_dir(), "test.ldif")
    # Stop the instance
    dbtasks_db2ldif(standalone, topology_be_latest.logcap.log, args)
    # Assert none.
    assert topology_be_latest.logcap.contains("db2ldif successful")
    topology_be_latest.logcap.flush()
    # We can re-use the same arguments
    dbtasks_ldif2db(standalone, topology_be_latest.logcap.log, args)
    # Assert none.
    assert topology_be_latest.logcap.contains("ldif2db successful")
