import logging
import pytest
import os
import subprocess
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.user import UserAccounts
from lib389.ldapi import LDAPIMapping, LDAPIFixedMapping
from lib389.topologies import topology_st as topo
from lib389.tasks import LDAPIMappingReloadTask

log = logging.getLogger(__name__)

if os.geteuid() != 0:
    pytest.skip("Must be root to run LDAPI tests")

# Common constants
LDAPI_AUTH_CONTAINER = "cn=auto_bind,cn=config"
DEFAULT_PASSWORD = "5ecret_137"


@pytest.fixture(scope="function")
def ldapi_config(topo):
    """Configure LDAPI settings and return socket path"""
    topo.standalone.config.set('nsslapd-accesslog-logbuffering', 'off')
    topo.standalone.config.set('nsslapd-ldapilisten', 'on')
    topo.standalone.config.set('nsslapd-ldapiautobind', 'on')
    topo.standalone.config.set('nsslapd-ldapiDNMappingBase', LDAPI_AUTH_CONTAINER)
    topo.standalone.config.set('nsslapd-ldapimaptoentries', 'on')
    topo.standalone.config.set('nsslapd-ldapiuidnumbertype', 'uidNumber')
    topo.standalone.config.set('nsslapd-ldapigidnumbertype', 'gidNumber')

    ldapi_socket_raw = topo.standalone.config.get_attr_val_utf8('nsslapd-ldapifilepath')
    ldapi_socket = ldapi_socket_raw.replace('/', '%2F')

    return ldapi_socket


def create_os_user(username, uid, password=DEFAULT_PASSWORD):
    """Helper to create OS user"""
    subprocess.run(['useradd', '-u', str(uid), '-p', password, username],
                   check=False)
    return username


def cleanup_os_user(username):
    """Helper to cleanup OS user"""
    try:
        subprocess.run(['userdel', '-r', username], check=False)
    except:
        pass


def ldapsearch_ldapi(ldapi_socket, user=None):
    """Helper to build ldapsearch command for LDAPI"""
    cmd = f'ldapsearch -b \'\' -s base -Y EXTERNAL -H ldapi://{ldapi_socket}'
    if user:
        return f'su {user} -c "{cmd}"'
    return cmd


@pytest.fixture(scope="function")
def os_user_factory(request):
    """Factory fixture to create and cleanup OS users"""
    users = []

    def _create_user(username, uid, password=DEFAULT_PASSWORD):
        create_os_user(username, uid, password)
        users.append(username)
        return username

    yield _create_user

    # Cleanup
    for user in users:
        cleanup_os_user(user)


@pytest.fixture(scope="function")
def ldap_user_factory(topo):
    """Factory fixture to create LDAP users with common properties"""
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)

    def _create_ldap_user(uid, uidnumber, gidnumber, **extra_props):
        """Create LDAP user with standard properties"""
        properties = {
            'uid': uid,
            'cn': uid,
            'sn': uid,
            'uidNumber': str(uidnumber),
            'gidNumber': str(gidnumber),
            'userpassword': PASSWORD,
            'homeDirectory': f'/home/{uid}'
        }
        properties.update(extra_props)
        return users.create(properties=properties)

    return _create_ldap_user


