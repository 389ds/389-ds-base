
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from lib389._constants import *
from lib389.mit_krb5 import MitKrb5, KrbClient
from lib389 import DirSrv, Entry
import pytest
import logging
import socket
import subprocess

import ldap
import ldap.sasl

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'
REALM = "EXAMPLE.COM"
TEST_USER = 'uid=test,%s' % DEFAULT_SUFFIX

KEYTAB = "/tmp/test.keytab"
CCACHE = "FILE:/tmp/test.ccache"


class TopologyInstance(object):
    def __init__(self, instance):
        instance.open()
        self.instance = instance


@pytest.fixture(scope="module")
def topology(request):
    # Create the realm
    krb = MitKrb5(realm=REALM)
    instance = DirSrv(verbose=False)
    instance.log.debug("Instance allocated")
    # WARNING: If this test fails it's like a hostname issue!!!
    args = {SER_HOST: socket.gethostname(),
            SER_PORT: INSTANCE_PORT,
            SER_REALM: REALM,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    instance.allocate(args)
    if instance.exists():
        instance.delete()
    # Its likely our realm exists too
    # Remove the old keytab
    if os.path.exists(KEYTAB):
        os.remove(KEYTAB)
    if krb.check_realm():
        krb.destroy_realm()
    # This will automatically create the krb entries
    krb.create_realm()
    instance.create()
    instance.open()

    def fin():
        if instance.exists():
            instance.delete()
        if krb.check_realm():
            krb.destroy_realm()
        if os.path.exists(KEYTAB):
            os.remove(KEYTAB)
        if os.path.exists(CCACHE):
            os.remove(CCACHE)
    request.addfinalizer(fin)

    return TopologyInstance(instance)


@pytest.fixture(scope="module")
def add_user(topology):
    """
    Create a user entry
    """

    log.info('Create a user entry: %s' % TEST_USER)
    uentry = Entry(TEST_USER)
    uentry.setValues('objectclass', 'top', 'extensibleobject')
    uentry.setValues('uid', 'test')
    topology.instance.add_s(uentry)
    # This doesn't matter that we re-open the realm
    krb = MitKrb5(realm=REALM)
    krb.create_principal("test")
    # We extract the kt so we can kinit from it
    krb.create_keytab("test", "/tmp/test.keytab")


def test_gssapi(topology, add_user):
    """
    Check that our bind completese with ldapwhoami correctly mapped from
    the principal to our test user object.
    """
    # Init our local ccache
    kclient = KrbClient("test@%s" % REALM, KEYTAB, CCACHE)
    # Probably need to change this to NOT be raw python ldap
    # conn = ldap.initialize("ldap://%s:%s" % (LOCALHOST, INSTANCE_PORT))
    conn = ldap.initialize("ldap://%s:%s" % (socket.gethostname(), INSTANCE_PORT))
    sasl = ldap.sasl.gssapi("test@%s" % REALM)
    try:
        conn.sasl_interactive_bind_s('', sasl)
    except Exception as e:
        try:
            print("%s" % subprocess.check_output(['klist']))
        except Exception as ex:
            print("%s" % ex)
        print("%s" % os.environ)
        print("IF THIS TEST FAILS ITS LIKELY A HOSTNAME ISSUE")
        raise e
    assert(conn.whoami_s() == "dn: uid=test,dc=example,dc=com")

    print("Error case 1. Broken Kerberos uid mapping")
    uidmapping = 'cn=Kerberos uid mapping,cn=mapping,cn=sasl,cn=config'
    topology.instance.modify_s(uidmapping, [(ldap.MOD_REPLACE, 'nsSaslMapFilterTemplate', '(cn=\1)')])
    conn0 = ldap.initialize("ldap://%s:%s" % (socket.gethostname(), INSTANCE_PORT))
    try:
        conn0.sasl_interactive_bind_s('', sasl)
    except Exception as e:
        print("Exception (expected): %s" % type(e).__name__)
        print('Desc ' + e.message['desc'])
        assert isinstance(e, ldap.INVALID_CREDENTIALS)

    # undo
    topology.instance.modify_s(uidmapping, [(ldap.MOD_REPLACE, 'nsSaslMapFilterTemplate', '(uid=\1)')])

    print("Error case 2. Delete %s from DS" % TEST_USER)
    topology.instance.delete_s(TEST_USER)
    try:
        conn0.sasl_interactive_bind_s('', sasl)
    except Exception as e:
        print("Exception (expected): %s" % type(e).__name__)
        print('Desc ' + e.message['desc'])
        assert isinstance(e, ldap.INVALID_CREDENTIALS)

    print("SUCCESS")


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
