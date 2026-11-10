# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


"""
This file contains the test for password storage scheme
"""

import os
import subprocess
import shutil
import pytest

from test389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts, UserAccount
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD, ErrorLog
from lib389.config import Config
from lib389.password_plugins import (
    SSHA512Plugin,
    PBKDF2SHA1Plugin,
    PBKDF2SHA256Plugin,
    PBKDF2SHA512Plugin
)
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1

PBKDF2_NUM_ITERATIONS_DEFAULT = 100000
FRESH_INSTALL_ACCEPT_MAX = 600000

PBKDF2_SCHEMES = [
    ('PBKDF2-SHA1', PBKDF2SHA1Plugin, PBKDF2_NUM_ITERATIONS_DEFAULT),
    ('PBKDF2-SHA256', PBKDF2SHA256Plugin, PBKDF2_NUM_ITERATIONS_DEFAULT),
    ('PBKDF2-SHA512', PBKDF2SHA512Plugin, PBKDF2_NUM_ITERATIONS_DEFAULT)
]


@pytest.fixture(scope="function")
def new_user(request, topo):
    """Fixture to create and clean up a test user for each test"""
    # Generate unique user ID based on test name
    uid = f'new_user_{request.node.name[:20]}'
    
    # Create user
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': uid,
        'cn': 'Test User',
        'sn': 'User',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': f'/home/{uid}'
    })
    
    def fin():
        try:
            # Ensure we're bound as DM before cleanup
            topo.standalone.simple_bind_s(DN_DM, PASSWORD)
            if user.exists():
                user.delete()
        except Exception as e:
            log.error(f"Error during user cleanup: {e}")
    
    request.addfinalizer(fin)
    return user


def user_config(topo, field_value):
    """
    Will set storage schema and create user.
    """
    Config(topo.standalone).replace("passwordStorageScheme", field_value)
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX).create_test_user()
    user.set('userpassword', 'ItsMeAnuj')
    return user


LIST_FOR_PARAMETERIZATION = ["CRYPT", "SHA", "SSHA", "SHA256", "SSHA256",
                             "SHA384", "SSHA384", "SHA512", "SSHA512", "MD5", "PBKDF2_SHA256"]


@pytest.mark.parametrize("value", LIST_FOR_PARAMETERIZATION, ids=LIST_FOR_PARAMETERIZATION)
def test_check_password_scheme(topo, value):
    """Check all password scheme.

    :id: 196bccfc-33a6-11ea-a2a5-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Change password scheme and create user with password.
        2. check password scheme is set .
        3. Delete user
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
    """
    user = user_config(topo, value)
    assert '{' + f'{value.lower()}' + '}' in \
           UserAccount(topo.standalone, user.dn).get_attr_val_utf8('userpassword').lower()
    user.delete()


@pytest.mark.parametrize('scheme_name,plugin_class,default_rounds', PBKDF2_SCHEMES)
def test_pbkdf2_default_rounds(topo, new_user, scheme_name, plugin_class, default_rounds):
    """Test PBKDF2 schemes with default iteration rounds.

    :id: bd58cd76-14f9-4d54-9793-ee7bba8e5369
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Remove any existing rounds configuration
        2. Verify default rounds are used
        3. Set password and verify hash format
        4. Test authentication
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
        4. Pass
    """
    try:
        # Flush logs
        topo.standalone.restart()
        topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))
        topo.standalone.deleteErrorLogs()
        
        plugin = plugin_class(topo.standalone)
        plugin.remove_all('nsslapd-pwdpbkdf2numiterations')
        topo.standalone.restart()
        
        topo.standalone.config.replace('passwordStorageScheme', scheme_name)
        
        current_rounds = plugin.get_rounds()
        assert current_rounds == default_rounds, \
            f"Expected default {default_rounds} rounds, got {current_rounds}"
        
        new_user.set('userPassword', 'Secret123')
        pwd_hash = new_user.get_attr_val_utf8('userPassword')
        assert pwd_hash.startswith('{' + scheme_name.upper() + '}')
        assert str(default_rounds) in pwd_hash
        
        topo.standalone.simple_bind_s(new_user.dn, 'Secret123')
        
        assert topo.standalone.searchErrorsLog(
            f'{scheme_name} - Number of iterations set to {default_rounds} from default'
        )
    finally:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)


