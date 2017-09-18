# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import ldap

from lib389.topologies import topology_st
# This pulls in logging I think
from lib389.utils import *
from lib389.sasl import PlainSASL
from lib389.idm.services import ServiceAccounts
from lib389._constants import (SECUREPORT_STANDALONE1, DEFAULT_SUFFIX)

log = logging.getLogger(__name__)


def test_sasl_plain(topology_st):

    standalone = topology_st.standalone

    # SETUP TLS
    standalone.stop()
    # Prepare SSL but don't enable it.
    for f in ('key3.db', 'cert8.db', 'key4.db', 'cert9.db', 'secmod.db', 'pkcs11.txt'):
        try:
            os.remove("%s/%s" % (standalone.confdir, f))
        except:
            pass
    assert(standalone.nss_ssl.reinit() is True)
    assert(standalone.nss_ssl.create_rsa_ca() is True)
    assert(standalone.nss_ssl.create_rsa_key_and_cert() is True)
    # Start again
    standalone.start()
    standalone.rsa.create()
    # Set the secure port and nsslapd-security
    # Could this fail with selinux?
    standalone.config.set('nsslapd-secureport', '%s' % SECUREPORT_STANDALONE1)
    standalone.config.set('nsslapd-security', 'on')
    # Do we need to restart to allow starttls?
    standalone.restart()

    # Create a user
    sas = ServiceAccounts(standalone, DEFAULT_SUFFIX)
    sas._basedn = DEFAULT_SUFFIX
    sa = sas.create(properties={'cn': 'testaccount', 'userPassword': 'password'})
    # Check we can bind. This will raise exceptions if it fails.
    sa.bind('password')

    # Check that PLAIN is listed in supported mechns.
    assert(standalone.rootdse.supports_sasl_plain())

    # The sasl parameters don't change, so set them up now.
    # Do we need the sasl map dn:?
    auth_tokens = PlainSASL("dn:%s" % sa.dn, 'password')

    # Check that it fails without TLS
    with pytest.raises(ldap.AUTH_UNKNOWN):
        standalone.openConnection(saslmethod='PLAIN', sasltoken=auth_tokens, starttls=False, connOnly=True)

    # We *have* to use REQCERT NEVER here because python ldap fails cert verification for .... some reason that even
    # I can not solve. I think it's leaking state across connections in start_tls_s?

    # Check that it works with TLS
    conn = standalone.openConnection(saslmethod='PLAIN', sasltoken=auth_tokens, starttls=True, connOnly=True,
                                    certdir=standalone.get_cert_dir(), reqcert=ldap.OPT_X_TLS_NEVER)
    conn.close()

    # Check that it correct fails our bind if we don't have the password.
    auth_tokens = PlainSASL("dn:%s" % sa.dn, 'password-wrong')
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        standalone.openConnection(saslmethod='PLAIN', sasltoken=auth_tokens, starttls=True, connOnly=True,
                                  certdir=standalone.get_cert_dir(), reqcert=ldap.OPT_X_TLS_NEVER)

    # Done!
