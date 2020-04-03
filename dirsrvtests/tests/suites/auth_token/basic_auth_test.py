# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import pytest
import time
from lib389.idm.user import nsUserAccounts, UserAccounts
from lib389.topologies import topology_st as topology
from lib389.paths import Paths
from lib389.utils import ds_is_older
from lib389._constants import *
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.account import Anonymous
from lib389.extended_operations import LdapSSOTokenRequest

default_paths = Paths()

pytestmark = pytest.mark.tier1

USER_PASSWORD = "password aouoaeu"
TEST_KEY = "4PXhmtKG7iCdT9C49GoBdD92x5X1tvF3eW9bHq4ND2Q="

@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.3.3'), reason="Auth tokens are not available in older versions")
def test_ldap_auth_token_config(topology):
    """ Test that we are able to configure the ldapssotoken backend with various types and states.

    :id: e9b9360b-76df-40ef-9f45-b448df4c9eda

    :setup: Standalone instance

    :steps:
        1. Enable the feature
        2. Set a key manually.
        3. Regerate a key server side.
        4. Attempt to set invalid keys.
        5. Disable the feature
        6. Assert that key changes are rejected

    :expectedresults:
        1. Feature enables
        2. Key is set and accepted
        3. The key is regenerated and unique
        4. The key is rejected
        5. The disable functions online
        6. The key changes are rejected
    """
    # Enable token
    topology.standalone.config.set('nsslapd-enable-ldapssotoken', 'on') # enable it.
    # Set a key
    topology.standalone.config.set('nsslapd-ldapssotoken-secret', TEST_KEY)
    # regen a key
    topology.standalone.config.remove_all('nsslapd-ldapssotoken-secret')
    k1 = topology.standalone.config.get_attr_val_utf8('nsslapd-ldapssotoken-secret')
    assert(k1 != TEST_KEY)
    # set an invalid key
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology.standalone.config.set('nsslapd-ldapssotoken-secret', 'invalid key')
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology.standalone.config.set('nsslapd-ldapssotoken-secret', '')
    # Disable token
    topology.standalone.config.set('nsslapd-enable-ldapssotoken', 'off') # disable it.
    # Set a key
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topology.standalone.config.set('nsslapd-ldapssotoken-secret', TEST_KEY)
    # regen a key
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topology.standalone.config.remove_all('nsslapd-ldapssotoken-secret')


@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.3.3'), reason="Auth tokens are not available in older versions")
def test_ldap_auth_token_nsuser(topology):
    """
    Test that we can generate and authenticate with authentication tokens
    for users in the directory, as well as security properties around these
    tokens.

    :id: 65335341-c85b-457d-ac7d-c4079ac90a60

    :setup: Standalone instance

    :steps:
        1. Create an account
        2. Generate a token for the account
        3. Authenticate with the token
        4. Assert that a token can not be issued from a token-authed account
        5. Regenerate the server key
        6. Assert the token no longer authenticates

    :expectedresults:
        1. Account is created
        2. Token is generated
        3. Token authenticates
        4. Token is NOT issued
        5. The key is regenerated
        6. The token fails to bind.
    """
    topology.standalone.enable_tls()
    topology.standalone.config.set('nsslapd-enable-ldapssotoken', 'on') # enable it.
    nsusers = nsUserAccounts(topology.standalone, DEFAULT_SUFFIX)
    # Create a user as dm.
    user = nsusers.create(properties={
        'uid': 'test_nsuser',
        'cn': 'test_nsuser',
        'displayName': 'testNsuser',
        'legalName': 'testNsuser',
        'uidNumber': '1001',
        'gidNumber': '1001',
        'homeDirectory': '/home/testnsuser',
        'userPassword': USER_PASSWORD,
    })
    # Create a new con and bind as the user.
    user_conn = user.bind(USER_PASSWORD)
    user_account = nsUserAccounts(user_conn, DEFAULT_SUFFIX).get('test_nsuser')
    # From the user_conn do an extop_s for the token
    token = user_account.request_sso_token()
    # Great! Now do a bind where the token is the pw:
    # user_conn_tok = user.bind(token)
    user_conn_tok = user.authenticate_sso_token(token)
    # Assert whoami.
    # Assert that user_conn_tok with the token can NOT get a new token.
    user_tok_account = nsUserAccounts(user_conn_tok, DEFAULT_SUFFIX).get('test_nsuser')
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        user_tok_account.request_sso_token()

    # Check with a lowered ttl (should deny)
    topology.standalone.config.set('nsslapd-ldapssotoken-ttl-secs', '1') # Set a low ttl
    # Ensure it's past - the one time I'll allow a sleep ....
    time.sleep(2)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        user.authenticate_sso_token(token)
    topology.standalone.config.set('nsslapd-ldapssotoken-ttl-secs', '3600') # Set a reasonable

    # Regenerate the server token key
    topology.standalone.config.remove_all('nsslapd-ldapssotoken-secret')
    # check we fail to authenticate.
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        user.authenticate_sso_token(token)

