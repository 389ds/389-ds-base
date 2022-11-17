# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, HOST_STANDALONE, PORT_STANDALONE

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)
# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")]


USER1 = 'user1'
USER1_DOMAIN = 'user1@example.com'
PW = 'password'
USER1_DN = 'uid=user1,ou=People,%s' % DEFAULT_SUFFIX
USER1_CONFLICT_DN = 'uid=user1,%s' % DEFAULT_SUFFIX


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
    except ldap.INVALID_CREDENTIALS:
        status = False
    return status


def test_ticket48272(topology_st):
    """
    Test the functionality of the addn bind plugin. This should allow users
    of the type "name" or "name@domain.com" to bind.
    """

    # There will be a better way to do this in the future.
    topology_st.standalone.add_s(Entry((
        "cn=addn,cn=plugins,cn=config", {
            "objectClass": "top nsSlapdPlugin extensibleObject".split(),
            "cn": "addn",
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

    topology_st.standalone.add_s(Entry((
        "cn=example.com,cn=addn,cn=plugins,cn=config", {
            "objectClass": "top extensibleObject".split(),
            "cn": "example.com",
            "addn_base": "ou=People,%s" % DEFAULT_SUFFIX,
            "addn_filter": "(&(objectClass=account)(uid=%s))",
        }
    )))

    topology_st.standalone.restart(60)

    # Add a user
    _create_user(topology_st.standalone, USER1, USER1_DN)

    if DEBUGGING is not False:
        print("Attach now")
        time.sleep(20)

    # Make sure our binds still work.
    assert (_bind(USER1_DN, PW))
    # Test an anonymous bind
    for i in range(0, 10):
        # Test bind as name
        assert (_bind(USER1, PW))

        # Make sure that name@fakedom fails
        assert (_bind(USER1_DOMAIN, PW))

    # Add a conflicting user to an alternate subtree
    _create_user(topology_st.standalone, USER1, USER1_CONFLICT_DN)
    # Change the plugin to search from the rootdn instead
    # This means we have a conflicting user in scope now!

    topology_st.standalone.modify_s("cn=example.com,cn=addn,cn=plugins,cn=config",
                                    [(ldap.MOD_REPLACE, 'addn_base', ensure_bytes(DEFAULT_SUFFIX))])
    topology_st.standalone.restart(60)

    # Make sure our binds still work.
    assert (_bind(USER1_DN, PW))
    assert (_bind(USER1_CONFLICT_DN, PW))
    for i in range(0, 10):

        # Test bind as name fails
        try:
            _bind(USER1, PW)
            assert (False)
        except:
            pass
        # Test bind as name@domain fails too
        try:
            _bind(USER1_DOMAIN, PW)
            assert (False)
        except:
            pass

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
