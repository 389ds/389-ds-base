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
from lib389.plugins import RootDNAccessControlPlugin
from lib389.cli_conf.plugins import rootdn_ac as rootdn_cli


@pytest.fixture(scope="module")
def topology(request):
    topology = default_topology(request)

    plugin = RootDNAccessControlPlugin(topology.standalone)
    if not plugin.exists():
        plugin.create()

    # we need to restart the server after enabling the plugin
    plugin.enable()
    topology.standalone.restart()
    topology.logcap.flush()

    return topology

def test_set_open_time(topology):
    args = FakeArgs()

    args.value = "1030"
    rootdn_cli.set_open_time(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-open-time set to")
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_time(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-open-time: 1030")
    topology.logcap.flush()

def test_set_close_time(topology):
    args = FakeArgs()

    args.value = "1545"
    rootdn_cli.set_close_time(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-close-time set to")
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_time(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-close-time: 1545")
    topology.logcap.flush()

def test_clear_time(topology):
    args = FakeArgs()

    rootdn_cli.clear_time(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("time-based policy was cleared")
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_time(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-open-time is not set")
    assert topology.logcap.contains("rootdn-close-time is not set")
    topology.logcap.flush()

def test_allow_ip(topology):
    args = FakeArgs()

    args.value = "127.0.0.1"
    rootdn_cli.allow_ip(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-allow-ip".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_ips(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-allow-ip: 127.0.0.1")
    topology.logcap.flush()

def test_deny_ip(topology):
    args = FakeArgs()

    args.value = "127.0.0.2"
    rootdn_cli.deny_ip(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-deny-ip".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_ips(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-deny-ip: 127.0.0.2")
    topology.logcap.flush()

def test_when_ip_is_allowed_its_not_denied(topology):
    args = FakeArgs()

    args.value = "127.0.0.3"
    rootdn_cli.deny_ip(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-deny-ip".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_ips(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-deny-ip: 127.0.0.3")
    topology.logcap.flush()

    args.value = "127.0.0.3"
    rootdn_cli.allow_ip(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-allow-ip".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_ips(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-allow-ip: 127.0.0.3")
    assert not topology.logcap.contains("rootdn-deny-ip: 127.0.0.3")
    topology.logcap.flush()

def test_when_ip_is_denied_its_not_allowed(topology):
    args = FakeArgs()

    args.value = "127.0.0.4"
    rootdn_cli.allow_ip(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-allow-ip".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_ips(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-allow-ip: 127.0.0.4")
    topology.logcap.flush()

    args.value = "127.0.0.4"
    rootdn_cli.deny_ip(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-deny-ip".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_ips(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-deny-ip: 127.0.0.4")
    assert not topology.logcap.contains("rootdn-allow-ip: 127.0.0.4")
    topology.logcap.flush()

def test_clear_ips(topology):
    args = FakeArgs()

    rootdn_cli.clear_all_ips(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("ip-based policy was cleared")
    topology.logcap.flush()

def test_allow_host(topology):
    args = FakeArgs()

    args.value = "example1.com"
    rootdn_cli.allow_host(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-allow-host".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_hosts(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-allow-host: example1.com")
    topology.logcap.flush()

def test_deny_host(topology):
    args = FakeArgs()

    args.value = "example2.com"
    rootdn_cli.deny_host(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-deny-host".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_hosts(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-deny-host: example2.com")
    topology.logcap.flush()

def test_when_host_is_allowed_its_not_denied(topology):
    args = FakeArgs()

    args.value = "example3.com"
    rootdn_cli.deny_host(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-deny-host".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_hosts(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-deny-host: example3.com")
    topology.logcap.flush()

    args.value = "example3.com"
    rootdn_cli.allow_host(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-allow-host".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_hosts(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-allow-host: example3.com")
    assert not topology.logcap.contains("rootdn-deny-host: example3.com")
    topology.logcap.flush()

def test_when_host_is_denied_its_not_allowed(topology):
    args = FakeArgs()

    args.value = "example4.com"
    rootdn_cli.allow_host(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-allow-host".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_hosts(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-allow-host: example4.com")
    topology.logcap.flush()

    args.value = "example4.com"
    rootdn_cli.deny_host(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-deny-host".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_hosts(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-deny-host: example4.com")
    assert not topology.logcap.contains("rootdn-allow-host: example4.com")
    topology.logcap.flush()

def test_clear_hosts(topology):
    args = FakeArgs()

    rootdn_cli.clear_all_hosts(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("host-based policy was cleared")
    topology.logcap.flush()

def test_allow_and_deny_days(topology):
    args = FakeArgs()

    args.value = "Mon".capitalize()
    rootdn_cli.allow_day(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("{} added to rootdn-days-allowed".format(args.value))
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_days(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-days-allowed: Mon")
    topology.logcap.flush()

    args.value = "friday".capitalize()
    rootdn_cli.allow_day(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("Fri added to rootdn-days-allowed")
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_days(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-days-allowed: Mon, Fri")
    topology.logcap.flush()

    args.value = "MONDAY".capitalize()
    rootdn_cli.deny_day(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("Mon removed from rootdn-days-allowed")
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_days(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("rootdn-days-allowed: Fri")
    topology.logcap.flush()

    args.value = "fri".capitalize()
    rootdn_cli.deny_day(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("Fri removed from rootdn-days-allowed")
    topology.logcap.flush()

    args.value = None
    rootdn_cli.display_days(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("No day-based access control policy has been configured")
    topology.logcap.flush()

def test_clear_days(topology):
    args = FakeArgs()

    rootdn_cli.clear_all_days(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("day-based policy was cleared")
    topology.logcap.flush()