@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.3.3'), reason="Auth tokens are not available in older versions")
def test_ldap_auth_token_disabled(topology):
    """ Assert when the feature is disabled that token operations are not able to progress

    :id: ccde5d0b-7f2d-49d5-b9d5-f7082f8f36a3

    :setup: Standalone instance

    :steps:
        1. Create a user
        2. Attempt to get a token.
        3. Enable the feature, get a token, then disable it.
        4. Attempt to auth

    :expectedresults:
        1. Success
        2. Fails to get a token
        3. Token is received
        4. Auth fails as token is disabled.
    """
    topology.standalone.enable_tls()
    topology.standalone.config.set('nsslapd-enable-ldapssotoken', 'off') # disable it.
    nsusers = nsUserAccounts(topology.standalone, DEFAULT_SUFFIX)
    # Create a user as dm.
    user = nsusers.create(properties={
        'uid': 'test_nsuser1',
        'cn': 'test_nsuser1',
        'displayName': 'testNsuser1',
        'legalName': 'testNsuser1',
        'uidNumber': '1002',
        'gidNumber': '1002',
        'homeDirectory': '/home/testnsuser1',
        'userPassword': USER_PASSWORD,
    })
    # Create a new con and bind as the user.
    user_conn = user.bind(USER_PASSWORD)
    user_account = nsUserAccounts(user_conn, DEFAULT_SUFFIX).get('test_nsuser1')
    # From the user_conn do an extop_s for the token
    with pytest.raises(ldap.PROTOCOL_ERROR):
        user_account.request_sso_token()
    # Now enable it
    topology.standalone.config.set('nsslapd-enable-ldapssotoken', 'on')
    token = user_account.request_sso_token()
    # Now disable
    topology.standalone.config.set('nsslapd-enable-ldapssotoken', 'off')
    # Now attempt to bind (should fail)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        user_account.authenticate_sso_token(token)


@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.3.3'), reason="Auth tokens are not available in older versions")
def test_ldap_auth_token_directory_manager(topology):
    """ Test token auth with directory manager is denied

    :id: ec9aec64-3edf-4f3f-853a-7527b0c42124

    :setup: Standalone instance

    :steps:
        1. Attempt to generate a token as DM

    :expectedresults:
        1. Fails
    """
    topology.standalone.enable_tls()
    topology.standalone.config.set('nsslapd-enable-ldapssotoken', 'on') # enable it.

    dm = DirectoryManager(topology.standalone)
    # Try getting a token at DM, should fail.
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        dm.request_sso_token()

## test as anon (will fail)
@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.3.3'), reason="Auth tokens are not available in older versions")
def test_ldap_auth_token_anonymous(topology):
    """ Test token auth with Anonymous is denied.

    :id: 966068c3-fbc6-468d-a554-18d68d1d895b

    :setup: Standalone instance

    :steps:
        1. Attempt to generate a token as Anonymous

    :expectedresults:
        1. Fails
    """
    topology.standalone.enable_tls()
    topology.standalone.config.set('nsslapd-enable-ldapssotoken', 'on') # enable it.

    anon_conn = Anonymous(topology.standalone).bind()
    # Build the request
    req = LdapSSOTokenRequest()
    # Get the response
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        (_, res) = anon_conn.extop_s(req, escapehatch='i am sure')

