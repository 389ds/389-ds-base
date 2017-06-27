# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import ldap

from lib389.topologies import topology_st
from lib389.utils import logging
from lib389.idm.user import UserAccounts
from lib389._constants import DEFAULT_SUFFIX, SECUREPORT_STANDALONE1

from lib389.config import CertmapLegacy

log = logging.getLogger(__name__)

def test_tls_external(topology_st):

    standalone = topology_st.standalone

    # SETUP TLS
    standalone.stop()
    assert(standalone.nss_ssl.reinit() is True)
    assert(standalone.nss_ssl.create_rsa_ca() is True)
    assert(standalone.nss_ssl.create_rsa_key_and_cert() is True)
    # Create a user
    assert(standalone.nss_ssl.create_rsa_user('testuser') is True)
    # Now get the details of where the key and crt are.
    tls_locs = standalone.nss_ssl.get_rsa_user('testuser')
    #  {'ca': ca_path, 'key': key_path, 'crt': crt_path}

    # Start again
    standalone.start()

    users = UserAccounts(standalone, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'testuser',
        'cn' : 'testuser',
        'sn' : 'user',
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/testuser'
    })

    standalone.rsa.create()
    # Set the secure port and nsslapd-security
    standalone.config.set('nsslapd-secureport', '%s' % SECUREPORT_STANDALONE1 )
    standalone.config.set('nsslapd-security', 'on')
    standalone.sslport = SECUREPORT_STANDALONE1
    # Now turn on the certmap.
    cm = CertmapLegacy(standalone)
    certmaps = cm.list()
    certmaps['default']['DNComps'] = ''
    certmaps['default']['FilterComps'] = ['cn']
    certmaps['default']['VerifyCert'] = 'off'
    cm.set(certmaps)

    # Check that EXTERNAL is listed in supported mechns.
    assert(standalone.rootdse.supports_sasl_external())
    # Restart to allow certmaps to be re-read: Note, we CAN NOT use post_open
    # here, it breaks on auth. see lib389/__init__.py
    standalone.restart(post_open=False)

    # Now attempt a bind with TLS external
    conn = standalone.openConnection(saslmethod='EXTERNAL', connOnly=True, certdir=standalone.get_cert_dir(), userkey=tls_locs['key'], usercert=tls_locs['crt'])

    assert(conn.whoami_s() == "dn: uid=testuser,ou=People,dc=example,dc=com")

    # Backup version of the code:
    # ldap.set_option(ldap.OPT_X_TLS_REQUIRE_CERT, ldap.OPT_X_TLS_NEVER)
    # ldap.set_option(ldap.OPT_X_TLS_CACERTFILE, tls_locs['ca'])
    # ldap.set_option(ldap.OPT_X_TLS_KEYFILE, tls_locs['key'])
    # ldap.set_option(ldap.OPT_X_TLS_CERTFILE, tls_locs['crt'])
    # conn = ldap.initialize(standalone.toLDAPURL())

    # sasl_auth = ldap.sasl.external()
    # conn.sasl_interactive_bind_s("", sasl_auth)