@pytest.mark.parametrize('scheme_name,plugin_class,default_rounds', PBKDF2_SCHEMES)
def test_pbkdf2_rounds_reset(topo, new_user, scheme_name, plugin_class, default_rounds):
    """Test PBKDF2 schemes rounds reset to defaults.

    :id: 59bf95c5-6a07-4db1-81eb-d59b54436826
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Set custom rounds for PBKDF2 plugin
        2. Verify custom rounds are used
        3. Remove rounds configuration
        4. Verify defaults are restored
        5. Test password operations with default rounds
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
        4. Pass
        5. Pass
    """
    try:
        # Flush logs
        topo.standalone.restart()
        topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))
        topo.standalone.deleteErrorLogs()
        
        test_rounds = 25000
        plugin = plugin_class(topo.standalone)
        plugin.set_rounds(test_rounds)
        topo.standalone.restart()
        
        current_rounds = plugin.get_rounds()
        assert current_rounds == test_rounds, \
            f"Expected {test_rounds} rounds, got {current_rounds}"
        
        plugin.remove_all('nsslapd-pwdpbkdf2numiterations')
        topo.standalone.restart()
        
        current_rounds = plugin.get_rounds()
        assert current_rounds == default_rounds, \
            f"Expected default {default_rounds} rounds after reset, got {current_rounds}"
        
        topo.standalone.config.replace('passwordStorageScheme', scheme_name)
        
        new_user.set('userPassword', 'Secret123')
        pwd_hash = new_user.get_attr_val_utf8('userPassword')
        assert pwd_hash.startswith('{' + scheme_name.upper() + '}')
        assert str(default_rounds) in pwd_hash
        
        topo.standalone.simple_bind_s(new_user.dn, 'Secret123')
        
        assert topo.standalone.searchErrorsLog(
            f'{scheme_name} - Number of iterations set to {default_rounds} from default'
        )
    finally:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)


@pytest.mark.parametrize('scheme_name,plugin_class,_', PBKDF2_SCHEMES)
@pytest.mark.parametrize('rounds', [10000, 20000, 50000])
def test_pbkdf2_custom_rounds(topo, new_user, scheme_name, plugin_class, _, rounds):
    """Test PBKDF2 schemes with custom iteration rounds.

    :id: 6bec6542-ed8d-4a0e-89d6-e047757767c2
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Set custom rounds for PBKDF2 plugin
        2. Verify rounds are set correctly
        3. Set password and verify hash format
        4. Test authentication
        5. Verify rounds in password hash
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
        4. Pass
        5. Pass
    """
    try:
        # Flush logs
        topo.standalone.restart()
        topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))
        topo.standalone.deleteErrorLogs()

        plugin = plugin_class(topo.standalone)
        plugin.set_rounds(rounds)
        topo.standalone.restart()
        
        current_rounds = plugin.get_rounds()
        assert current_rounds == rounds, \
            f"Expected {rounds} rounds, got {current_rounds}"
        
        topo.standalone.config.replace('passwordStorageScheme', scheme_name)
        
        new_user.set('userPassword', 'Secret123')
        pwd_hash = new_user.get_attr_val_utf8('userPassword')
        assert pwd_hash.startswith('{' + scheme_name.upper() + '}')
        assert str(rounds) in pwd_hash
        
        topo.standalone.simple_bind_s(new_user.dn, 'Secret123')
        
        assert topo.standalone.searchErrorsLog(
            f'{scheme_name} - Number of iterations set'
        )
    finally:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)


@pytest.mark.parametrize('scheme_name,plugin_class,_', PBKDF2_SCHEMES)
def test_pbkdf2_invalid_rounds(topo, scheme_name, plugin_class, _):
    """Test PBKDF2 schemes with invalid iteration rounds.

    :id: 4e5b4f37-c97b-4f58-b5c5-726495d9fa4e
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Try to set invalid rounds (too low and too high)
        2. Verify appropriate errors are raised
        3. Verify original rounds are maintained
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
    """
    # Flush logs
    topo.standalone.restart()
    topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))
    topo.standalone.deleteErrorLogs()

    plugin = plugin_class(topo.standalone)
    plugin.enable()
    
    original_rounds = plugin.get_rounds()
    
    with pytest.raises(ValueError) as excinfo:
        plugin.set_rounds(5000)
    assert "rounds must be between 10,000 and 10,000,000" in str(excinfo.value)
    
    with pytest.raises(ValueError) as excinfo:
        plugin.set_rounds(20000000)
    assert "rounds must be between 10,000 and 10,000,000" in str(excinfo.value)
    
    current_rounds = plugin.get_rounds()
    assert current_rounds == original_rounds, \
        f"Rounds changed from {original_rounds} to {current_rounds}"


