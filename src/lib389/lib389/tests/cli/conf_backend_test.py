# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest

from lib389.cli_conf.backend import backend_list, backend_get, backend_get_dn, backend_create, backend_delete

from lib389.cli_base import LogCapture, FakeArgs
from lib389.tests.cli import topology

# Topology is pulled from __init__.py
def test_backend_cli(topology):
    # 
    args = FakeArgs()
    backend_list(topology.standalone, None, topology.logcap.log, None)
    # Assert none.
    assert(topology.logcap.contains("No objects to display"))
    topology.logcap.flush()
    # Add a backend
    # We need to fake the args
    args.extra = ['dc=example,dc=com', 'userRoot']
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

