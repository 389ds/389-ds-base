# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import base64
import hashlib
import struct

import ldap
import pytest
from test389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX, DN_PWDSTORAGE_SCHEMES
from lib389.idm.user import UserAccounts
from lib389.password_plugins import PasswordPlugin, PBKDF2SHA256Plugin
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1

C_PBKDF2_PLUGIN_DN = f'cn=PBKDF2_SHA256,{DN_PWDSTORAGE_SCHEMES}'
C_PBKDF2_CONFIG_OC = 'pwdPBKDF2PluginConfig'
C_PBKDF2_ACCEPT_MAX_ATTR = 'nsslapd-pwdPBKDF2AcceptMaxIterations'
C_PBKDF2_MAX_ACCEPT_ITERATIONS = 2147483647
LEGACY_PBKDF2_ITERATIONS = 50000
HIGH_PBKDF2_ITERATIONS = 60000
LEGACY_PBKDF2_SALT = b'389ds-pbkdf2-upgrade-test'.ljust(64, b'\0')
MODERN_PBKDF2_SCHEME = 'PBKDF2-SHA256'
MODERN_PBKDF2_ITERATIONS = 600000
MODERN_PBKDF2_ROUNDS_ATTR = 'nsslapd-pwdPBKDF2NumIterations'


def _legacy_pbkdf2_sha256_hash(password, iterations=LEGACY_PBKDF2_ITERATIONS):
    digest = hashlib.pbkdf2_hmac('sha256', password.encode(), LEGACY_PBKDF2_SALT,
                                 iterations, dklen=256)
    payload = struct.pack('!I', iterations) + LEGACY_PBKDF2_SALT + digest
    return '{PBKDF2_SHA256}' + base64.b64encode(payload).decode()


def _assert_modern_pbkdf2_sha256_hash(stored_password):
    expected_prefix = f'{{{MODERN_PBKDF2_SCHEME}}}{MODERN_PBKDF2_ITERATIONS}$'
    assert stored_password.startswith(expected_prefix)


@pytest.mark.skipif(ds_is_older('1.4.1'), reason="Not implemented")
def test_pbkdf2_upgrade(topology_st):
    """On upgrade pbkdf2 doesn't ship. We need to be able to
    provide this on upgrade to make sure default hashes work.
    However, password plugins are special - they need really
    early bootstap so that setting the default has specs work.

    This tests that the removal of the pbkdf2 plugin causes
    it to be re-bootstrapped and added.

    :id: c2198692-7c02-433b-af5b-3be54920571a
    :setup: Single instance
    :steps: 1. Remove the PBKDF2 plugin
            2. Restart the server
            3. Restart the server
    :expectedresults:
            1. Plugin is removed (IE pre-upgrade state)
            2. The plugin is bootstrapped and added
            3. No change (already bootstrapped)

    """
    # Remove the pbkdf2 plugin config
    p1 = PBKDF2SHA256Plugin(topology_st.standalone)
    assert(p1.exists())
    p1._protected = False
    p1.delete()
    # Restart
    topology_st.standalone.restart()
    # check it's been readded.
    p2 = PBKDF2SHA256Plugin(topology_st.standalone)
    assert(p2.exists())
    # Now restart to make sure we still work from the non-bootstrap form
    topology_st.standalone.restart()
    p3 = PBKDF2SHA256Plugin(topology_st.standalone)
    assert(p3.exists())


