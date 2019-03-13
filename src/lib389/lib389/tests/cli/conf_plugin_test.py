# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest

from lib389.cli_conf.plugin import plugin_list, plugin_get

from lib389.cli_base import LogCapture, FakeArgs

from lib389.tests.cli import topology

plugins = [
    'Class of Service',
    'ldbm database',
    'Roles Plugin',
    'USN',
    'SSHA512',
]

# Topology is pulled from __init__.py
def test_plugin_cli(topology):
    args = FakeArgs()

    plugin_list(topology.standalone, None, topology.logcap.log, None)
    for p in plugins:
        assert(topology.logcap.contains(p))
    topology.logcap.flush()

    # print(topology.logcap.outputs)
    # Need to delete something, then re-add it.
    args.selector = 'USN'
    plugin_get(topology.standalone, None, topology.logcap.log, args)
    assert(topology.logcap.contains('USN'))
    topology.logcap.flush()

    args.dn = 'cn=USN,cn=plugins,cn=config'
    plugin_get_dn(topology.standalone, None, topology.logcap.log, args)
    assert(topology.logcap.contains('USN'))
    topology.logcap.flush()

    plugin_disable(topology.standalone, None, topology.logcap.log, args, warn=False)
    assert(topology.logcap.contains('Disabled'))
    topology.logcap.flush()

    plugin_enable(topology.standalone, None, topology.logcap.log, args)
    assert(topology.logcap.contains('Enabled'))
    topology.logcap.flush()

