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

from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts, UserAccount
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD, ErrorLog
from lib389.config import Config
from lib389.password_plugins import (
    SSHA512Plugin,
    PBKDF2Plugin,
    PBKDF2SHA1Plugin,
    PBKDF2SHA256Plugin,
    PBKDF2SHA512Plugin
)
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1


@pytest.fixture(scope="function")
def test_user(request, topo):
    """Fixture to create and clean up a test user for each test"""
    # Generate unique user ID based on test name
    uid = f'test_user_{request.node.name[:20]}'
    
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
    """Check password scheme PBKDF2-SHA512.

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
    assert Config(topo.standalone).get_attr_val_utf8('passwordStorageScheme') == 'PBKDF2-SHA512'
    assert topo.standalone.status()
    user.delete()


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


@pytest.mark.parametrize('plugin_class,plugin_name', [
    (PBKDF2Plugin, 'PBKDF2'),
    (PBKDF2SHA1Plugin, 'PBKDF2-SHA1'),
    (PBKDF2SHA256Plugin, 'PBKDF2-SHA256'),
    (PBKDF2SHA512Plugin, 'PBKDF2-SHA512')
])
def test_pbkdf2_rounds_configuration(topo, test_user, plugin_class, plugin_name):
    """Test PBKDF2 rounds configuration for different variants"""
    try:
        # Enable plugin logging
        topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))

        # Configure plugin
        plugin = plugin_class(topo.standalone)
        plugin.enable()
        
        # Test rounds configuration
        test_rounds = 20000
        plugin.set_rounds(test_rounds)
        # Restart after changing rounds
        topo.standalone.restart()
        assert plugin.get_rounds() == test_rounds
        
        # Verify invalid rounds are rejected
        with pytest.raises(ValueError):
            plugin.set_rounds(5000)  # Too low
        with pytest.raises(ValueError):
            plugin.set_rounds(2000000)  # Too high
        
        # Configure as password storage scheme
        topo.standalone.config.replace('passwordStorageScheme', plugin_name)
        topo.standalone.deleteErrorLogs()

        # PBKDF2-SHA1 is the actual digest used for PBKDF2
        plugin_name = 'PBKDF2-SHA1' if plugin_name == 'PBKDF2' else plugin_name
        digest_name = plugin_name.split('-')[1] if '-' in plugin_name else 'SHA1'
        
        TEST_PASSWORD = 'Secret123'
        test_user.set('userPassword', TEST_PASSWORD)
        
        # Verify password hash format
        pwd_hash = test_user.get_attr_val_utf8('userPassword')
        assert pwd_hash.startswith('{' + plugin_name.upper() + '}')
        
        # Test authentication
        topo.standalone.simple_bind_s(test_user.dn, TEST_PASSWORD)
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        
        # Restart to flush logs
        topo.standalone.restart()
        
        # Verify logs for configuration message
        assert topo.standalone.searchErrorsLog(
            f'handle_pbkdf2_rounds_config -> Number of iterations for PBKDF2-{digest_name} password scheme set to {test_rounds}'
        )
    
    finally:
        # Always rebind as Directory Manager
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)


@pytest.mark.parametrize('plugin_class,plugin_name', [
    (PBKDF2Plugin, 'PBKDF2'),
    (PBKDF2SHA1Plugin, 'PBKDF2-SHA1'),
    (PBKDF2SHA256Plugin, 'PBKDF2-SHA256'),
    (PBKDF2SHA512Plugin, 'PBKDF2-SHA512')
])
def test_pbkdf2_rounds_modification(topo, test_user, plugin_class, plugin_name):
    """Test PBKDF2 rounds modification behavior"""
    try:
        # Enable plugin logging
        topo.standalone.config.loglevel((ErrorLog.DEFAULT, ErrorLog.PLUGIN))
        
        plugin = plugin_class(topo.standalone)
        plugin.enable()
        
        # Set initial rounds and restart
        initial_rounds = 15000
        plugin.set_rounds(initial_rounds)
        topo.standalone.restart()
        
        # Configure as password storage scheme
        topo.standalone.config.replace('passwordStorageScheme', plugin_name)
        topo.standalone.deleteErrorLogs()

        # PBKDF2-SHA1 is the actual digest used for PBKDF2
        plugin_name = 'PBKDF2-SHA1' if plugin_name == 'PBKDF2' else plugin_name
        digest_name = plugin_name.split('-')[1] if '-' in plugin_name else 'SHA1'
        
        INITIAL_PASSWORD = 'Initial123'
        NEW_PASSWORD = 'New123'
        
        test_user.set('userPassword', INITIAL_PASSWORD)
        
        # Modify rounds and restart
        new_rounds = 25000
        plugin.set_rounds(new_rounds)
        topo.standalone.restart()
        
        # Verify old password still works
        topo.standalone.simple_bind_s(test_user.dn, INITIAL_PASSWORD)
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        
        # Set new password
        test_user.set('userPassword', NEW_PASSWORD)
        
        # Verify new password works
        topo.standalone.simple_bind_s(test_user.dn, NEW_PASSWORD)
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        
        # Verify logs for configuration message
        assert topo.standalone.searchErrorsLog(
            f'handle_pbkdf2_rounds_config -> Number of iterations for PBKDF2-{digest_name} password scheme set to {new_rounds}'
        )

    finally:
        # Always rebind as Directory Manager
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
