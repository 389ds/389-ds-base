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
from lib389._constants import DEFAULT_SUFFIX, DEFAULT_SECURE_PORT
from lib389.sasl import PlainSASL
from lib389.idm.services import ServiceAccounts, ServiceAccount

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


def test_basic_feature(topology_st):
    """Check basic SASL functionality for PLAIN mechanism

    :id: 75ddc6fa-aa5a-4025-9c71-1abad20c91fc
    :setup: Standalone instance
    :steps:
        1. Stop the instance
        2. Clean up confdir from previous cert and key files
        3. Create RSA files: CA, key and cert
        4. Start the instance
        5. Create RSA entry
        6. Set nsslapd-secureport to 636 and nsslapd-security to 'on'
        7. Restart the instance
        8. Create a user
        9. Check we can bind
        10. Check that PLAIN is listed in supported mechs
        11. Set up Plain SASL credentials
        12. Try to open a connection without TLS
        13. Try to open a connection with TLS
        14. Try to open a connection with a wrong password
    :expectedresults:
        1. The instance should stop
        2. Confdir should be clean
        3. RSA files should be created
        4. The instance should start
        5. RSA entry should be created
        6. nsslapd-secureport and nsslapd-security should be set successfully
        7. The instance should be restarted
        8. User should be created
        9. Bind should be successful
        10. PLAIN should be listed in supported mechs
        11. Plain SASL should be successfully set
        12. AUTH_UNKNOWN exception should be raised
        13. The connection should open
        14. INVALID_CREDENTIALS exception should be raised
    """

    standalone = topology_st.standalone
    standalone.enable_tls()

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
        conn = sa.sasl_bind(uri=standalone.get_ldap_uri(), saslmethod='PLAIN', sasltoken=auth_tokens, connOnly=True)

    # We *have* to use REQCERT NEVER here because python ldap fails cert verification for .... some reason that even
    # I can not solve. I think it's leaking state across connections in start_tls_s?

    # Check that it works with TLS
    conn = sa.sasl_bind(uri=standalone.get_ldaps_uri(), saslmethod='PLAIN', sasltoken=auth_tokens, connOnly=True)
    conn.close()

    # Check that it correct fails our bind if we don't have the password.
    auth_tokens = PlainSASL("dn:%s" % sa.dn, 'password-wrong')
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        conn = sa.sasl_bind(uri=standalone.get_ldaps_uri(), saslmethod='PLAIN', sasltoken=auth_tokens, connOnly=True)

    # Done!
