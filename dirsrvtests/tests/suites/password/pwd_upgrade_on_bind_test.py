# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import pytest
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts
from lib389._constants import (DEFAULT_SUFFIX, PASSWORD)

pytestmark = pytest.mark.tier1

def test_password_hash_on_upgrade(topology_st):
    """If a legacy password hash is present, assert that on a correct bind
    the hash is "upgraded" to the latest-and-greatest hash format on the
    server.

    Assert also that password FAILURE does not alter the password.

    :id: 42cf99e6-454d-46f5-8f1c-8bb699864a07
    :setup: Single instance
    :steps: 1. Set a password hash in SSHA256, and hash to pbkdf2 statically
            2. Test a faulty bind
            3. Assert the PW is SSHA256
            4. Test a correct bind
            5. Assert the PW is PBKDF2
    :expectedresults:
            1. Successfully set the values
            2. The bind fails
            3. The PW is SSHA256
            4. The bind succeeds
            5. The PW is PBKDF2
    """
    # Make sure the server is set to pkbdf
    topology_st.standalone.config.set('passwordStorageScheme', 'PBKDF2_SHA256')
    topology_st.standalone.config.set('nsslapd-allow-hashed-passwords', 'on')
    topology_st.standalone.config.set('nsslapd-enable-upgrade-hash', 'on')

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user()
    # Static version of "password" in SSHA256.
    user.set('userPassword', "{SSHA256}9eliEQgjfc4Fcj1IXZtc/ne1GRF+OIjz/NfSTX4f7HByGMQrWHLMLA==")
    # Attempt to bind with incorrect password.
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        badconn = user.bind('badpassword')
    # Check the pw is SSHA256
    up = user.get_attr_val_utf8('userPassword')
    assert up.startswith('{SSHA256}')

    # Bind with correct.
    conn = user.bind(PASSWORD)
    # Check the pw is now PBKDF2!
    up = user.get_attr_val_utf8('userPassword')
    assert up.startswith('{PBKDF2_SHA256}')

def test_password_hash_on_upgrade_clearcrypt(topology_st):
    """In some deploymentes, some passwords MAY be in clear or crypt which have
    specific possible application integrations allowing the read value to be
    processed by other entities. We avoid upgrading these two, to prevent
    breaking these integrations.

    :id: 27712492-a4bf-4ea9-977b-b4850ddfb628
    :setup: Single instance
    :steps: 1. Set a password hash in CLEAR, and hash to pbkdf2 statically
            2. Test a correct bind
            3. Assert the PW is CLEAR
            4. Set the password to CRYPT
            5. Test a correct bind
            6. Assert the PW is CLEAR
    :expectedresults:
            1. Successfully set the values
            2. The bind succeeds
            3. The PW is CLEAR
            4. The set succeeds
            4. The bind succeeds
            5. The PW is CRYPT
    """
    # Make sure the server is set to pkbdf
    topology_st.standalone.config.set('nsslapd-allow-hashed-passwords', 'on')
    topology_st.standalone.config.set('nsslapd-enable-upgrade-hash', 'on')

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user(1001)

    topology_st.standalone.config.set('passwordStorageScheme', 'CLEAR')
    user.set('userPassword', "password")
    topology_st.standalone.config.set('passwordStorageScheme', 'PBKDF2_SHA256')

    conn = user.bind(PASSWORD)
    up = user.get_attr_val_utf8('userPassword')
    assert up.startswith('password')

    user.set('userPassword', "{crypt}I0S3Ry62CSoFg")
    conn = user.bind(PASSWORD)
    up = user.get_attr_val_utf8('userPassword')
    assert up.startswith('{crypt}')

def test_password_hash_on_upgrade_disable(topology_st):
    """If a legacy password hash is present, assert that on a correct bind
    the hash is "upgraded" to the latest-and-greatest hash format on the
    server. But some people may not like this, so test that we can disable
    the feature too!

    :id: ed315145-a3d1-4f17-b04c-73d3638e7ade
    :setup: Single instance
    :steps: 1. Set a password hash in SSHA256, and hash to pbkdf2 statically
            2. Test a faulty bind
            3. Assert the PW is SSHA256
            4. Test a correct bind
            5. Assert the PW is SSHA256
    :expectedresults:
            1. Successfully set the values
            2. The bind fails
            3. The PW is SSHA256
            4. The bind succeeds
            5. The PW is SSHA256
    """
    # Make sure the server is set to pkbdf
    topology_st.standalone.config.set('passwordStorageScheme', 'PBKDF2_SHA256')
    topology_st.standalone.config.set('nsslapd-allow-hashed-passwords', 'on')
    topology_st.standalone.config.set('nsslapd-enable-upgrade-hash', 'off')

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user(1002)
    # Static version of "password" in SSHA256.
    user.set('userPassword', "{SSHA256}9eliEQgjfc4Fcj1IXZtc/ne1GRF+OIjz/NfSTX4f7HByGMQrWHLMLA==")
    # Attempt to bind with incorrect password.
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        badconn = user.bind('badpassword')
    # Check the pw is SSHA256
    up = user.get_attr_val_utf8('userPassword')
    assert up.startswith('{SSHA256}')

    # Bind with correct.
    conn = user.bind(PASSWORD)
    # Check the pw is NOT upgraded!
    up = user.get_attr_val_utf8('userPassword')
    assert up.startswith('{SSHA256}')
