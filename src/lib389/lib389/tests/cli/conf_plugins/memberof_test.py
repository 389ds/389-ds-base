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
from lib389.plugins import MemberOfPlugin
from lib389.cli_conf.plugins import memberof as memberof_cli


@pytest.fixture(scope="module")
def topology(request):
    topology = default_topology(request)

    plugin = MemberOfPlugin(topology.standalone)
    if not plugin.exists():
        plugin.create()

    # At the moment memberof plugin needs to be enabled in order to perform
    # syntax checking. Additionally, we have to restart the server in order
    # for the action of enabling the plugin to take effect.
    plugin.enable()
    topology.standalone.restart()
    topology.logcap.flush()

    return topology


def test_set_attr_with_legal_value(topology):
    args = FakeArgs()

    args.value = "memberOf"
    memberof_cli.manage_attr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("memberOfAttr set to")
    topology.logcap.flush()

    args.value = None
    memberof_cli.manage_attr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": memberOf")
    topology.logcap.flush()

def test_set_attr_with_illegal_value(topology):
    args = FakeArgs()

    args.value = "whatever"
    memberof_cli.manage_attr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("Failed to set")
    topology.logcap.flush()

def test_set_groupattr_with_legal_value(topology):
    args = FakeArgs()

    args.value = "uniquemember"
    memberof_cli.add_groupattr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully added memberOfGroupAttr value")
    topology.logcap.flush()

    memberof_cli.display_groupattr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": uniquemember")
    topology.logcap.flush()

