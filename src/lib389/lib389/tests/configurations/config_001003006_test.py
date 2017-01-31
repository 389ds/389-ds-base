# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap

import logging
import sys
import time

import pytest

from lib389 import DirSrv
from lib389._constants import *
from lib389.properties import *

DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

### WARNING:
# We can't use topology here, as we need to force python install!

REQUIRED_DNS = [
    'dc=example,dc=com',
    'ou=Groups,dc=example,dc=com',
    'ou=People,dc=example,dc=com',
    'ou=Special Users,dc=example,dc=com',
    'cn=Accounting Managers,ou=Groups,dc=example,dc=com',
    'cn=HR Managers,ou=Groups,dc=example,dc=com',
    'cn=QA Managers,ou=Groups,dc=example,dc=com',
    'cn=PD Managers,ou=Groups,dc=example,dc=com',
    'cn=Directory Administrators,dc=example,dc=com',
]

class TopologyMain(object):
    def __init__(self, standalones=None, masters=None,
                 consumers=None, hubs=None):
        if standalones:
            if isinstance(standalones, dict):
                self.ins = standalones
            else:
                self.standalone = standalones
        if masters:
            self.ms = masters
        if consumers:
            self.cs = consumers
        if hubs:
            self.hs = hubs

@pytest.fixture(scope="module")
def topology_st(request):
    """Create DS standalone instance"""

    if DEBUGGING:
        standalone = DirSrv(verbose=True)
    else:
        standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE1
    args_instance[SER_PORT] = PORT_STANDALONE1
    # args_instance[SER_SECURE_PORT] = SECUREPORT_STANDALONE1
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE1
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create(pyinstall=True, version=INSTALL_LATEST_CONFIG)
    standalone.open()

    def fin():
        if DEBUGGING:
            standalone.stop()
        else:
            standalone.delete()

    request.addfinalizer(fin)

    return TopologyMain(standalones=standalone)

def test_install_sample_entries(topology_st):
    # Assert that our entries match.

    entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE)

    for entry in entries:
        assert(entry.dn in REQUIRED_DNS)
        # We can make this assert the full object content, plugins and more later.

    assert(True)
