import os
import sys
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

DEBUGGING = False

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)


log = logging.getLogger(__name__)

USER1 = 'user1'
USER1_DOMAIN = 'user1@example.com'
PW = 'password'
USER1_DN = 'uid=user1,ou=People,%s' % DEFAULT_SUFFIX
USER1_CONFLICT_DN = 'uid=user1,%s' % DEFAULT_SUFFIX


class TopologyStandalone(object):
    """The DS Topology Class"""
    def __init__(self, standalone):
        """Init"""
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    """Create DS Deployment"""

    # Creating standalone instance ...
    if DEBUGGING:
        standalone = DirSrv(verbose=True)
    else:
        standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        """If we are debugging just stop the instances, otherwise remove
        them
        """
        if DEBUGGING:
            standalone.stop(60)
        else:
            standalone.delete()

    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)

def _create_user(inst, name, dn):
    inst.add_s(Entry((
                dn, {
                    'objectClass': 'top account simplesecurityobject'.split(),
                     'uid': name,
                     'userpassword': PW
                })))

def _bind(name, cred):
    # Returns true or false if it worked.
    if DEBUGGING:
        print('test 48272 BINDING AS %s:%s' % (name, cred))
    status = True
    conn = ldap.initialize("ldap://%s:%s" % (HOST_STANDALONE, PORT_STANDALONE))
    try:
        conn.simple_bind_s(name, cred)
        conn.unbind_s()
    except ldap.NO_SUCH_OBJECT:
        status = False
    return status


def test_ticket48272(topology):
    """
    Test the functionality of the addn bind plugin. This should allow users
    of the type "name" or "name@domain.com" to bind.
    """

    # There will be a better way to do this in the future.
    topology.standalone.add_s(Entry((
        "cn=addn,cn=plugins,cn=config", {
            "objectClass" : "top nsSlapdPlugin extensibleObject".split(),
            "cn" : "addn",
            "nsslapd-pluginPath": "libaddn-plugin",
            "nsslapd-pluginInitfunc": "addn_init",
            "nsslapd-pluginType": "preoperation",
            "nsslapd-pluginEnabled": "on",
            "nsslapd-pluginId": "addn",
            "nsslapd-pluginVendor": "389 Project",
            "nsslapd-pluginVersion": "1.3.6.0",
            "nsslapd-pluginDescription": "Allow AD DN style bind names to LDAP",
            "addn_default_domain": "example.com",
        }
    )))

    topology.standalone.add_s(Entry((
        "cn=example.com,cn=addn,cn=plugins,cn=config", {
            "objectClass" : "top extensibleObject".split(),
            "cn" : "example.com",
            "addn_base": "ou=People,%s" % DEFAULT_SUFFIX,
            "addn_filter": "(&(objectClass=account)(uid=%s))",
        }
    )))

    topology.standalone.restart(60)

    # Add a user
    _create_user(topology.standalone, USER1, USER1_DN)

    # Make sure our binds still work.
    assert(_bind(USER1_DN, PW))
    # Test an anonymous bind
    for i in range(0,10):

        # Test bind as name
        assert(_bind(USER1, PW))

        # Make sure that name@fakedom fails
        assert(_bind(USER1_DOMAIN, PW))

    # Add a conflicting user to an alternate subtree
    _create_user(topology.standalone, USER1, USER1_CONFLICT_DN)
    # Change the plugin to search from the rootdn instead
    # This means we have a conflicting user in scope now!

    topology.standalone.modify_s("cn=example.com,cn=addn,cn=plugins,cn=config", [(ldap.MOD_REPLACE, 'addn_base', DEFAULT_SUFFIX)])
    topology.standalone.restart(60)

    # Make sure our binds still work.
    assert(_bind(USER1_DN, PW))
    assert(_bind(USER1_CONFLICT_DN, PW))
    for i in range(0,10):

        # Test bind as name fails
        try:
            _bind(USER1, PW)
            assert(False)
        except:
            pass
        # Test bind as name@domain fails too
        try:
            _bind(USER1_DOMAIN, PW)
            assert(False)
        except:
            pass

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

