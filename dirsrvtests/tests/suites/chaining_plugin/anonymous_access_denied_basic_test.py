# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 William Brown <william@blackhats.net.au>
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
from lib389.idm.services import ServiceAccounts, ServiceAccount
from lib389.idm.domain import Domain

PW = 'thnaoehtnuaoenhtuaoehtnu'

pytestmark = pytest.mark.tier1

def test_chaining_paged_search(topology):
    """ Check that when the chaining target has anonymous access
    disabled that the ping still functions and allows the search
    to continue with an appropriate bind user.

    :id: 00bf31db-d93b-4224-8e70-86abb2d4cd17
    :setup: Two standalones in chaining.
    :steps:
        1. Configure chaining between the nodes
        2. Do a chaining search (w anon allow) to assert it works
        3. Configure anon dis allowed on st2
        4. Restart both
        5. Check search still works

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    st1 = topology.ins["standalone1"]
    st2 = topology.ins["standalone2"]

    ### We setup so that st1 -> st2

    # Setup a chaining user on st2 to authenticate to.
    sa = ServiceAccounts(st2, DEFAULT_SUFFIX).create(properties = {
        'cn': 'sa',
        'userPassword': PW
    })

    # Add a proxy user.
    sproxy = ServiceAccounts(st2, DEFAULT_SUFFIX).create(properties = {
        'cn': 'proxy',
        'userPassword': PW
    })

    # Add the read and proxy ACI
    dc = Domain(st2, DEFAULT_SUFFIX)
    dc.add('aci',
        f"""(targetattr="objectClass || cn || uid")(version 3.0; acl "Enable sa read"; allow (read, search, compare)(userdn="ldap:///{sa.dn}");)"""
    )
    # Add the proxy ACI
    dc.add('aci',
        f"""(targetattr="*")(version 3.0; acl "Enable proxy access"; allow (proxy)(userdn="ldap:///{sproxy.dn}");)"""
    )

    # Clear all the BE in st1
    bes1 = Backends(st1)
    for be in bes1.list():
        be.delete()

    # Setup st1 to chain to st2
    chain_plugin_1 = ChainingBackendPlugin(st1)
    chain_plugin_1.enable()

    # Chain with the proxy user.
    chains = ChainingLinks(st1)
    chain = chains.create(properties={
        'cn': 'demochain',
        'nsfarmserverurl': st2.toLDAPURL(),
        'nsslapd-suffix': DEFAULT_SUFFIX,
        'nsmultiplexorbinddn': sproxy.dn,
        'nsmultiplexorcredentials': PW,
        'nsCheckLocalACI': 'on',
        'nsConnectionLife': '30',
    })

    mts = MappingTrees(st1)
    # Due to a bug in lib389, we need to delete and recreate the mt.
    for mt in mts.list():
        mt.delete()
    mts.ensure_state(properties={
        'cn': DEFAULT_SUFFIX,
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'demochain',
        'nsslapd-distribution-plugin': 'libreplication-plugin',
        'nsslapd-distribution-funct': 'repl_chain_on_update',
    })

    # Enable pwpolicy (Not sure if part of the issue).
    st1.config.set('passwordIsGlobalPolicy', 'on')
    st2.config.set('passwordIsGlobalPolicy', 'on')

    # Restart to enable everything.
    st1.restart()

    # Get a proxy auth connection.
    sa1 = ServiceAccount(st1, sa.dn)
    sa1_conn = sa1.bind(password=PW)

    # Now do a search from st1 -> st2
    sa1_dc = Domain(sa1_conn, DEFAULT_SUFFIX)
    assert sa1_dc.exists()

    # Now on st2 disable anonymous access.
    st2.config.set('nsslapd-allow-anonymous-access', 'rootdse')

    # Stop st2 to force the connection to be dead.
    st2.stop()
    # Restart st1 - this means it must re-do the ping/keepalive.
    st1.restart()

    # do a bind - this should fail, and forces the conn offline.
    with pytest.raises(ldap.OPERATIONS_ERROR):
        sa1.bind(password=PW)

    # Allow time to attach lldb if needed.
    # print("🔥🔥🔥")
    # time.sleep(45)

    # Bring st2 online.
    st2.start()

    # Wait a bit
    time.sleep(5)

    # Get a proxy auth connection (again)
    sa1_conn = sa1.bind(password=PW)
    # Now do a search from st1 -> st2
    sa1_dc = Domain(sa1_conn, DEFAULT_SUFFIX)
    assert sa1_dc.exists()