@pytest.mark.parametrize('scheme_name,plugin_class,_', PBKDF2_SCHEMES)
def test_pbkdf2_rounds_persistence(topo, new_user, scheme_name, plugin_class, _):
    """Test PBKDF2 rounds persistence across server restarts.

    :id: b15de1ae-53ac-429f-991b-cea5e6a7b383
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Set custom rounds for PBKDF2 plugin
        2. Restart server
        3. Verify rounds are maintained
        4. Set password and verify hash
        5. Test authentication
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
        4. Pass
        5. Pass
    """
    try:
        # Flush logs
        topo.standalone.restart()
        topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))
        topo.standalone.deleteErrorLogs()

        test_rounds = 15000
        plugin = plugin_class(topo.standalone)
        plugin.set_rounds(test_rounds)
        topo.standalone.restart()
        
        current_rounds = plugin.get_rounds()
        assert current_rounds == test_rounds, \
            f"Expected {test_rounds} rounds after restart, got {current_rounds}"
        
        topo.standalone.config.replace('passwordStorageScheme', scheme_name)
        
        new_user.set('userPassword', 'Secret123')
        pwd_hash = new_user.get_attr_val_utf8('userPassword')
        assert str(test_rounds) in pwd_hash
        
        topo.standalone.simple_bind_s(new_user.dn, 'Secret123')
    finally:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)


def test_clear_scheme(topo):
    """Check clear password scheme.

    :id: 2420aadc-33a6-11ea-b59a-8c16451d917b
    :setup: Standalone
    :steps:
        1. Change password scheme and create user with password.
        2. check password scheme is set .
        3. Delete user
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
    """
    user = user_config(topo, "CLEAR")
    assert "ItsMeAnuj" in UserAccount(topo.standalone, user.dn).get_attr_val_utf8('userpassword')
    user.delete()


def test_check_two_scheme(topo):
    """Check password scheme SHA and CRYPT

    :id: 2b677f1e-33a6-11ea-a371-8c16451d917b
    :setup: Standalone
    :steps:
        1. Change password scheme and create user with password.
        2. check password scheme is set .
        3. Delete user
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
    """
    for schema, value in [("nsslapd-rootpwstoragescheme", "SHA"),
                          ("passwordStorageScheme", "CRYPT")]:
        Config(topo.standalone).replace(schema, value)
    topo.standalone.restart()
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX).create_test_user()
    user.set('userpassword', 'ItsMeAnuj')
    assert '{' + f'{"CRYPT".lower()}' + '}' \
           in UserAccount(topo.standalone, user.dn).get_attr_val_utf8('userpassword').lower()
    user.delete()

@pytest.mark.skipif(ds_is_older('1.4'), reason="Not implemented")
def test_check_pbkdf2_sha512(topo):
    """Check password scheme PBKDF2-SHA512 is restored after deletion

    :id: 31612e7e-33a6-11ea-a750-8c16451d917b
    :setup: Standalone
    :steps:
        1. Try to delete PBKDF2-SHA512.
        2. Should not deleted PBKDF2-SHA512 and server should up.
    :expectedresults:
        1. Pass
        2. Pass
    """
    value = 'PBKDF2-SHA512'
    user = user_config(topo, value)
    assert '{' + f'{value.lower()}' + '}' in \
           UserAccount(topo.standalone, user.dn).get_attr_val_utf8('userpassword').lower()
    plg = PBKDF2SHA512Plugin(topo.standalone)
    plg._protected = False
    plg.delete()
    topo.standalone.restart()
    assert Config(topo.standalone).get_attr_val_utf8('passwordStorageScheme') == value
    assert topo.standalone.status()
    user.delete()