def test_c_pbkdf2_sha256_upgrade(topology_st, request):
    """Verify legacy C PBKDF2_SHA256 and migrate it to modern PBKDF2-SHA256

    :id: 129f0dbc-7b1d-4b6a-b6c7-1ce3266a695d
    :setup: Single instance
    :steps:
        1. Configure modern PBKDF2-SHA256 with 600,000 rounds as the preferred scheme
        2. Remove the C plugin configuration object class to simulate an old installation
        3. Store and verify a legacy 50,000-round C PBKDF2_SHA256 password
        4. Restart the server and verify the configuration object class was restored
        5. Verify a 60,000-round legacy hash is rejected, then raise its verifier maximum
        6. Verify successful legacy binds migrate to the configured modern scheme
        7. Verify invalid persisted legacy configuration falls back safely
        8. Verify newly stored passwords use modern PBKDF2-SHA256 at 600,000 rounds
    :expectedresults:
        1. The modern scheme and work factor are loaded
        2. The legacy plugin entry matches its pre-upgrade configuration
        3. The legacy password authenticates and is transparently migrated
        4. The plugin entry is upgraded and the existing account still authenticates
        5. The default ceiling is enforced and can be raised on the upgraded entry
        6. Migrated hashes use the modern scheme and configured work factor
        7. Invalid stored values do not prevent startup
        8. New password values use the modern scheme and configured work factor
    """
    inst = topology_st.standalone
    plugin = PasswordPlugin(inst, dn=C_PBKDF2_PLUGIN_DN)
    modern_plugin = PBKDF2SHA256Plugin(inst)
    original_allow_hashed = inst.config.get_attr_val_utf8('nsslapd-allow-hashed-passwords')
    original_upgrade_hash = inst.config.get_attr_val_utf8('nsslapd-enable-upgrade-hash')
    original_password_scheme = inst.config.get_attr_val_utf8('passwordStorageScheme')
    original_accept_max = plugin.get_attr_val_utf8(C_PBKDF2_ACCEPT_MAX_ATTR)
    original_modern_rounds = modern_plugin.get_attr_val_utf8(MODERN_PBKDF2_ROUNDS_ATTR)
    user = None

    def fin():
        cleanup_plugin = PasswordPlugin(inst, dn=C_PBKDF2_PLUGIN_DN)
        cleanup_plugin.ensure_present('objectClass', C_PBKDF2_CONFIG_OC)
        if user is not None and user.exists():
            user.delete()
        if original_allow_hashed is None:
            inst.config.remove_all('nsslapd-allow-hashed-passwords')
        else:
            inst.config.replace('nsslapd-allow-hashed-passwords', original_allow_hashed)
        if original_upgrade_hash is None:
            inst.config.remove_all('nsslapd-enable-upgrade-hash')
        else:
            inst.config.replace('nsslapd-enable-upgrade-hash', original_upgrade_hash)
        if original_password_scheme is not None:
            inst.config.replace('passwordStorageScheme', original_password_scheme)
        if original_accept_max is None:
            cleanup_plugin.remove_all(C_PBKDF2_ACCEPT_MAX_ATTR)
        else:
            cleanup_plugin.replace(C_PBKDF2_ACCEPT_MAX_ATTR, original_accept_max)
        cleanup_modern_plugin = PBKDF2SHA256Plugin(inst)
        if original_modern_rounds is None:
            cleanup_modern_plugin.remove_all(MODERN_PBKDF2_ROUNDS_ATTR)
        else:
            cleanup_modern_plugin.replace(MODERN_PBKDF2_ROUNDS_ATTR,
                                          original_modern_rounds)
        inst.restart()

    request.addfinalizer(fin)

    modern_plugin.set_rounds(MODERN_PBKDF2_ITERATIONS)
    inst.config.replace('passwordStorageScheme', MODERN_PBKDF2_SCHEME)
    inst.config.replace('nsslapd-enable-upgrade-hash', 'on')
    inst.restart()

    plugin = PasswordPlugin(inst, dn=C_PBKDF2_PLUGIN_DN)
    assert plugin.present('objectClass', C_PBKDF2_CONFIG_OC)
    plugin.remove('objectClass', C_PBKDF2_CONFIG_OC)
    assert not plugin.present('objectClass', C_PBKDF2_CONFIG_OC)

    inst.config.replace('nsslapd-allow-hashed-passwords', 'on')
    user = UserAccounts(inst, DEFAULT_SUFFIX).create_test_user(uid=7613, gid=7613)
    password = 'LegacyPassword_7613'
    legacy_hash = _legacy_pbkdf2_sha256_hash(password)
    high_iteration_hash = _legacy_pbkdf2_sha256_hash(password, HIGH_PBKDF2_ITERATIONS)
    user.replace('userPassword', legacy_hash)
    assert user.get_attr_val_utf8('userPassword') == legacy_hash
    user.bind(password)
    _assert_modern_pbkdf2_sha256_hash(user.get_attr_val_utf8('userPassword'))

    # Successful binds may upgrade the password to the preferred scheme.  Put
    # the exact legacy hash back before restart so this remains an upgrade test.
    user.replace('userPassword', legacy_hash)
    assert user.get_attr_val_utf8('userPassword') == legacy_hash

    inst.restart()

    plugin = PasswordPlugin(inst, dn=C_PBKDF2_PLUGIN_DN)
    assert plugin.present('objectClass', C_PBKDF2_CONFIG_OC)
    assert user.get_attr_val_utf8('userPassword') == legacy_hash
    user.bind(password)
    _assert_modern_pbkdf2_sha256_hash(user.get_attr_val_utf8('userPassword'))

    user.replace('userPassword', high_iteration_hash)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        user.bind(password)

    plugin.replace(C_PBKDF2_ACCEPT_MAX_ATTR, str(HIGH_PBKDF2_ITERATIONS))
    inst.restart()

    plugin = PasswordPlugin(inst, dn=C_PBKDF2_PLUGIN_DN)
    assert plugin.get_attr_val_int(C_PBKDF2_ACCEPT_MAX_ATTR) == HIGH_PBKDF2_ITERATIONS
    user.bind(password)
    _assert_modern_pbkdf2_sha256_hash(user.get_attr_val_utf8('userPassword'))

    # A successful bind may transparently upgrade the stored password to the
    # server's preferred scheme.  Restore the legacy high-iteration hash so the
    # invalid configuration restart still exercises the verifier ceiling.
    user.replace('userPassword', high_iteration_hash)
    assert user.get_attr_val_utf8('userPassword') == high_iteration_hash

    plugin.replace(C_PBKDF2_ACCEPT_MAX_ATTR,
                   str(C_PBKDF2_MAX_ACCEPT_ITERATIONS + 1))
    inst.restart()

    assert inst.ds_error_log.match(
        f'.*Invalid {C_PBKDF2_ACCEPT_MAX_ATTR} value '
        f'{C_PBKDF2_MAX_ACCEPT_ITERATIONS + 1}.*using the default '
        f'{LEGACY_PBKDF2_ITERATIONS}.*'
    )
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        user.bind(password)
    user.replace('userPassword', legacy_hash)
    user.bind(password)
    _assert_modern_pbkdf2_sha256_hash(user.get_attr_val_utf8('userPassword'))

    new_password = 'ModernPassword_7613'
    user.replace('userPassword', new_password)
    _assert_modern_pbkdf2_sha256_hash(user.get_attr_val_utf8('userPassword'))
    user.bind(new_password)
