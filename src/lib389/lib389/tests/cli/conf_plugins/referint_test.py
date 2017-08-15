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
from lib389.plugins import ReferentialIntegrityPlugin
from lib389.cli_conf.plugins import referint as referint_cli


@pytest.fixture(scope="module")
def topology(request):
    topology = default_topology(request)

    plugin = ReferentialIntegrityPlugin(topology.standalone)
    if not plugin.exists():
        plugin.create()

    # we need to restart the server after enabling the plugin
    plugin.enable()
    topology.standalone.restart()
    topology.logcap.flush()

    return topology


def test_set_update_delay(topology):
    args = FakeArgs()

    args.value = 60
    referint_cli.manage_update_delay(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains('referint-update-delay set to "60"')
    topology.logcap.flush()

    args.value = None
    referint_cli.manage_update_delay(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("referint-update-delay: 60")
    topology.logcap.flush()

    args.value = 0
    referint_cli.manage_update_delay(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains('referint-update-delay set to "0"')
    topology.logcap.flush()

    args.value = None
    referint_cli.manage_update_delay(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("referint-update-delay: 0")
    topology.logcap.flush()

def test_add_membership_attr(topology):
    args = FakeArgs()

    args.value = "member2"
    referint_cli.add_membership_attr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully added membership attribute")
    topology.logcap.flush()

    referint_cli.display_membership_attr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": member2")
    topology.logcap.flush()

def test_add_membership_attr_with_value_that_already_exists(topology):
    plugin = ReferentialIntegrityPlugin(topology.standalone)
    # setup test
    if not "uniqueMember" in plugin.get_membership_attr():
        plugin.add_membership_attr("uniqueMember")

    args = FakeArgs()

    args.value = "uniqueMember"
    referint_cli.add_membership_attr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("already exists")
    topology.logcap.flush()

def test_remove_membership_attr_with_value_that_exists(topology):
    plugin = ReferentialIntegrityPlugin(topology.standalone)
    # setup test
    if not "uniqueMember" in plugin.get_membership_attr():
        plugin.add_membership_attr("uniqueMember")

    args = FakeArgs()

    args.value = "uniqueMember"
    referint_cli.remove_membership_attr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully removed membership attribute")
    topology.logcap.flush()

    referint_cli.display_membership_attr(topology.standalone, None, topology.logcap.log, args)
    assert not topology.logcap.contains(": uniqueMember")
    topology.logcap.flush()

def test_remove_membership_attr_with_value_that_doesnt_exist(topology):
    args = FakeArgs()

    args.value = "whatever"
    referint_cli.remove_membership_attr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains('No value "{0}" found'.format(args.value))
    topology.logcap.flush()

def test_try_remove_all_membership_attr_values(topology):
    plugin = ReferentialIntegrityPlugin(topology.standalone)
    #setup test
    membership_values = plugin.get_membership_attr()
    assert len(membership_values) > 0
    for val in membership_values[:-1]:
        plugin.remove_membership_attr(val)

    args = FakeArgs()

    args.value = membership_values[-1]
    referint_cli.remove_membership_attr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("Error: Failed to delete. At least one value for membership attribute should exist.")
    topology.logcap.flush()