def test_ldapi_authdn_attr_rewrite(topo, ldapi_config, os_user_factory, ldap_user_factory):
    """Test LDAPI Authentication DN mapping feature

    :id: e8d68979-4b3d-4e2d-89ed-f9bad827718c
    :setup: Standalone Instance
    :steps:
        1. Set LDAPI configuration
        2. Create LDAP users
        3. Create OS users
        4. Create entries under cn=config for auto bind subtree and mapping entry
        5. Do an LDAPI ldapsearch as the OS user
        6. OS user was mapped expected LDAP entry
        7. Do search using root & LDAPI
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    LINUX_USER = "ldapi_test_lib389_user"
    LINUX_USER2 = "ldapi_test_lib389_user2"
    LINUX_USER3 = "ldapi_test_lib389_user3"
    LDAP_ENTRY_DN = f"uid=test_ldapi,ou=people,{DEFAULT_SUFFIX}"
    LDAP_ENTRY_DN2 = f"uid=test_ldapi2,ou=people,{DEFAULT_SUFFIX}"
    LDAP_ENTRY_DN3 = f"uid=test_ldapi3,ou=people,{DEFAULT_SUFFIX}"

    # Create LDAP users
    ldap_user_factory('test_ldapi', 2020, 2020, description='userdesc')
    ldap_user_factory('test_ldapi2', 2021, 2021, description='userdesc')
    ldap_user_factory('test_ldapi3', 2023, 2023, description='userdesc')

    # Create OS users
    os_user_factory(LINUX_USER, 5001)
    os_user_factory(LINUX_USER2, 5002)

    # Create some mapping entries
    ldapi_mapping = LDAPIMapping(topo.standalone, LDAPI_AUTH_CONTAINER)
    ldapi_mapping.create_mapping(name='entry_map1', username='dummy1',
                                 ldap_dn=f'uid=dummy1,{DEFAULT_SUFFIX}')
    ldapi_mapping.create_mapping(name='entry_map2', username=LINUX_USER,
                                 ldap_dn=LDAP_ENTRY_DN)
    ldapi_mapping.create_mapping(name='entry_map3', username='dummy2',
                                 ldap_dn=f'uid=dummy3,{DEFAULT_SUFFIX}')

    # Restart server for config to take effect, and clear the access log
    topo.standalone.deleteAccessLogs(restart=True)

    # Bind as OS user using ldapsearch
    os.system(ldapsearch_ldapi(ldapi_config, LINUX_USER))

    # Check access log
    assert topo.standalone.ds_access_log.match(f'.*AUTOBIND dn="{LDAP_ENTRY_DN}".*')

    # Bind as Root DN just to make sure it still works
    assert os.system(ldapsearch_ldapi(ldapi_config)) == 0
    assert topo.standalone.ds_access_log.match(f'.*AUTOBIND dn="{DN_DM}".*')

    # Create some fixed mapping
    ldapi_fixed_mapping = LDAPIFixedMapping(topo.standalone, LDAPI_AUTH_CONTAINER)
    ldapi_fixed_mapping.create_mapping("fixed", "5002", "5002", ldap_dn=LDAP_ENTRY_DN2)
    topo.standalone.deleteAccessLogs(restart=True)

    # Bind as OS user using ldapsearch
    os.system(ldapsearch_ldapi(ldapi_config, LINUX_USER2))

    # Check access log
    assert topo.standalone.ds_access_log.match(f'.*AUTOBIND dn="{LDAP_ENTRY_DN2}".*')

    # Add 3rd user, and test reload task
    os_user_factory(LINUX_USER3, 5003)
    ldapi_fixed_mapping.create_mapping("reload", "5003", "5003", ldap_dn=LDAP_ENTRY_DN3)

    reload_task = LDAPIMappingReloadTask(topo.standalone).create()
    reload_task.wait(timeout=20)

    os.system(ldapsearch_ldapi(ldapi_config, LINUX_USER3))
    assert topo.standalone.ds_access_log.match(f'.*AUTOBIND dn="{LDAP_ENTRY_DN3}".*')


@pytest.mark.parametrize("lock_state,expected_autobind", [
    ("locked", False),      # Locked account should NOT autobind
    ("nonexistent", False), # Non-existent entry should NOT autobind
])
def test_ldapi_for_bad_accounts(topo, ldapi_config, os_user_factory, ldap_user_factory,
                                lock_state, expected_autobind):
    """Test LDAPI memory leaks with locked/non-existent accounts

    :id: c0f5cce9-d62e-441d-9e6f-5b67dc0a4e69
    :setup: Standalone Instance with LDAPI configured
    :steps:
        1. Create LDAP user (or not, if testing non-existent)
        2. Lock account if testing locked state
        3. Create OS user
        4. Create LDAPI mapping
        5. Restart server
        6. Try LDAPI bind as OS user
        7. Verify expected behavior
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. No AUTOBIND for locked/non-existent
    """

    LINUX_USER = f"ldapi_{lock_state}_user"
    LDAP_UID = f"test_{lock_state}"
    LDAP_ENTRY_DN = f"uid={LDAP_UID},ou=people,{DEFAULT_SUFFIX}"
    OS_UID = 6001 if lock_state == "locked" else 6002

    # Create OS user
    os_user_factory(LINUX_USER, OS_UID)

    if lock_state == "locked":
        # Create LDAP user and lock it
        user = ldap_user_factory(LDAP_UID, OS_UID, OS_UID)
        user.replace('nsAccountLock', 'true')

    # Create mapping (to locked account or non-existent entry)
    ldapi_mapping = LDAPIMapping(topo.standalone, LDAPI_AUTH_CONTAINER)
    ldapi_mapping.create_mapping(name=f'{lock_state}_map',
                                 username=LINUX_USER,
                                 ldap_dn=LDAP_ENTRY_DN)

    # Restart and clear logs
    if lock_state == "nonexistent":
        topo.standalone.deleteErrorLogs()
    topo.standalone.deleteAccessLogs(restart=True)

    # Try LDAPI bind as OS user
    os.system(ldapsearch_ldapi(ldapi_config, LINUX_USER))

    # Verify expected behavior
    autobind_match = topo.standalone.ds_access_log.match(f'.*AUTOBIND dn="{LDAP_ENTRY_DN}".*')

    if expected_autobind:
        assert autobind_match, f"Expected AUTOBIND for {lock_state}"
    else:
        assert not autobind_match, f"Unexpected AUTOBIND for {lock_state} account"

    # Additional check for non-existent entry
    if lock_state == "nonexistent":
        assert topo.standalone.ds_error_log.match('.*LDAPI auth mapping.*does not exist.*')


def test_ldapi_locked_root_account(topo, ldapi_config, ldap_user_factory, request):
    """Test LDAPI with locked root account

    This tests the memory leak fix for ldapi.c:348-354 where root_dn
    is not freed when root account is locked.

    :id: 5c07605e-ec94-4b62-8344-87ac4ab9ba6b
    :setup: Standalone Instance with LDAPI configured
    :steps:
        1. Create a new LDAP user to serve as root DN
        2. Change root DN configuration to this user
        3. Rebind as the new root DN
        4. Lock the root DN entry
        5. Try to bind as system root using LDAPI
        6. Verify no AUTOBIND (account is locked)
        7. Restore original root DN
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Fail
        6. Success
        7. Success
    """

    test_root_dn = f'uid=test_rootdn,ou=people,{DEFAULT_SUFFIX}'

    def fin():
        # Cleanup: restore original root DN
        try:
            # First unlock the test root user so we can authenticate with it
            from lib389._mapped_object import DSLdapObject
            try:
                root_user = DSLdapObject(topo.standalone, dn=test_root_dn)
                root_user.remove('nsAccountLock', 'true')
                # Rebind as unlocked test root
                dm = DirectoryManager(topo.standalone, test_root_dn)
                dm.bind(PASSWORD)
            except:
                pass

            # Restore original root DN
            topo.standalone.config.replace('nsslapd-rootdn', DN_DM)
            topo.standalone.restart()
        except Exception as e:
            log.error(f"Cleanup failed: {e}")
    request.addfinalizer(fin)

    # Create a new LDAP user to use as root DN
    root_user = ldap_user_factory('test_rootdn', 7001, 7001)

    # Change root DN to this user (while still unlocked)
    topo.standalone.config.replace('nsslapd-rootdn', root_user.dn)

    # Rebind as the new root DN
    dm = DirectoryManager(topo.standalone, root_user.dn)
    dm.bind(PASSWORD)

    # Lock the root account
    root_user.replace('nsAccountLock', 'true')

    # Try to bind as system root using LDAPI (should fail - account is locked)
    os.system(ldapsearch_ldapi(ldapi_config))

    # Verify NO AUTOBIND in access log (root account is locked)
    assert not topo.standalone.ds_access_log.match(f'.*AUTOBIND dn="{root_user.dn}".*'), \
        "Root DN should not autobind when locked"


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
