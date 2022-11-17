# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import ldap
import logging
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

USER_DN = 'uid=user,' + DEFAULT_SUFFIX
ROLE_DN = 'cn=Filtered_Role_That_Includes_Empty_Role,' + DEFAULT_SUFFIX
filters = ['nsrole=cn=empty,dc=example,dc=com',
           '(nsrole=cn=empty,dc=example,dc=com)',
           '(&(nsrole=cn=empty,dc=example,dc=com))',
           '(!(nsrole=cn=empty,dc=example,dc=com))',
           '(&(|(objectclass=person)(sn=app*))(userpassword=*))',
           '(&(|(objectclass=person)(nsrole=cn=empty,dc=example,dc=com))(userpassword=*))',
           '(&(|(nsrole=cn=empty,dc=example,dc=com)(sn=app*))(userpassword=*))',
           '(&(|(objectclass=person)(sn=app*))(nsrole=cn=empty,dc=example,dc=com))',
           '(&(|(&(cn=*)(objectclass=person)(nsrole=cn=empty,dc=example,dc=com)))(uid=*))']


def test_ticket49122(topo):
    """Search for non-existant role and make sure the server does not crash
    """

    # Enable roles plugin
    topo.standalone.plugins.enable(name=PLUGIN_ROLES)
    topo.standalone.restart()

    # Add test user
    try:
        topo.standalone.add_s(Entry((
            USER_DN, {'objectclass': "top extensibleObject".split(),
                      'uid': 'user'})))
    except ldap.LDAPError as e:
        topo.standalone.log.fatal('Failed to add test user: error ' + str(e))
        assert False

    if DEBUGGING:
        print("Attach gdb")
        time.sleep(20)

    # Loop over filters
    for role_filter in filters:
        log.info('Testing filter: ' + role_filter)

        # Add invalid role
        try:
            topo.standalone.add_s(Entry((
                ROLE_DN, {'objectclass': ['top', 'ldapsubentry', 'nsroledefinition',
                                          'nscomplexroledefinition', 'nsfilteredroledefinition'],
                          'cn': 'Filtered_Role_That_Includes_Empty_Role',
                          'nsRoleFilter': role_filter,
                          'description': 'A filtered role with filter that will crash the server'})))
        except ldap.LDAPError as e:
            topo.standalone.log.fatal('Failed to add filtered role: error ' + e.message['desc'])
            assert False

        # Search for the role
        try:
            topo.standalone.search_s(USER_DN, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nsrole'])
        except ldap.LDAPError as e:
            topo.standalone.log.fatal('Search failed: error ' + str(e))
            assert False

        # Cleanup
        try:
            topo.standalone.delete_s(ROLE_DN)
        except ldap.LDAPError as e:
            topo.standalone.log.fatal('delete failed: error ' + str(e))
            assert False
        time.sleep(1)

    topo.standalone.log.info('Test Passed')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

