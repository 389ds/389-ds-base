# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import pytest
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.services import ServiceAccounts
from lib389.idm.domain import Domain
from test389.topologies import topology_i2 as topology
from lib389.backend import Backends
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import ChainingBackendPlugin
from lib389.chaining import ChainingLinks
from lib389.mappingTree import MappingTrees

PW = 'Secret123'

pytestmark = pytest.mark.tier2


def test_chaining_bind_no_pw_encoding_warning(topology):
    """Verify binding as remote user through chaining does not log
    a warning about missing password attribute

    :id: 81656cd4-5537-47df-9cb1-c3cc3c653aa6
    :setup: Two standalone instances in chaining
    :steps:
        1. Configure chaining from st1 to st2 with a proxy user
        2. Create a test user on st2
        3. Enable password hash upgrade on st1
        4. Bind as the remote user through st1
        5. Check st1 error log
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. No "Could not read password attribute" warning in error log
    """

    st1 = topology.ins["standalone1"]
    st2 = topology.ins["standalone2"]

    # Create a proxy user on st2 for the chaining link
    sproxy = ServiceAccounts(st2, DEFAULT_SUFFIX).create(properties={
        'cn': 'proxy',
        'userPassword': PW
    })

    # Add read and proxy ACIs on st2
    dc = Domain(st2, DEFAULT_SUFFIX)
    dc.add('aci',
        f'(targetattr="*")(version 3.0; acl "Enable proxy access"; '
        f'allow (proxy)(userdn="ldap:///{sproxy.dn}");)')
    dc.add('aci',
        '(targetattr="*")(version 3.0; acl "Enable read access"; '
        'allow (read, search, compare)(userdn="ldap:///anyone");)')

    # Create the test user on st2
    users = UserAccounts(st2, DEFAULT_SUFFIX)
    test_user = users.create_test_user(uid=2000)
    test_user.set('userPassword', PW)

    # Clear all local backends on st1
    for be in Backends(st1).list():
        be.delete()

    # Enable the chaining backend plugin on st1
    ChainingBackendPlugin(st1).enable()

    # Create the chaining link st1 -> st2
    ChainingLinks(st1).create(properties={
        'cn': 'demochain',
        'nsfarmserverurl': st2.toLDAPURL(),
        'nsslapd-suffix': DEFAULT_SUFFIX,
        'nsmultiplexorbinddn': sproxy.dn,
        'nsmultiplexorcredentials': PW,
        'nsCheckLocalACI': 'on',
    })

    # Recreate the mapping tree to point at the chaining backend
    mts = MappingTrees(st1)
    for mt in mts.list():
        mt.delete()
    mts.ensure_state(properties={
        'cn': DEFAULT_SUFFIX,
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'demochain',
        'nsslapd-distribution-plugin': 'libreplication-plugin',
        'nsslapd-distribution-funct': 'repl_chain_on_update',
    })

    # Enable password hash upgrade — this triggers update_pw_encoding()
    # on every successful bind
    st1.config.set('nsslapd-enable-upgrade-hash', 'on')
    st1.config.set('passwordIsGlobalPolicy', 'on')
    st2.config.set('passwordIsGlobalPolicy', 'on')

    st1.restart()

    # Bind as the remote user through the chaining frontend
    remote_user = UserAccount(st1, test_user.dn)
    conn = remote_user.bind(password=PW)
    assert conn

    # No warning should appear in st1's error log
    assert not st1.searchErrorsLog("Could not read password attribute")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
