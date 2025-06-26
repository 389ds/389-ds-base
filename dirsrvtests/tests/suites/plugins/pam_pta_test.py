# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pwd
import pytest
import os
import shutil
from lib389.utils import *
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import PAMPassThroughAuthPlugin, PAMPassThroughAuthConfigs
from lib389.idm.user import UserAccounts, DEFAULT_BASEDN_RDN

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv('DEBUGGING', False)

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

PAM_PTA_PLUGIN_DN = 'cn=PAM Pass Through Auth,cn=plugins,cn=config'
PAM_PTA_MIGRATED_DN = "cn=default,cn=PAM Pass Through Auth,cn=plugins,cn=config"
PLUGIN_ENABLED = 'nsslapd-pluginEnabled'
SYSTEM_USER = 'pam_pta_user'
SYSTEM_PSWD = 'Secret123'

@pytest.fixture(scope='module')
def system_user():
    """Create a local system user and yield its details."""

    try:
        user_info = pwd.getpwnam(SYSTEM_USER)
    except KeyError:
        log.info("Create system user, set password, add permissions")
        subprocess.run(['sudo', 'useradd', '-m', SYSTEM_USER], check=True)
        subprocess.run(['sudo', 'bash', '-c', f'echo "{SYSTEM_USER}:{SYSTEM_PSWD}" | chpasswd'], check=True)
        subprocess.run(['sudo', 'setfacl', '-m', f'{SYSTEM_USER}:r--', '/etc/shadow'], check=True)
        subprocess.run(['sudo', 'setfacl', '-m', 'dirsrv:r--', '/etc/shadow'], check=True)
        user_info = pwd.getpwnam(SYSTEM_USER)

    try:
        yield user_info

    finally:
        try:
            subprocess.run(['sudo', 'userdel', '-r', SYSTEM_USER], check=True)
        except Exception as e:
            log.warning("Failed to delete system user %s: %s", SYSTEM_USER, e)

@pytest.fixture(scope='module')
def ldap_user(topology_st, system_user):
    """Create an LDAP user, matching the system user and yield its details."""

    inst = topology_st.standalone
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    
    user_props = {
        'uid': system_user.pw_name,
        'cn': system_user.pw_name,
        'sn': 'whatever',
        'uidNumber': str(system_user.pw_uid),
        'gidNumber': str(system_user.pw_gid),
        'homeDirectory': system_user.pw_dir,
    }
    
    log.info("Create ldap user (no password)")
    user = users.create(properties=user_props)
    
    try:
        yield user

    finally:
        try:
            user.delete()
        except Exception as e:
            log.warning("Failed to delete ldap user %s: %s", SYSTEM_USER, e)

@pytest.fixture(scope='module')
def pam_service_ldapserver(migrated_child_config):
    """ Setup config for pamService:ldapserver """

    pam_file = "/etc/pam.d/ldapserver"
    backup_file = pam_file + ".bak"
    required_lines = [
        "auth    required   pam_unix.so",
        "account required   pam_unix.so"
    ]

    pam_file_exists = os.path.exists(pam_file)
    try:
        # Backup config if it exists
        if pam_file_exists:
            shutil.copy2(pam_file, backup_file)

        # Add required lines
        with open(pam_file, "w") as f:
            for line in required_lines:
                f.write(line + "\n")
        os.chmod(pam_file, 0o644)

    except Exception as e:
        if os.path.exists(backup_file):
            # Restore backup on error
            shutil.copy2(backup_file, pam_file)
        pytest.fail(f"Test setup failed with {e}")

    finally:
        # Cleanup
        if pam_file_exists and os.path.exists(backup_file):
            shutil.copy2(backup_file, pam_file)
            os.remove(backup_file)
        elif not pam_file_exists:
            os.remove(pam_file)

@pytest.fixture(scope='module')
def migrated_child_config(topology_st):
    """Check child config entry 'cn=default...' exists, if not skip the test."""

    inst = topology_st.standalone

    try:
        inst.getEntry(PAM_PTA_MIGRATED_DN, ldap.SCOPE_BASE, "(objectClass=*)")
        print(f"{PAM_PTA_MIGRATED_DN} exists")
    except ldap.NO_SUCH_OBJECT:
        pytest.skip(f"Child config '{PAM_PTA_MIGRATED_DN}' does not exist. Skipping test.")

    yield

def test_bind_default_config(topology_st, migrated_child_config, pam_service_ldapserver, ldap_user):
    """Test PAM Passthrough Auth with default config.

    :id: 6e2b16c9-04c8-4dd2-ad55-e8e9c92038dd
    :setup: Standalone instance
    :steps:
        1. Enable TLS (default child config requires pamSecure=True)
        2. Enable the plugin and restart the instance
        3. Bind as ldap user
        4. Check error logs for plugin message
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. No plugin warning in logs
    """
    inst = topology_st.standalone
    inst.enable_tls()

    log.info("Enable PAM PTA plugin and restart server")
    pta = PAMPassThroughAuthPlugin(inst)
    pta.enable()

    inst.restart()
    entry = inst.getEntry(PAM_PTA_PLUGIN_DN, ldap.SCOPE_BASE, "(objectclass=*)", [PLUGIN_ENABLED])
    assert entry.hasAttr(PLUGIN_ENABLED)
    assert entry.getValue(PLUGIN_ENABLED).lower() == b'on'

    log.info("Bind as user, default config")
    try:
        ldap_user.bind(SYSTEM_PSWD)
    except:
        pytest.fail("PTA - default config bind failed")

    log.info("Verify there are no warnings in error logs")
    assert not topology_st.standalone.ds_error_log.match('.*pam_passthru-plugin.*')

    pta.disable()

