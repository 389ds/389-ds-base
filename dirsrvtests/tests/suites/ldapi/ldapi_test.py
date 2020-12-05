import logging
import pytest
import os
import subprocess
from lib389._constants import DEFAULT_SUFFIX, DN_DM
from lib389.idm.user import UserAccounts
from lib389.ldapi import LDAPIMapping, LDAPIFixedMapping
from lib389.topologies import topology_st as topo
from lib389.tasks import LDAPIMappingReloadTask


def test_ldapi_authdn_attr_rewrite(topo, request):
    """Test LDAPI Authentication DN mapping feature

    :id: e8d68979-4b3d-4e2d-89ed-f9bad827718c
    :setup: Standalone Instance
    :steps:
        1. Set LDAPI configuration
        2. Create LDAP user
        3. Create OS user
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
    LINUX_PWD = "5ecret_137"
    LDAP_ENTRY_DN = "uid=test_ldapi,ou=people,dc=example,dc=com"
    LDAP_ENTRY_DN2 = "uid=test_ldapi2,ou=people,dc=example,dc=com"
    LDAP_ENTRY_DN3 = "uid=test_ldapi3,ou=people,dc=example,dc=com"
    LDAPI_AUTH_CONTAINER = "cn=auto_bind,cn=config"

    def fin():
        # Remove the OS users
        for user in [LINUX_USER, LINUX_USER2, LINUX_USER3]:
            try:
                subprocess.run(['userdel', '-r', user])
            except:
                pass
    request.addfinalizer(fin)

    # Must be root
    if os.geteuid() != 0:
        return

    # Perform config tasks
    topo.standalone.config.set('nsslapd-accesslog-logbuffering', 'off')
    topo.standalone.config.set('nsslapd-ldapiDNMappingBase', 'cn=auto_bind,cn=config')
    topo.standalone.config.set('nsslapd-ldapimaptoentries', 'on')
    topo.standalone.config.set('nsslapd-ldapiuidnumbertype', 'uidNumber')
    topo.standalone.config.set('nsslapd-ldapigidnumbertype', 'gidNumber')
    ldapi_socket_raw = topo.standalone.config.get_attr_val_utf8('nsslapd-ldapifilepath')
    ldapi_socket = ldapi_socket_raw.replace('/', '%2F')

    # Create LDAP users
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user_properties = {
        'uid': 'test_ldapi',
        'cn': 'test_ldapi',
        'sn': 'test_ldapi',
        'uidNumber': '2020',
        'gidNumber': '2020',
        'userpassword': 'password',
        'description': 'userdesc',
        'homeDirectory': '/home/test_ldapi'}
    users.create(properties=user_properties)

    user_properties = {
        'uid': 'test_ldapi2',
        'cn': 'test_ldapi2',
        'sn': 'test_ldapi2',
        'uidNumber': '2021',
        'gidNumber': '2021',
        'userpassword': 'password',
        'description': 'userdesc',
        'homeDirectory': '/home/test_ldapi2'}
    users.create(properties=user_properties)

    user_properties = {
        'uid': 'test_ldapi3',
        'cn': 'test_ldapi3',
        'sn': 'test_ldapi3',
        'uidNumber': '2023',
        'gidNumber': '2023',
        'userpassword': 'password',
        'description': 'userdesc',
        'homeDirectory': '/home/test_ldapi3'}
    users.create(properties=user_properties)

    # Create OS users
    subprocess.run(['useradd', '-u', '5001', '-p', LINUX_PWD, LINUX_USER])
    subprocess.run(['useradd', '-u', '5002', '-p', LINUX_PWD, LINUX_USER2])

    # Create some mapping entries
    ldapi_mapping = LDAPIMapping(topo.standalone, LDAPI_AUTH_CONTAINER)
    ldapi_mapping.create_mapping(name='entry_map1', username='dummy1',
                                 ldap_dn='uid=dummy1,dc=example,dc=com')
    ldapi_mapping.create_mapping(name='entry_map2', username=LINUX_USER,
                                 ldap_dn=LDAP_ENTRY_DN)
    ldapi_mapping.create_mapping(name='entry_map3', username='dummy2',
                                 ldap_dn='uid=dummy3,dc=example,dc=com')

    # Restart server for config to take effect, and clear the access log
    topo.standalone.deleteAccessLogs(restart=True)

    # Bind as OS user using ldapsearch
    ldapsearch_cmd = f'ldapsearch -b \'\' -s base -Y EXTERNAL -H ldapi://{ldapi_socket}'
    os.system(f'su {LINUX_USER} -c "{ldapsearch_cmd}"')

    # Check access log
    assert topo.standalone.ds_access_log.match(f'.*AUTOBIND dn="{LDAP_ENTRY_DN}".*')

    # Bind as Root DN just to make sure it still works
    assert os.system(ldapsearch_cmd) == 0
    assert topo.standalone.ds_access_log.match(f'.*AUTOBIND dn="{DN_DM}".*')

    # Create some fixed mapping
    ldapi_fixed_mapping = LDAPIFixedMapping(topo.standalone, LDAPI_AUTH_CONTAINER)
    ldapi_fixed_mapping.create_mapping("fixed", "5002", "5002", ldap_dn=LDAP_ENTRY_DN2)
    topo.standalone.deleteAccessLogs(restart=True)

    # Bind as OS user using ldapsearch
    os.system(f'su {LINUX_USER2} -c "{ldapsearch_cmd}"')

    # Check access log
    assert topo.standalone.ds_access_log.match(f'.*AUTOBIND dn="{LDAP_ENTRY_DN2}".*')

    # Add 3rd user, and test reload task
    subprocess.run(['useradd', '-u', '5003', '-p', LINUX_PWD, LINUX_USER3])
    ldapi_fixed_mapping.create_mapping("reload", "5003", "5003", ldap_dn=LDAP_ENTRY_DN3)

    reload_task = LDAPIMappingReloadTask(topo.standalone).create()
    reload_task.wait(timeout=20)

    os.system(f'su {LINUX_USER3} -c "{ldapsearch_cmd}"')
    assert topo.standalone.ds_access_log.match(f'.*AUTOBIND dn="{LDAP_ENTRY_DN3}".*')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
