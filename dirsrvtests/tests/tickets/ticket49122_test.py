import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st as topo

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

USER_DN = 'uid=user,' + DEFAULT_SUFFIX
ROLE_DN = 'cn=Filtered_Role_That_Includes_Empty_Role,' + DEFAULT_SUFFIX


def test_ticket49122(topo):
    """Search for non-existant role and make sure the server does not crash
    """

    # Enable roles plugin
    topo.standalone.plugins.enable(name=PLUGIN_ROLES)
    topo.standalone.restart()

    # Add invalid role
    try:
        topo.standalone.add_s(Entry((
            ROLE_DN, {'objectclass': ['top', 'ldapsubentry', 'nsroledefinition',
                                      'nscomplexroledefinition', 'nsfilteredroledefinition'],
                      'cn': 'Filtered_Role_That_Includes_Empty_Role',
                      'nsRoleFilter': '(!(nsrole=cn=This_Is_An_Empty_Managed_NsRoleDefinition,dc=example,dc=com))',
                      'description': 'A filtered role with filter that will crash the server'})))
    except ldap.LDAPError as e:
        topo.standalone.log.fatal('Failed to add filtered role: error ' + e.message['desc'])
        assert False

    # Add test user
    try:
        topo.standalone.add_s(Entry((
            USER_DN, {'objectclass': "top extensibleObject".split(),
                      'uid': 'user'})))
    except ldap.LDAPError as e:
        topo.standalone.log.fatal('Failed to add test user: error ' + str(e))
        assert False

    if DEBUGGING:
        # Add debugging steps(if any)...
        print "Attach gdb"
        time.sleep(20)

    # Search for the role
    try:
        topo.standalone.search_s(USER_DN, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nsrole'])
    except ldap.LDAPError as e:
        topo.standalone.log.fatal('Search failed: error ' + str(e))
        assert False

    topo.standalone.log.info('Test Passed')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

