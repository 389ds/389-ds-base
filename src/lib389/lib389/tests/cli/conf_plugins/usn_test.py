# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016-2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest

from lib389.tests.cli import topology as default_topology
from lib389.cli_base import LogCapture, FakeArgs
from lib389.plugins import USNPlugin
from lib389.cli_conf.plugins import usn as usn_cli


@pytest.fixture(scope="module")
def topology(request):
    topology = default_topology(request)

    plugin = USNPlugin(topology.standalone)
    if not plugin.exists():
        plugin.create()

    # we need to restart the server after enabling the plugin
    plugin.enable()
    topology.standalone.restart()
    topology.logcap.flush()

    return topology


def test_enable_global_mode(topology):
    args = FakeArgs()

    usn_cli.enable_global_mode(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("USN global mode enabled")
    topology.logcap.flush()

    usn_cli.display_usn_mode(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("USN global mode is enabled")
    topology.logcap.flush()

def test_disable_global_mode(topology):
    args = FakeArgs()

    usn_cli.disable_global_mode(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("USN global mode disabled")
    topology.logcap.flush()

    usn_cli.display_usn_mode(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("USN global mode is disabled")
    topology.logcap.flush()
