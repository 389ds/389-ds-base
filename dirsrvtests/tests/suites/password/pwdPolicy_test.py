# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# --- END COPYRIGHT BLOCK ---
#
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

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

from lib389.config import RSA, Encryption, Config

DEBUGGING = False

USER_DN = 'uid=user,ou=People,%s' % DEFAULT_SUFFIX

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)


log = logging.getLogger(__name__)


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

    # Deploy certs
    # This is a trick. The nss db that ships with DS is broken
    for f in ('key3.db', 'cert8.db', 'key4.db', 'cert9.db', 'secmod.db', 'pkcs11.txt'):
        try:
            os.remove("%s/%s" % (topology.standalone.confdir, f ))
        except:
            pass

    assert(standalone.nss_ssl.reinit() is True)
    assert(standalone.nss_ssl.create_rsa_ca() is True)
    assert(standalone.nss_ssl.create_rsa_key_and_cert() is True)

    # Say that we accept the cert
    # Connect again!

    # Enable the SSL options
    standalone.rsa.create()
    standalone.rsa.set('nsSSLPersonalitySSL', 'Server-Cert')
    standalone.rsa.set('nsSSLToken', 'internal (software)')
    standalone.rsa.set('nsSSLActivation', 'on')

    standalone.config.set('nsslapd-secureport', PORT_STANDALONE2)
    standalone.config.set('nsslapd-security', 'on')

    standalone.restart()


    def fin():
        """If we are debugging just stop the instances, otherwise remove
        them
        """
        if DEBUGGING:
            standalone.stop()
        else:
            standalone.delete()

    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)

def _create_user(inst):
    inst.add_s(Entry((
                USER_DN, {
                    'objectClass': 'top account simplesecurityobject'.split(),
                     'uid': 'user',
                     'userpassword': 'password'
                })))


def test_pwdPolicy_constraint(topology):
    '''
    Password policy test: Ensure that on a password change, the policy is
    enforced correctly.
    '''

    # Create a user
    _create_user(topology.standalone)
    # Set the password policy globally
    topology.standalone.config.set('passwordMinLength', '10')
    topology.standalone.config.set('passwordMinDigits', '2')
    topology.standalone.config.set('passwordCheckSyntax', 'on')
    topology.standalone.config.set('nsslapd-pwpolicy-local', 'off')
    # Now open a new ldap connection with TLS
    userconn = ldap.initialize("ldap://%s:%s" % (HOST_STANDALONE, PORT_STANDALONE))
    userconn.set_option(ldap.OPT_X_TLS_REQUIRE_CERT, ldap. OPT_X_TLS_NEVER )
    userconn.start_tls_s()
    userconn.simple_bind_s(USER_DN, 'password')
    # This should have an exception!
    try:
        userconn.passwd_s(USER_DN, 'password', 'password1')
        assert(False)
    except ldap.CONSTRAINT_VIOLATION:
        assert(True)
    # Change the password to something invalid!


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
