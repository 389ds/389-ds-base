# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import pytest
import ldap
import os

from lib389.topologies import topology_m2
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.group import Groups, Group
from lib389.idm.domain import Domain
from lib389.idm.directorymanager import DirectoryManager
from ldap.schema.models import ObjectClass
from lib389.replica import ReplicationManager
from lib389.schema import Schema, OBJECT_MODEL_PARAMS
from lib389._constants import DEFAULT_SUFFIX, PASSWORD

log = logging.getLogger(__name__)
pytestmark = pytest.mark.tier2


@pytest.fixture(scope="module")
def setup_test_env(topology_m2, request):
    """
    Sets up a Two supplier replication environment for testing SELFDN ACIs.

    This fixture prepares the environment by creating a custom object class,
    a bind user, and several dummy entries. It removes default ACIs for a
    clean slate and handles cleanup of all created objects after the test.
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    users = UserAccounts(supplier1, DEFAULT_SUFFIX)

    schema = Schema(supplier1)
    oc_name = 'OCSelfDNTest'
    bind_name = 'bind_entry'

    log.info(f"Adding custom object class '{oc_name}'")
    parameters = OBJECT_MODEL_PARAMS[ObjectClass].copy()
    parameters.update({
        'names': (oc_name,),
        'oid': '1.2.3.4.5.6.7.8.9.10.2',
        'desc': 'Test SELFDN ACLs',
        'sup': ('person',),
        'kind': 2, # 0=STRUCTURAL, 1=ABSTRACT, 2=AUXILIARY
        'must': ('postalAddress', 'postalCode'),
        'may': ('member', 'street')
    })
    schema.add_objectclass(parameters)

    log.info(f"Adding bind user '{bind_name}'")
    bind_properties = {
        'cn': bind_name,
        'sn': bind_name,
        'userpassword': 'password',
        'uidNumber': '1000',
        'gidNumber': '1000',
        'homeDirectory': f'/home/{bind_name}'
    }
    bind_user = users.create(f"cn={bind_name}", bind_properties)

    log.info("Removing default ACIs from suffix")
    domain1 = Domain(supplier1, DEFAULT_SUFFIX)
    domain2 = Domain(supplier2, DEFAULT_SUFFIX)
    domain1.remove_all('aci')
    domain2.remove_all('aci')

    log.info("Adding dummy entries for member attribute")
    for i in range(10):
        dummy_name = f"other_entry{i}"
        dummy_properties = {
            'cn': dummy_name,
            'sn': dummy_name,
            'uidNumber': str(1001 + i),
            'gidNumber': '1000',
            'homeDirectory': f'/home/{dummy_name}'
        }
        users.create(f"cn={dummy_name}", dummy_properties)

    repl = ReplicationManager(DEFAULT_SUFFIX)
    dm = DirectoryManager(supplier1)
    dm.rebind()
    repl.wait_for_replication(supplier1, supplier2)

    def cleanup():
        log.info("Cleaning up: removing test entries and custom schema")
        dm = DirectoryManager(supplier1)
        dm.rebind()
        bind_user.delete()
        try:
            test_user = users.get('test_entry')
            test_user.delete()
        except ldap.NO_SUCH_OBJECT:
            pass
        try:
            test_user = users.get('test_without_member')
            test_user.delete()
        except ldap.NO_SUCH_OBJECT:
            pass
        try:
            test_user = users.get('test_multiple_members')
            test_user.delete()
        except ldap.NO_SUCH_OBJECT:
            pass
        for i in range(10):
            try:
                dummy_user = users.get(f"other_entry{i}")
                dummy_user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass
        schema.remove_objectclass(oc_name)

    request.addfinalizer(cleanup)

    yield {'oc_name': oc_name, 'bind_dn': bind_user.dn, 'suffix': DEFAULT_SUFFIX}


def test_selfdn_acl_operations(topology_m2, setup_test_env):
    """
    Validates that SELFDN ACIs for ADD and MODIFY operations function
    correctly and are replicated across a multi-master setup.

    :id: 4db4a0eb-62a4-4288-91aa-c4241c144981
    :setup: Two supplier replication
    :steps:
        1. Verify ADD is denied without a 'SELFDN' ACI.
        2. Add ACI to allow ADD if bind DN is in the 'member' attribute.
        3. Verify ADD is denied if the 'member' attribute is missing.
        4. Verify ADD is denied if multiple 'member' values are present.
        5. Verify ADD is allowed when a single 'member' attribute contains the bind DN.
        6. Verify the new entry is replicated to the replica.
        7. Verify MODIFY is denied without a 'SELFDN' WRITE ACI.
        8. Add ACI to allow WRITE if bind DN is in the 'member' attribute.
        9. Verify MODIFY is allowed with the correct ACI.
        10. Verify the modification is replicated to the replica.
        11. Verify a modification on the replica is replicated back to the master.
    :expectedresults:
        1. The ADD operation should fail with INSUFFICIENT_ACCESS.
        2. The ACI should be added successfully.
        3. The ADD operation should fail with INSUFFICIENT_ACCESS.
        4. The ADD operation should fail with INSUFFICIENT_ACCESS.
        5. The ADD operation should succeed.
        6. The new entry should be present on the replica.
        7. The MODIFY operation should fail with INSUFFICIENT_ACCESS.
        8. The ACI should be added successfully.
        9. The MODIFY operation should succeed.
        10. The modification should be present on the replica.
        11. The modification should be replicated back to the master.
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]

    oc_name = setup_test_env['oc_name']
    bind_dn = setup_test_env['bind_dn']
    entry_name = 'test_entry'

    entry_attrs = {
        'objectclass': ['top', 'person', oc_name],
        'sn': entry_name,
        'cn': entry_name,
        'postalAddress': 'here',
        'postalCode': '1234',
        'member': bind_dn
    }

    log.info("Verifying ADD is denied without ACI")
    bind_user = UserAccount(supplier1, bind_dn)
    user_conn = bind_user.bind(PASSWORD)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        groups = Groups(user_conn, DEFAULT_SUFFIX)
        groups.create(properties=entry_attrs)
    user_conn.close()

    log.info("Adding 'SelfDN add' ACI")
    dm = DirectoryManager(supplier1)
    dm.rebind()
    aci_add = (f'(target = "ldap:///cn=*,ou=Groups,{DEFAULT_SUFFIX}")(targetfilter ="(objectClass={oc_name})")'
               f'(version 3.0; acl "SelfDN add"; allow (add) userattr = "member#selfDN";)')
    domain = Domain(supplier1, DEFAULT_SUFFIX)
    domain.add('aci', aci_add)
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(supplier1, supplier2)

    log.info("Verifying ADD is denied if 'member' attribute is missing")
    entry_without_member_attrs = {
        'objectclass': ['top', 'person', oc_name],
        'sn': 'test_without_member',
        'cn': 'test_without_member',
        'postalAddress': 'here',
        'postalCode': '1234'
    }

    user_conn = bind_user.bind(PASSWORD)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        groups = Groups(user_conn, DEFAULT_SUFFIX)
        groups.create(properties=entry_without_member_attrs)
    user_conn.close()

    log.info("Verifying ADD is denied with multiple 'member' values")
    entry_with_multiple_members_attrs = {
        'objectclass': ['top', 'person', oc_name],
        'sn': 'test_multiple_members',
        'cn': 'test_multiple_members',
        'postalAddress': 'here',
        'postalCode': '1234',
        'member': [bind_dn, f'cn=other_entry0,ou=People,{DEFAULT_SUFFIX}']
    }
    user_conn = bind_user.bind(PASSWORD)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        groups = Groups(user_conn, DEFAULT_SUFFIX)
        groups.create(properties=entry_with_multiple_members_attrs)
    user_conn.close()

    log.info("Verifying ADD is allowed with correct ACI and 'member' attribute")
    user_conn = bind_user.bind(PASSWORD)
    groups = Groups(user_conn, DEFAULT_SUFFIX)
    test_entry = groups.create(properties=entry_attrs)
    user_conn.close()

    log.info("Verifying entry was replicated")
    repl.wait_for_replication(supplier1, supplier2)
    assert Group(supplier2, test_entry.dn), f"Entry {test_entry.dn} not found on supplier2"

    log.info("Verifying MODIFY is denied without WRITE ACI")
    user_conn = bind_user.bind(PASSWORD)
    test_entry_user = Group(user_conn, test_entry.dn)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        test_entry_user.replace('postalCode', '9876')
    user_conn.close()

    log.info("Adding 'SelfDN write' ACI")
    aci_write = (f'(target = "ldap:///cn=*,ou=Groups,{DEFAULT_SUFFIX}")(targetattr = *)(targetfilter ="(objectClass={oc_name})")'
                 f'(version 3.0; acl "SelfDN write"; allow (write) userattr = "member#selfDN";)')
    domain.add('aci', aci_write)
    repl.wait_for_replication(supplier1, supplier2)

    log.info("Verifying MODIFY is allowed with correct ACI")
    user_conn = bind_user.bind(PASSWORD)
    test_entry_user = Group(user_conn, test_entry.dn)
    test_entry_user.replace('postalCode', '1928')
    user_conn.close()

    log.info("Verifying modification was replicated")
    repl.wait_for_replication(supplier1, supplier2)

    entry_on_supplier2 = Group(supplier2, test_entry.dn)
    assert entry_on_supplier2.get_attr_val_utf8('postalCode') == '1928'

    log.info("Verifying MODIFY on replica is replicated back to master")
    bind_user_m2 = UserAccount(supplier2, bind_dn)
    user_conn_m2 = bind_user_m2.bind(PASSWORD)
    entry_m2_group = Group(user_conn_m2, test_entry.dn)
    entry_m2_group.replace('postalCode', '9999')
    user_conn_m2.close()

    repl.wait_for_replication(supplier2, supplier1)

    entry_m1_updated = Group(supplier1, test_entry.dn)
    assert entry_m1_updated.get_attr_val_utf8('postalCode') == '9999'


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
