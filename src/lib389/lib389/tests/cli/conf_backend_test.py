# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import pytest

from lib389.cli_conf.backend import backend_list, backend_get, backend_get_dn, backend_create, backend_delete, backend_export, backend_import

from lib389.cli_base import LogCapture, FakeArgs
from lib389.tests.cli import topology
from lib389.topologies import topology_st

from lib389.utils import ds_is_older
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")


# Topology is pulled from __init__.py
def test_basic(topology):
    # 
    args = FakeArgs()
    backend_list(topology.standalone, None, topology.logcap.log, None)
    # Assert none.
    assert(topology.logcap.contains("No objects to display"))
    topology.logcap.flush()
    # Add a backend
    # We need to fake the args
    args.cn = 'userRoot'
    args.nsslapd_suffix = 'dc=example,dc=com'
    backend_create(topology.standalone, None, topology.logcap.log, args)
    # Assert one.
    backend_list(topology.standalone, None, topology.logcap.log, None)
    # Assert none.
    assert(topology.logcap.contains("userRoot"))
    topology.logcap.flush()
    # Assert we can get by name, suffix, dn
    args.selector = 'userRoot'
    backend_get(topology.standalone, None, topology.logcap.log, args)
    # Assert none.
    assert(topology.logcap.contains("userRoot"))
    topology.logcap.flush()
    # Assert we can get by name, suffix, dn
    args.dn = 'cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
    backend_get_dn(topology.standalone, None, topology.logcap.log, args)
    # Assert none.
    assert(topology.logcap.contains("userRoot"))
    topology.logcap.flush()
    # delete it
    backend_delete(topology.standalone, None, topology.logcap.log, args, warn=False)
    backend_list(topology.standalone, None, topology.logcap.log, None)
    # Assert none.
    assert(topology.logcap.contains("No objects to display"))
    topology.logcap.flush()
    # Done!


def test_import_export(topology_st):
    BE_NAME = 'userRoot'
    EXCLUDE_SUFFIX = "ou=Groups,dc=example,dc=com"
    LDIF_PATH = os.path.join(topology_st.standalone.ds_paths.ldif_dir, "test_import_export.ldif")
    topology_st.logcap = LogCapture()
    args = FakeArgs()
    # Export the backend
    args.be_names = [BE_NAME]
    args.ldif = LDIF_PATH
    args.use_id2entry = None
    args.encrypted = None
    args.min_base64 = None
    args.no_dump_uniq_id = None
    args.replication = None
    args.not_folded = None
    args.no_seq_num = None
    args.include_suffixes = None
    args.exclude_suffixes = [EXCLUDE_SUFFIX]
    backend_export(topology_st.standalone, None, topology_st.logcap.log, args)
    # Assert the right ldif was created
    os.path.exists(LDIF_PATH)
    assert os.path.exists(LDIF_PATH)
    with open(LDIF_PATH, 'r') as ldif:
        for line in ldif:
            assert not line.endswith("%s\n" % EXCLUDE_SUFFIX)
    # Assert the ldif was created
    os.path.exists(LDIF_PATH)
    # Import the backend
    args.be_name = BE_NAME
    args.ldifs = [LDIF_PATH]
    args.chunks_size = None
    args.encrypted = None
    args.gen_uniq_id = None
    args.only_core = None
    args.include_suffixes = None
    args.exclude_suffixes = None
    backend_import(topology_st.standalone, None, topology_st.logcap.log, args)
    # No error has happened! Done!
    # Clean up
    os.remove(LDIF_PATH)
