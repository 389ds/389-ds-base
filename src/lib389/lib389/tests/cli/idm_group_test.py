# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest
import ldap

from lib389._constants import DEFAULT_SUFFIX, INSTALL_LATEST_CONFIG

from lib389.cli_conf.backend import backend_create
from lib389.cli_idm.initialise import initialise
from lib389.cli_idm.group import get, create, delete, members, add_member, remove_member
from lib389.cli_idm.user import create as create_user

from lib389.cli_base import LogCapture, FakeArgs
from lib389.tests.cli import topology_be_latest as topology

from lib389.utils import ds_is_older
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")

# Topology is pulled from __init__.py
def test_group_tasks(topology):
    # First check that our test group isn't there:
    topology.logcap.flush()
    g_args = FakeArgs()
    g_args.selector = 'testgroup'
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        get(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, g_args)

    # Create a group
    topology.logcap.flush()
    g_args.cn = 'testgroup'
    create(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, g_args)
    assert(topology.logcap.contains("Successfully created testgroup"))

    # Assert it exists
    topology.logcap.flush()
    g_args = FakeArgs()
    g_args.selector = 'testgroup'
    get(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, g_args)
    assert(topology.logcap.contains("dn: cn=testgroup,ou=groups,dc=example,dc=com"))

    # Add a user
    topology.logcap.flush()
    u_args = FakeArgs()
    u_args.uid = 'testuser'
    u_args.cn = 'Test User'
    u_args.displayName = 'Test User'
    u_args.homeDirectory = '/home/testuser'
    u_args.uidNumber = '5000'
    u_args.gidNumber = '5000'
    create_user(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, u_args)
    assert(topology.logcap.contains("Successfully created testuser"))

    # Add them to the group as a member
    topology.logcap.flush()
    g_args.cn = "testgroup"
    g_args.dn = "uid=testuser,ou=people,dc=example,dc=com"
    add_member(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, g_args)
    assert(topology.logcap.contains("added member"))

    # Check they are a member
    topology.logcap.flush()
    g_args.cn = "testgroup"
    members(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, g_args)
    assert(topology.logcap.contains("uid=testuser,ou=people,dc=example,dc=com"))

    # Remove them from the group
    topology.logcap.flush()
    g_args.cn = "testgroup"
    g_args.dn = "uid=testuser,ou=people,dc=example,dc=com"
    remove_member(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, g_args)
    assert(topology.logcap.contains("removed member"))

    # Check they are not a member
    topology.logcap.flush()
    g_args.cn = "testgroup"
    members(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, g_args)
    assert(topology.logcap.contains("No members to display"))

    # Delete the group
    topology.logcap.flush()
    g_args.dn = "cn=testgroup,ou=groups,dc=example,dc=com"
    delete(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, g_args, warn=False)
    assert(topology.logcap.contains("Successfully deleted cn=testgroup,ou=groups,dc=example,dc=com"))