def test_pbkdf2_accept_max_independent_of_rounds(topo, new_user):
    """Lowering generation rounds must not break binds to stronger stored hashes

    :id: 0b9b580b-655e-4844-8363-a3856963f22a
    :setup: Standalone
    :steps:
        1. Configure PBKDF2-SHA256 with 30,000 rounds and accept-max 50,000
        2. Set a user password and verify the hash uses 30,000 rounds
        3. Lower generation rounds to 15,000 and restart
        4. Bind with the existing password
        5. Reset the password and verify new hashes use 15,000 rounds
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
        4. Existing stronger hash still authenticates
        5. New hashes use the lowered generation rounds
    """
    scheme_name = 'PBKDF2-SHA256'
    plugin = PBKDF2SHA256Plugin(topo.standalone)
    try:
        topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))
        plugin.set_accept_max_iterations(50000)
        plugin.set_rounds(30000)
        topo.standalone.restart()
        topo.standalone.config.replace('passwordStorageScheme', scheme_name)

        new_user.set('userPassword', 'Secret123')
        pwd_hash = new_user.get_attr_val_utf8('userPassword')
        assert pwd_hash.startswith('{PBKDF2-SHA256}30000$')

        plugin.set_rounds(15000)
        topo.standalone.restart()

        topo.standalone.simple_bind_s(new_user.dn, 'Secret123')
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)

        new_user.set('userPassword', 'Secret123')
        pwd_hash = new_user.get_attr_val_utf8('userPassword')
        assert pwd_hash.startswith('{PBKDF2-SHA256}15000$')
        topo.standalone.simple_bind_s(new_user.dn, 'Secret123')
    finally:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        plugin.remove_all('nsslapd-pwdpbkdf2numiterations')
        plugin.remove_all('nsslapd-pwdpbkdf2acceptmaxiterations')
        topo.standalone.restart()


def test_pbkdf2_encrypt_caps_rounds_to_accept_max(topo, new_user):
    """Generation rounds above accept-max are capped for new hashes

    :id: b6016b0c-5880-4dda-b9b0-e29ca0fd5c2f
    :setup: Standalone
    :steps:
        1. Configure PBKDF2-SHA256 with rounds 30,000 and accept-max 15,000
        2. Restart and inspect the error log for the capping message
        3. Set a user password
        4. Verify the stored hash uses 15,000 rounds and authenticates
    :expectedresults:
        1. Pass
        2. Cap message is logged
        3. Pass
        4. Hash is capped and bind succeeds
    """
    scheme_name = 'PBKDF2-SHA256'
    plugin = PBKDF2SHA256Plugin(topo.standalone)
    try:
        topo.standalone.restart()
        topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))
        topo.standalone.deleteErrorLogs()

        plugin.set_accept_max_iterations(15000)
        plugin.set_rounds(30000)
        topo.standalone.restart()
        topo.standalone.config.replace('passwordStorageScheme', scheme_name)

        assert topo.standalone.searchErrorsLog(
            'Configured 30000 rounds; capping at the configured accept max 15000'
        )

        new_user.set('userPassword', 'Secret123')
        pwd_hash = new_user.get_attr_val_utf8('userPassword')
        assert pwd_hash.startswith('{PBKDF2-SHA256}15000$')
        topo.standalone.simple_bind_s(new_user.dn, 'Secret123')
    finally:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        plugin.remove_all('nsslapd-pwdpbkdf2numiterations')
        plugin.remove_all('nsslapd-pwdpbkdf2acceptmaxiterations')
        topo.standalone.restart()


def test_pbkdf2_invalid_accept_max_soft_fallback(topo, new_user):
    """Invalid accept-max config falls back to the default and keeps the plugin up

    :id: 4712ae96-63a6-4c0c-bd1e-469f31df6ef3
    :setup: Standalone
    :steps:
        1. Write an out-of-range accept-max directly into the plugin entry
        2. Restart the server
        3. Set a password with the default PBKDF2-SHA256 scheme
        4. Verify authentication still works
    :expectedresults:
        1. Pass
        2. Server starts and logs the soft fallback
        3. Hash uses the default generation rounds
        4. Bind succeeds
    """
    scheme_name = 'PBKDF2-SHA256'
    plugin = PBKDF2SHA256Plugin(topo.standalone)
    try:
        topo.standalone.restart()
        topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))
        topo.standalone.deleteErrorLogs()

        plugin.ensure_present('objectClass', 'pwdPBKDF2PluginConfig')
        plugin.replace('nsslapd-pwdPBKDF2AcceptMaxIterations', '999')
        plugin.remove_all('nsslapd-pwdpbkdf2numiterations')
        topo.standalone.restart()
        topo.standalone.config.replace('passwordStorageScheme', scheme_name)

        assert topo.standalone.searchErrorsLog(
            'Invalid nsslapd-pwdPBKDF2AcceptMaxIterations value 999'
        )
        assert topo.standalone.searchErrorsLog(
            f'PBKDF2 accept max iterations set to {PBKDF2SHA256Plugin.ACCEPT_MAX_DEFAULT}'
        )

        new_user.set('userPassword', 'Secret123')
        pwd_hash = new_user.get_attr_val_utf8('userPassword')
        assert pwd_hash.startswith(
            f'{{PBKDF2-SHA256}}{PBKDF2_NUM_ITERATIONS_DEFAULT}$'
        )
        topo.standalone.simple_bind_s(new_user.dn, 'Secret123')
    finally:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        plugin.remove_all('nsslapd-pwdpbkdf2numiterations')
        plugin.remove_all('nsslapd-pwdpbkdf2acceptmaxiterations')
        topo.standalone.restart()


