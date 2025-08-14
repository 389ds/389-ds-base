# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import ldap
import time
import pytest
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_m2
from lib389.idm.user import UserAccounts
from lib389.replica import ReplicationManager
from lib389._mapped_object import DSLdapObject
from lib389.schema import Schema, OBJECT_MODEL_PARAMS
from ldap.schema.models import ObjectClass

pytestmark = pytest.mark.tier2
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def wait_for_attr_value(instance, dn, attr, value=None, timeout=30):
    """Wait for an attribute to have a specific value or be absent."""

    for _ in range(timeout):
        try:
            entry_obj = DSLdapObject(instance, dn)
            if value is None:
                if not entry_obj.present(attr):
                    return True
            else:
                if entry_obj.present(attr, value):
                    return True
        except (ldap.NO_SUCH_OBJECT, ldap.SERVER_DOWN, ldap.CONNECT_ERROR):
            pass  # Expected during server restart
        except Exception as e:
            log.debug(f"Error checking {dn}: {e}")

        time.sleep(1)

    if value is None:
        raise AssertionError(f"Timeout: attribute '{attr}' still present in {dn} after {timeout}s")
    else:
        raise AssertionError(f"Timeout: attribute '{attr}' does not have value '{value}' in {dn} after {timeout}s")

def _add_oc(instance, oid, name):
    """Add an objectClass"""
    schema = Schema(instance)
    params = OBJECT_MODEL_PARAMS[ObjectClass].copy()
    params.update({
        'names': (name,),
        'oid': oid,
        'desc': 'To test schema update policies',
        'must': ('postalAddress', 'postalCode'),
        'may': ('member', 'street'),
        'sup': ('person',),
    })
    schema.add_objectclass(params)


@pytest.fixture
def temporary_oc2(topology_m2):
    """Fixture to create and automatically clean up OC2UpdatePolicy."""
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    name = "OC2UpdatePolicy"

    log.info(f"Adding temporary objectclass: {name}")
    _add_oc(supplier1, "1.2.3.4.5.6.7.8.9.10.3", name)

    yield name

    log.info(f"Cleaning up temporary objectclass: {name}")
    for s in [supplier1, supplier2]:
        try:
            schema = Schema(s)
            schema.remove_objectclass(name)
        except (ldap.NO_SUCH_OBJECT, ValueError):
            pass


@pytest.fixture(scope="function")
def setup_test_env(request, topology_m2):
    """Initialize the test environment"""
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]

    log.info("Add OCUpdatePolicy that allows 'member' attribute")
    _add_oc(supplier1, "1.2.3.4.5.6.7.8.9.10.2", "OCUpdatePolicy")

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(supplier1, supplier2)
    repl.wait_for_replication(supplier2, supplier1)

    users = UserAccounts(supplier1, DEFAULT_SUFFIX)
    users.create_test_user(uid=1000)

    config = supplier1.config
    config.replace_many(
        ('nsslapd-errorlog-level', str(128 + 8192))
    )

    config2 = supplier2.config
    config2.replace_many(
        ('nsslapd-errorlog-level', str(128 + 8192))
    )

    for i in range(10):
        users.create_test_user(uid=2000 + i)

    log.info("Create main test entry")
    members = [f"uid=test_user_{2000 + i},ou=People,{DEFAULT_SUFFIX}" for i in range(10)]
    members.append(f"uid=test_user_1000,ou=People,{DEFAULT_SUFFIX}")
    users.create(properties={
        'uid': 'test_entry',
        'cn': 'test_entry',
        'sn': 'test_entry',
        'objectClass': ['top', 'person', 'organizationalPerson',
                        'inetOrgPerson', 'posixAccount', 'account', 'OCUpdatePolicy'],
        'postalAddress': 'here',
        'postalCode': '1234',
        'member': members,
        'uidNumber': '3000',
        'gidNumber': '3000',
        'homeDirectory': '/home/test_entry'
    })

    def fin():
        users = UserAccounts(supplier1, DEFAULT_SUFFIX)
        for user in users.list():
            user.delete()

        config.replace('nsslapd-errorlog-level', '0')
        config2.replace('nsslapd-errorlog-level', '0')

        schema = Schema(supplier1)
        try:
            schema.remove_objectclass("OCUpdatePolicy")
        except (ldap.NO_SUCH_OBJECT, ValueError):
            pass

    request.addfinalizer(fin)


