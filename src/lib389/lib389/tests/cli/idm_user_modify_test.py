# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
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
from lib389.cli_idm.user import create, modify

from lib389.cli_base import LogCapture, FakeArgs
from lib389.tests.cli import topology

from lib389.utils import ds_is_older
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")

# Topology is pulled from __init__.py
def test_user_modify(topology):
    be_args = FakeArgs()

    be_args.be_name = 'userRoot'
    be_args.suffix = DEFAULT_SUFFIX
    be_args.parent_suffix = None
    be_args.create_entries = False
    backend_create(topology.standalone, None, topology.logcap.log, be_args)

    # And add the skeleton objects.
    init_args = FakeArgs()
    init_args.version = INSTALL_LATEST_CONFIG
    initialise(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, init_args)

    # Check that our modify parser works. Modify statements are such as:
    # "add:attr:value". Replace is the exception as "replace:attr:old:new"

    # Check bad syntax
    modify_args = FakeArgs()
    modify_args.selector = "demo_user"
    modify_args.changes = ["tnaohtnsuahtnsouhtns"]

    with pytest.raises(ValueError):
        modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    modify_args.changes = ["add:attr:"]
    with pytest.raises(ValueError):
        modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    modify_args.changes = ["add:attr"]
    with pytest.raises(ValueError):
        modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    modify_args.changes = ["replace::"]
    with pytest.raises(ValueError):
        modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    modify_args.changes = ["replace:attr::new"]
    with pytest.raises(ValueError):
        modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    modify_args.changes = ["delete:attr:old:new"]
    with pytest.raises(ValueError):
        modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    # Check that even a single bad value causes error
    modify_args.changes = ["add:description:goodvalue", "add:attr:"]
    with pytest.raises(ValueError):
        modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    # check good syntax
    modify_args.changes = ["add:description:testvalue"]
    modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    modify_args.changes = ["replace:description:newvalue"]
    modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    modify_args.changes = ["delete:description:newvalue"]
    modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    modify_args.changes = ["add:description:testvalue"]
    modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    modify_args.changes = ["delete:description:"]
    modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

    # check mixed type, with multiple actions

    modify_args.changes = ["add:objectclass:nsMemberOf", "add:description:anothervalue"]
    modify(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, modify_args)

