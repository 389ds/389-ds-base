# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import pytest
import time
import shutil
from lib389.idm.account import Accounts, Account
from lib389.topologies import topology_i2 as topology
from lib389.backend import Backends
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import ChainingBackendPlugin
from lib389.chaining import ChainingLinks
from lib389.mappingTree import MappingTrees

pytestmark = pytest.mark.tier1

def test_chaining_paged_search(topology):
    """ Test paged search through the chaining db. This
    would cause a SIGSEGV with paged search which could
    be triggered by SSSD.

    :id: 7b29b1f5-26cf-49fa-9fe7-ee29a1408633
    :setup: Two standalones in chaining.
    :steps:
        1. Configure chaining between the nodes
        2. Do a chaining search (no page) to assert it works
        3. Do a paged search through chaining.

    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    st1 = topology.ins["standalone1"]
    st2 = topology.ins["standalone2"]

    ### We setup so that st1 -> st2

    # Clear all the BE in st1
    bes1 = Backends(st1)
    for be in bes1.list():
        be.delete()

    # Setup st1 to chain to st2
    chain_plugin_1 = ChainingBackendPlugin(st1)
    chain_plugin_1.enable()

    chains = ChainingLinks(st1)
    chain = chains.create(properties={
        'cn': 'demochain',
        'nsslapd-suffix': DEFAULT_SUFFIX,
        'nsmultiplexorbinddn': '',
        'nsmultiplexorcredentials': '',
        'nsfarmserverurl': st2.toLDAPURL(),
    })

    mts = MappingTrees(st1)
    # Due to a bug in lib389, we need to delete and recreate the mt.
    for mt in mts.list():
        mt.delete()
    mts.ensure_state(properties={
        'cn': DEFAULT_SUFFIX,
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'demochain',
    })
    # Restart to enable
    st1.restart()

    # Get an anonymous connection.
    anon = Account(st1, dn='')
    anon_conn = anon.bind(password='')

    # Now do a search from st1 -> st2
    accs_1 = Accounts(anon_conn, DEFAULT_SUFFIX)
    assert len(accs_1.list()) > 0

    # Allow time to attach lldb if needed.
    # import time
    # print("🔥🔥🔥")
    # time.sleep(45)

    # Now do a *paged* search from st1 -> st2
    assert len(accs_1.list(paged_search=2, paged_critical=False)) > 0


