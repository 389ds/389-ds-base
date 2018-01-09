# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
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
from lib389.cli_idm.user import get, create, delete, status, lock, unlock

from lib389.cli_base import LogCapture, FakeArgs
from lib389.tests.cli import topology

from lib389.utils import ds_is_older
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")

# Topology is pulled from __init__.py
def test_user_tasks(topology):
    # 
    be_args = FakeArgs()

    be_args.cn = 'userRoot'
    be_args.nsslapd_suffix = DEFAULT_SUFFIX
    backend_create(topology.standalone, None, topology.logcap.log, be_args)

    # And add the skeleton objects.
    init_args = FakeArgs()
    init_args.version = INSTALL_LATEST_CONFIG
    initialise(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, init_args)

    # First check that our test user isn't there:
    topology.logcap.flush()
    u_args = FakeArgs()
    u_args.selector = 'testuser'
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        get(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, u_args)

    # Create the user
    topology.logcap.flush()
    u_args.cn = 'testuser'
    u_args.uid = 'testuser'
    u_args.sn = 'testuser'
    u_args.homeDirectory = '/home/testuser'
    u_args.uidNumber = '5000'
    u_args.gidNumber = '5000'
    create(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, u_args)

    assert(topology.logcap.contains("Sucessfully created testuser"))
    # Assert they exist
    topology.logcap.flush()
    get(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, u_args)
    assert(topology.logcap.contains('dn: uid=testuser,ou=people,dc=example,dc=com'))

    # Reset the password

    # Lock the account, check status
    topology.logcap.flush()
    lock(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, u_args)
    assert(topology.logcap.contains('locked'))

    topology.logcap.flush()
    status(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, u_args)
    assert(topology.logcap.contains('locked: True'))

    # Unlock check status
    topology.logcap.flush()
    unlock(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, u_args)
    assert(topology.logcap.contains('unlocked'))

    topology.logcap.flush()
    status(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, u_args)
    assert(topology.logcap.contains('locked: False'))

    # Enroll a dummy cert

    # Enroll a dummy sshkey

    # Delete it 
    topology.logcap.flush()
    u_args.dn = 'uid=testuser,ou=people,dc=example,dc=com'
    delete(topology.standalone, DEFAULT_SUFFIX, topology.logcap.log, u_args, warn=False)
    assert(topology.logcap.contains('Sucessfully deleted uid=testuser,ou=people,dc=example,dc=com'))