def test_pbkdf2_missing_accept_max_allows_existing_600k_hash(topo, new_user):
    """Missing accept-max falls back to 10m and still verifies existing 600k hashes

    :id: 33776566-dd5c-4ef6-a8db-eb3d15c0d66e
    :setup: Standalone
    :steps:
        1. Configure PBKDF2-SHA256 with 600,000 generation rounds
        2. Store a user password hashed at 600,000 rounds
        3. Remove accept-max (upgraded install without the attribute) and restart
        4. Bind with the existing password
        5. Confirm the soft-fallback accept-max is logged as 10,000,000
    :expectedresults:
        1. Pass
        2. Stored hash uses 600,000 rounds
        3. Server starts without accept-max configured
        4. Existing 600k hash still authenticates
        5. Runtime default accept-max is 10,000,000
    """
    scheme_name = 'PBKDF2-SHA256'
    plugin = PBKDF2SHA256Plugin(topo.standalone)
    try:
        topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))
        plugin.set_accept_max_iterations(FRESH_INSTALL_ACCEPT_MAX)
        plugin.set_rounds(FRESH_INSTALL_ACCEPT_MAX)
        topo.standalone.restart()
        topo.standalone.config.replace('passwordStorageScheme', scheme_name)

        new_user.set('userPassword', 'Secret123')
        pwd_hash = new_user.get_attr_val_utf8('userPassword')
        assert pwd_hash.startswith(
            f'{{PBKDF2-SHA256}}{FRESH_INSTALL_ACCEPT_MAX}$'
        )

        plugin.remove_all('nsslapd-pwdpbkdf2acceptmaxiterations')
        plugin.remove_all('nsslapd-pwdpbkdf2numiterations')
        topo.standalone.deleteErrorLogs()
        topo.standalone.restart()

        assert plugin.get_attr_val_utf8('nsslapd-pwdPBKDF2AcceptMaxIterations') is None
        assert topo.standalone.searchErrorsLog(
            f'PBKDF2 accept max iterations set to {PBKDF2SHA256Plugin.ACCEPT_MAX_DEFAULT}'
        )
        topo.standalone.simple_bind_s(new_user.dn, 'Secret123')
    finally:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        plugin.remove_all('nsslapd-pwdpbkdf2numiterations')
        plugin.remove_all('nsslapd-pwdpbkdf2acceptmaxiterations')
        topo.standalone.restart()


def test_check_ssha512(topo):
    """Check password scheme SSHA512.

    :id: 9db023d2-33a1-11ea-b68c-8c16451d917b
    :setup: Standalone
    :steps:
        1. Try to delete SSHA512Plugin.
        2. Should deleted SSHA512Plugin and server should not up.
        3. Restore dse file to recover
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
    """
    value = 'SSHA512'
    config_dir = topo.standalone.get_config_dir()
    user = user_config(topo, value)
    assert '{' + f'{value.lower()}' + '}' in \
           UserAccount(topo.standalone, user.dn).get_attr_val_utf8('userpassword').lower()
    plg = SSHA512Plugin(topo.standalone)
    plg._protected = False
    plg.delete()
    with pytest.raises(subprocess.CalledProcessError):
        topo.standalone.restart()
    shutil.copy(config_dir + '/dse.ldif.startOK', config_dir + '/dse.ldif')
    topo.standalone.restart()
    user.delete()


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