def test_schema_update_policy_allow(topology_m2, setup_test_env):
    """Test that schema updates are allowed and replicated when no reject policy is set

    :id: c8e3d2e4-5b7a-4a9d-9f2e-8a5b3c4d6e7f
    :setup: Two supplier replication setup with test entries
    :steps:
        1. Add an entry with custom objectclass on supplier1
        2. Check entry is replicated to supplier2
        3. Update entry on supplier2
        4. Check update is replicated back to supplier1
    :expectedresults:
        1. Entry should be added successfully
        2. Entry should be replicated with schema
        3. Update should succeed
        4. Update should be replicated
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info("Check entry was replicated to supplier2")
    repl.wait_for_replication(supplier1, supplier2)
    users = UserAccounts(supplier1, DEFAULT_SUFFIX)
    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    entry = users2.get('test_entry')
    assert entry

    log.info("Update entry on supplier2")
    entry.replace('description', 'test_add')
    repl.wait_for_replication(supplier2, supplier1)
    entry = users.get('test_entry')
    assert entry.get_attr_val_utf8('description') == 'test_add'


def test_schema_update_policy_reject(topology_m2, setup_test_env, temporary_oc2):
    """Test that schema updates can be rejected based on policy

    :id: d9f4e3b5-6c8b-5b0e-0f3a-9b6c5d8e9g0
    :setup: Two supplier replication setup with test entries
    :steps:
        1. Configure supplier1 to reject schema updates containing OC_NAME
        2. Add a new objectclass on supplier1
        3. Update an entry to trigger schema push
        4. Verify schema was not pushed to supplier2
        5. Remove reject policy
        6. Update entry again
        7. Verify schema is now pushed to supplier2
    :expectedresults:
        1. Policy should be configured
        2. New objectclass should be added
        3. Update should trigger replication
        4. Schema should not be pushed due to policy
        5. Policy should be removed
        6. Update should trigger replication
        7. Schema should now be pushed
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info("Configure supplier to reject schema updates for OCUpdatePolicy")
    policy_dn = f"cn=supplierUpdatePolicy,cn=replSchema,{supplier1.config.dn}"
    policy_entry = DSLdapObject(supplier1, policy_dn)
    policy_entry.add('schemaUpdateObjectclassReject', 'OCUpdatePolicy')
    supplier1.restart()
    wait_for_attr_value(supplier1, policy_dn, 'schemaUpdateObjectclassReject', 'OCUpdatePolicy')

    log.info("Verify OC2UpdatePolicy is in supplier1")
    schema = Schema(supplier1)
    schema_attrs = schema.get_objectclasses()
    assert any('oc2updatepolicy' in (name.lower() for name in oc.names) for oc in schema_attrs)

    log.info("Update entry on supplier1 to trigger schema push")
    users = UserAccounts(supplier1, DEFAULT_SUFFIX)
    test_user = users.get('test_entry')
    test_user.replace('description', 'test_reject')

    log.info("Check update was replicated")
    repl.wait_for_replication(supplier1, supplier2)
    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    entry = users2.get('test_entry')
    assert entry.get_attr_val_utf8('description') == 'test_reject'

    log.info("Verify OC2UpdatePolicy was NOT pushed to supplier2")
    schema_attrs = supplier2.schema.get_objectclasses()
    assert not any('oc2updatepolicy' in (name.lower() for name in oc.names) for oc in schema_attrs)

    log.info("Remove reject policy")
    policy_entry.remove('schemaUpdateObjectclassReject', 'OCUpdatePolicy')
    supplier1.restart()
    wait_for_attr_value(supplier1, policy_dn, 'schemaUpdateObjectclassReject', None)

    log.info("Update entry again to trigger schema push")
    test_user.replace('description', 'test_no_more_reject')

    log.info("Check update was replicated")
    repl.wait_for_replication(supplier1, supplier2)
    entry = users2.get('test_entry')
    assert entry.get_attr_val_utf8('description') == 'test_no_more_reject'

    log.info("Verify OC2UpdatePolicy is now in supplier2")
    schema_attrs = supplier2.schema.get_objectclasses()
    assert any('oc2updatepolicy' in (name.lower() for name in oc.names) for oc in schema_attrs)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