def test_bind_excluded_suffix(topology_st, pam_service_ldapserver, ldap_user):
    """Test PAM Passthrough Auth with excluded suffix (fallback to default config).

    :id: ddbba223-7dcd-4604-9cfc-392ab053943e
    :setup: Standalone instance
    :steps:
        1. Enable TLS (default child config requires pamSecure=True)
        2. Enable the plugin and restart the instance
        3. Create a config to exclude users suffix
        4. Bind as ldap user
        5. Add user password
        6. Bind as ldap user
        7. Check error logs for plugin message
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Bind should fail with invalid creds (no ldap passwd and not pta)
        5. Success
        6. Success
        7. No plugin warning in logs
    """
    inst = topology_st.standalone
    inst.enable_tls()
    pta_config = "exclude"

    log.info("Enable PAM PTA plugin and restart server")
    pta = PAMPassThroughAuthPlugin(inst)
    pta.enable()

    inst.restart()
    entry = inst.getEntry(PAM_PTA_PLUGIN_DN, ldap.SCOPE_BASE, "(objectclass=*)", [PLUGIN_ENABLED])
    assert entry.hasAttr(PLUGIN_ENABLED)
    assert entry.getValue(PLUGIN_ENABLED).lower() == b'on'

    log.info("Create PAM PTA config to exclude users suffix")
    pta_configs = PAMPassThroughAuthConfigs(inst)
    pta_configs.create(properties={
        'cn': pta_config,
        'pamExcludeSuffix': f"{DEFAULT_BASEDN_RDN},{DEFAULT_SUFFIX}",
        'pamFallback': 'FALSE',
        'pamIDAttr': 'uid',
        'pamMissingSuffix': 'ERROR',
        'pamIDMapMethod': 'ENTRY',
        'pamSecure': 'FALSE',
        'pamService': 'system-auth'
    })

    log.info("Bind as user, excluded suffix, no passwd")
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        ldap_user.bind(SYSTEM_PSWD)

    LDAP_PWSD = "iamexcluded"
    ldap_user.set('userpassword', LDAP_PWSD)
    try:
        log.info("Bind as user, excluded suffix, with passwd")
        ldap_user.bind(LDAP_PWSD)
    except:
        pytest.fail("PTA - excluded suffix bind failed")

    log.info("Verify there are no warnings in error logs")
    assert not topology_st.standalone.ds_error_log.match('.*pam_passthru-plugin.*')

    pta.disable()

def test_bind_included_suffix(topology_st, ldap_user):
    """Test PAM Passthrough Auth with included suffix.

    :id: d2b5366f-b36b-4251-b7ba-1c60546e75a8
    :setup: Standalone instance
    :steps:
        1. Enable the plugin and restart the instance
        2. Create a config to exclude users suffix, fallback=true
        3. Set ldap user password
        4. Bind as ldap user
        5. Check error logs for plugin messages
    :expectedresults:
        1. Success
        2. Success
        3. SUccess
        4. Success
        5. No plugin warning in logs
    """
    inst = topology_st.standalone
    pta_config = "included"

    log.info("Enable PAM PTA plugin and restart server")
    pta = PAMPassThroughAuthPlugin(inst)
    pta.enable()
    inst.restart()
    entry = inst.getEntry(PAM_PTA_PLUGIN_DN, ldap.SCOPE_BASE, "(objectclass=*)", [PLUGIN_ENABLED])
    assert entry.hasAttr(PLUGIN_ENABLED)
    assert entry.getValue(PLUGIN_ENABLED).lower() == b'on'

    log.info("Create PAM PTA config, include users suffix")
    pta_configs = PAMPassThroughAuthConfigs(inst)
    pta_configs.create(properties={
        'cn': pta_config,
        'pamIncludeSuffix': f"{DEFAULT_BASEDN_RDN},{DEFAULT_SUFFIX}",
        'pamFallback': 'FALSE',
        'pamIDAttr': 'uid',
        'pamIDMapMethod': 'ENTRY',
        'pamSecure': 'FALSE',
        'pamService': 'system-auth'
    })

    log.info("Bind as user, included suffix")
    try:
        ldap_user.bind(SYSTEM_PSWD)
    except:
        pytest.fail("PTA - included suffix bind failed")

    log.info("Verify there are no warnings in error logs")
    assert not topology_st.standalone.ds_error_log.match('.*- pam_passthru-plugin -*.')

    pta.disable()

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