def test_set_groupattr_with_value_that_already_exists(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not "uniquemember" in plugin.get_groupattr():
        plugin.add_groupattr("uniquemember")

    args = FakeArgs()

    args.value = "uniquemember"
    memberof_cli.add_groupattr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("already exists")
    topology.logcap.flush()

def test_set_groupattr_with_illegal_value(topology):
    args = FakeArgs()

    args.value = "whatever"
    memberof_cli.add_groupattr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("Illegal value")
    topology.logcap.flush()

def test_remove_groupattr_with_value_that_exists(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not "uniquemember" in plugin.get_groupattr():
        plugin.add_groupattr("uniquemember")

    args = FakeArgs()

    args.value = "uniquemember"
    memberof_cli.remove_groupattr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully removed memberOfGroupAttr value")
    topology.logcap.flush()

    memberof_cli.display_groupattr(topology.standalone, None, topology.logcap.log, args)
    assert not topology.logcap.contains(": uniquemember")
    topology.logcap.flush()

def test_remove_groupattr_with_value_that_doesnt_exist(topology):
    args = FakeArgs()

    args.value = "whatever"
    memberof_cli.remove_groupattr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains('No value "{0}" found'.format(args.value))
    topology.logcap.flush()

def test_try_remove_all_groupattr_values(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # make sure "member" value exists and it is the only one
    assert "member" in plugin.get_groupattr() # exists from default
    assert len(plugin.get_groupattr()) == 1

    args = FakeArgs()

    args.value = "member"
    memberof_cli.remove_groupattr(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("Error: Failed to delete. memberOfGroupAttr is required.")
    topology.logcap.flush()

def test_get_allbackends_when_not_set(topology):
    args = FakeArgs()

    memberof_cli.display_allbackends(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("memberOfAllBackends is not set")
    topology.logcap.flush()

def test_enable_allbackends(topology):
    args = FakeArgs()

    memberof_cli.enable_allbackends(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("enabled successfully")
    topology.logcap.flush()

    memberof_cli.display_allbackends(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": on")
    topology.logcap.flush()

def test_disable_all_backends(topology):
    args = FakeArgs()

    memberof_cli.disable_allbackends(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("disabled successfully")
    topology.logcap.flush()

    memberof_cli.display_allbackends(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": off")
    topology.logcap.flush()

def test_get_skipnested_when_not_set(topology):
    args = FakeArgs()

    memberof_cli.display_skipnested(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("memberOfSkipNested is not set")
    topology.logcap.flush()

def test_set_skipnested(topology):
    args = FakeArgs()

    memberof_cli.enable_skipnested(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("set successfully")
    topology.logcap.flush()

    memberof_cli.display_skipnested(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": on")
    topology.logcap.flush()

def test_unset_skipnested(topology):
    args = FakeArgs()

    memberof_cli.disable_skipnested(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("unset successfully")
    topology.logcap.flush()

    memberof_cli.display_skipnested(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": off")
    topology.logcap.flush()

def test_set_autoaddoc(topology):
    args = FakeArgs()

    # argparse makes sure that only choices 'nsmemberof', 'inetuser', 'inetadmin', 'del' are allowed
    args.value = "nsmemberof"
    memberof_cli.manage_autoaddoc(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("memberOfAutoAddOc set to")
    topology.logcap.flush()

    args.value = None
    memberof_cli.manage_autoaddoc(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": nsMemberOf")
    topology.logcap.flush()

def test_remove_autoaddoc(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not plugin.get_autoaddoc():
        plugin.set_autoaddoc("nsmemberof")

    args = FakeArgs()

    # argparse makes sure that only choices 'nsmemberof', 'inetuser', 'inetadmin', 'del' are allowed
    args.value = "del"
    memberof_cli.manage_autoaddoc(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("memberOfAutoAddOc attribute deleted")
    topology.logcap.flush()

    args.value = None
    memberof_cli.manage_autoaddoc(topology.standalone, None, topology.logcap.log, args)
    assert not topology.logcap.contains(": nsMemberOf")
    topology.logcap.flush()

def test_remove_autoaddoc_when_not_set(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if plugin.get_autoaddoc():
        plugin.remove_autoaddoc()

    args = FakeArgs()

    args.value = "del"
    memberof_cli.manage_autoaddoc(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("memberOfAutoAddOc was not set")
    topology.logcap.flush()

def test_get_autoaddoc_when_not_set(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if plugin.get_autoaddoc():
        plugin.remove_autoaddoc()

    args = FakeArgs()

    args.value = None
    memberof_cli.manage_autoaddoc(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("memberOfAutoAddOc is not set")
    topology.logcap.flush()

def test_get_entryscope_when_not_set(topology):
    args = FakeArgs()

    args.value = None
    memberof_cli.display_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("memberOfEntryScope is not set")
    topology.logcap.flush()

def test_add_entryscope_with_legal_value(topology):
    args = FakeArgs()

    args.value = "dc=example,dc=com"
    memberof_cli.add_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully added memberOfEntryScope value")
    topology.logcap.flush()

    args.value = None
    memberof_cli.display_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": dc=example,dc=com")
    topology.logcap.flush()

    args.value = "a=b"
    memberof_cli.add_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully added memberOfEntryScope value")
    topology.logcap.flush()

    args.value = None
    memberof_cli.display_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": dc=example,dc=com")
    assert topology.logcap.contains(": a=b")
    topology.logcap.flush()

def test_add_entryscope_with_illegal_value(topology):
    args = FakeArgs()

    args.value = "whatever"
    memberof_cli.add_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("Error: Invalid DN")
    topology.logcap.flush()

def test_add_entryscope_with_existing_value(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not "dc=example,dc=com" in plugin.get_entryscope():
        plugin.add_entryscope("dc=example,dc=com")

    args = FakeArgs()

    args.value = "dc=example,dc=com"
    memberof_cli.add_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains('Value "{}" already exists'.format(args.value))
    topology.logcap.flush()

def test_remove_entryscope_with_existing_value(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not "a=b" in plugin.get_entryscope():
        plugin.add_entryscope("a=b")
    if not "dc=example,dc=com" in plugin.get_entryscope():
        plugin.add_entryscope("dc=example,dc=com")

    args = FakeArgs()

    args.value = "a=b"
    memberof_cli.remove_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully removed memberOfEntryScope value")
    topology.logcap.flush()

    args.value = None
    memberof_cli.display_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": dc=example,dc=com")
    assert not topology.logcap.contains(": a=b")
    topology.logcap.flush()

def test_remove_entryscope_with_non_existing_value(topology):
    args = FakeArgs()

    args.value = "whatever"
    memberof_cli.remove_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains('No value "{0}" found'.format(args.value))
    topology.logcap.flush()

def test_remove_all_entryscope(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not "dc=example,dc=com" in plugin.get_entryscope():
        plugin.add_entryscope("dc=example,dc=com")
    if not "a=b" in plugin.get_entryscope():
        plugin.add_entryscope("a=b")

    args = FakeArgs()

    args.value = None
    memberof_cli.display_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": a=b")
    assert topology.logcap.contains(": dc=example,dc=com")
    topology.logcap.flush()

    args.value = None
    memberof_cli.remove_all_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully removed all memberOfEntryScope values")
    topology.logcap.flush()

    args.value = None
    memberof_cli.display_scope(topology.standalone, None, topology.logcap.log, args)
    assert not topology.logcap.contains(": a=b")
    assert not topology.logcap.contains(": dc=example,dc=com")
    topology.logcap.flush()

def test_get_excludescope_when_not_set(topology):
    args = FakeArgs()

    args.value = None
    memberof_cli.display_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("memberOfEntryScopeExcludeSubtree is not set")
    topology.logcap.flush()

def test_add_excludescope_with_legal_value(topology):
    args = FakeArgs()

    args.value = "ou=people,dc=example,dc=com"
    memberof_cli.add_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully added memberOfEntryScopeExcludeSubtree value")
    topology.logcap.flush()

    args.value = None
    memberof_cli.display_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": ou=people,dc=example,dc=com")
    topology.logcap.flush()

    args.value = "a=b"
    memberof_cli.add_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully added memberOfEntryScopeExcludeSubtree value")
    topology.logcap.flush()

    args.value = None
    memberof_cli.display_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": ou=people,dc=example,dc=com")
    assert topology.logcap.contains(": a=b")
    topology.logcap.flush()

def test_add_excludescope_with_illegal_value(topology):
    args = FakeArgs()

    args.value = "whatever"
    memberof_cli.add_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("Error: Invalid DN")
    topology.logcap.flush()

def test_add_excludescope_with_existing_value(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not "ou=people,dc=example,dc=com" in plugin.get_excludescope():
        plugin.add_excludescope("ou=people,dc=example,dc=com")

    args = FakeArgs()

    args.value = "ou=people,dc=example,dc=com"
    memberof_cli.add_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains('Value "{}" already exists'.format(args.value))
    topology.logcap.flush()

def test_remove_excludescope_with_existing_value(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not "a=b" in plugin.get_excludescope():
        plugin.add_excludescope("a=b")
    if not "ou=people,dc=example,dc=com" in plugin.get_excludescope():
        plugin.add_excludescope("ou=people,dc=example,dc=com")

    args = FakeArgs()

    args.value = "a=b"
    memberof_cli.remove_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully removed memberOfEntryScopeExcludeSubtree value")
    topology.logcap.flush()

    args.value = None
    memberof_cli.display_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": ou=people,dc=example,dc=com")
    assert not topology.logcap.contains(": a=b")
    topology.logcap.flush()

def test_remove_excludescope_with_non_existing_value(topology):
    args = FakeArgs()

    args.value = "whatever"
    memberof_cli.remove_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains('No value "{0}" found'.format(args.value))
    topology.logcap.flush()

def test_remove_all_excludescope(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not "a=b" in plugin.get_excludescope():
        plugin.add_excludescope("a=b")
    if not "ou=people,dc=example,dc=com" in plugin.get_excludescope():
        plugin.add_excludescope("ou=people,dc=example,dc=com")

    args = FakeArgs()

    args.value = None
    memberof_cli.display_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains(": a=b")
    assert topology.logcap.contains(": ou=people,dc=example,dc=com")
    topology.logcap.flush()

    args.value = None
    memberof_cli.remove_all_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("successfully removed all memberOfEntryScopeExcludeSubtree values")
    topology.logcap.flush()

    args.value = None
    memberof_cli.display_excludescope(topology.standalone, None, topology.logcap.log, args)
    assert not topology.logcap.contains(": a=b")
    assert not topology.logcap.contains(": ou=people,dc=example,dc=com")
    topology.logcap.flush()

def test_add_entryscope_with_value_that_exists_in_excludescope(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not "dc=example,dc=com" in plugin.get_entryscope():
        plugin.add_entryscope("dc=example,dc=com")
    if not "ou=people,dc=example,dc=com" in plugin.get_excludescope():
        plugin.add_excludescope("ou=people,dc=example,dc=com")

    args = FakeArgs()

    args.value = "ou=people,dc=example,dc=com"
    memberof_cli.add_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("is also listed as an exclude suffix")
    topology.logcap.flush()

def test_add_excludescope_with_value_that_exists_in_entryscope(topology):
    plugin = MemberOfPlugin(topology.standalone)
    # setup test
    if not "dc=example,dc=com" in plugin.get_entryscope():
        plugin.add_entryscope("dc=example,dc=com")
    if not "ou=people,dc=example,dc=com" in plugin.get_excludescope():
        plugin.add_excludescope("ou=people,dc=example,dc=com")

    args = FakeArgs()

    args.value = "ou=people,dc=example,dc=com"
    memberof_cli.add_scope(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("is also listed as an exclude suffix")
    topology.logcap.flush()
