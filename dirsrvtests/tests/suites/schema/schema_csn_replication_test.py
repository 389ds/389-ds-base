
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import time, os
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_m2
from lib389.replica import ReplicationManager
from lib389.schema import Schema
from lib389.idm.user import UserAccounts
from lib389.config import Config

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

MAX_TEST_USERS = 4


def create_custom_attributetype(name='testAT', num=1):
    """Create a custom attribute type definition"""
    return {
        'names': (f"{name}{num}",),
        'oid': str(9000 + num),
        'desc': f'Test attribute type for schema replication - {name}',
        'sup': (),
        'syntax': '1.3.6.1.4.1.1466.115.121.1.15',
        'syntax_len': None,
        'x_ordered': None,
        'collective': None,
        'obsolete': None,
        'single_value': None,
        'no_user_mod': None,
        'equality': None,
        'substr': None,
        'ordering': None,
        'usage': None,
        'x_origin': ('Test Schema Replication',)
    }

def create_custom_objectclass(name='testOC', num=1):
    """Create a custom object class definition"""
    return {
        'names': (f"{name}{num}",),
        'oid': str(9100 + num),
        'desc': f'Test object class for schema replication - {name}',
        'sup': ('top',),
        'kind': 1,  # STRUCTURAL
        'must': ('ou',),
        'may': (),
        'obsolete': None,
        'x_origin': ('Test Schema Replication',)
    }



@pytest.fixture(scope="function")
def schema_csn_setup(topology_m2, request):
    """Setup fixture for schema CSN replication tests"""
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]

    log.info("Setting up schema CSN replication test environment")

    config1 = Config(supplier1)
    config2 = Config(supplier2)
    config1.set('nsslapd-errorlog-level', '8192')
    config2.set('nsslapd-errorlog-level', '8192')

    users1 = UserAccounts(supplier1, DEFAULT_SUFFIX)

    test_users = []
    for i in range(MAX_TEST_USERS):
        test_user = users1.create_test_user(uid=90000 + i)
        test_users.append(test_user)

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(supplier1, supplier2)

    def cleanup():
        """Cleanup function"""
        log.info("Cleaning up schema CSN test environment")
        try:
            config1.set('nsslapd-errorlog-level', '16384')
            config2.set('nsslapd-errorlog-level', '16384')
            for user in test_users:
                user.delete()
        except Exception as e:
            log.warning(f"Cleanup warning: {e}")

    request.addfinalizer(cleanup)

    return test_users


def wait_for_schema_csn_sync(instance1, instance2, timeout=30):
    """Wait for schema CSN synchronization"""
    schema1 = Schema(instance1)
    schema2 = Schema(instance2)

    for i in range(timeout):
        csn1 = schema1.get_schema_csn()
        csn2 = schema2.get_schema_csn()

        if csn1 is not None and csn2 is not None and csn1 == csn2:
            return True

        if i == 0:
            log.debug(f"Schema CSN sync in progress: {instance1.serverid} to {instance2.serverid}")
        time.sleep(1)

    log.error(
        f"Schema CSN sync failed after {timeout}s: "
        f"{instance1.serverid} CSN={csn1}, "
        f"{instance2.serverid} CSN={csn2}"
    )
    pytest.fail(f"Schema CSN did not sync in {timeout} seconds")


def trigger_replication(repl_manager, supplier1, supplier2, test_user):
    """Trigger replication by modifying a test entry"""
    test_user.replace('description', 'Replication trigger')
    repl_manager.wait_for_replication(supplier1, supplier2)


def test_schema_csn_replication_basic_sync(topology_m2, schema_csn_setup):
    """Test basic schema replication and CSN synchronization

    :id: 8a9b5c42-6d7e-4f3c-a5b2-1e9f8d7c6b5a
    :setup: Two supplier replication topology with test users
    :steps:
        1. Add custom and standard schema definitions to supplier2
        2. Trigger replication from supplier2 to supplier1
        3. Verify that schema CSNs are synchronized
    :expectedresults:
        1. The schema changes are applied successfully on supplier2
        2. The data modification replicates successfully to supplier1
        3. The schema CSNs on both suppliers are identical
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    test_users = schema_csn_setup
    test_user = test_users[0]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info('Add custom attribute types and object classes to supplier2')
    schema2 = Schema(supplier2)

    custom_at = create_custom_attributetype('testAttr', 1)
    schema2.add_attributetype(custom_at)

    custom_oc = create_custom_objectclass('testClass', 1)
    schema2.add_objectclass(custom_oc)

    log.info('Modify standard schema definitions on supplier2')
    new_at = (
        "( 2.16.840.1.113730.3.1.569 NAME 'cosPriority' "
        "DESC 'Netscape defined attribute type - modified for test' "
        "SYNTAX 1.3.6.1.4.1.1466.115.121.1.27 "
        "X-ORIGIN 'Netscape Directory Server - Test Modified' )"
    )
    schema2.add('attributetypes', new_at)

    new_oc = (
        "( 5.3.6.1.1.1.2.0 NAME 'trustAccount' "
        "DESC 'Sets trust accounts information - modified for test' "
        "SUP top AUXILIARY MUST trustModel MAY ( accessTo $ ou ) "
        "X-ORIGIN 'nss_ldap/pam_ldap - Test Modified' )"
    )
    schema2.add('objectClasses', new_oc)

    log.info('Trigger replication from supplier2 to supplier1')

    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    user_on_s2 = users2.get(test_user.get_attr_val_utf8('uid'))
    trigger_replication(repl, supplier2, supplier1, user_on_s2)

    users1 = UserAccounts(supplier1, DEFAULT_SUFFIX)
    user1 = users1.get(test_user.get_attr_val_utf8('uid'))
    assert user1.get_attr_val_utf8('description') == user_on_s2.get_attr_val_utf8('description')

    log.info('Verify schema CSN synchronization')
    wait_for_schema_csn_sync(supplier1, supplier2)


def test_schema_csn_after_data_replication(topology_m2, schema_csn_setup):
    """Test that schema CSN synchronizes after schema and data replication

    :id: 7f8e9d6c-5b4a-3c2d-1e0f-9e8d7c6b5a49
    :setup: Two supplier replication topology
    :steps:
        1. Add new schema definitions to supplier1
        2. Modify a user on supplier1 to trigger replication
        3. Verify that schema CSNs are synchronized
    :expectedresults:
        1. The new schema is added successfully on supplier1
        2. The user modification replicates successfully to supplier2
        3. The schema CSNs on both suppliers are identical
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    test_users = schema_csn_setup
    test_user = test_users[1]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info('Add schema changes to supplier1')
    schema1 = Schema(supplier1)
    custom_at = create_custom_attributetype('dataTestAttr', 5)
    schema1.add_attributetype(custom_at)

    custom_oc = create_custom_objectclass('dataTestClass', 5)
    schema1.add_objectclass(custom_oc)

    log.info('Perform a data modification on supplier1')
    trigger_replication(repl, supplier1, supplier2, test_user)

    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    user2 = users2.get(test_user.get_attr_val_utf8('uid'))
    assert user2.get_attr_val_utf8('description') == test_user.get_attr_val_utf8('description')

    log.info('Check schema CSN synchronization')
    wait_for_schema_csn_sync(supplier1, supplier2)


def test_schema_learning_with_paused_agreement(topology_m2, schema_csn_setup):
    """Test schema learning when replication agreement is paused

    :id: 9c8d7b6a-5e4f-3a2b-1c0d-8e7d6c5b4a38
    :setup: Two supplier replication topology
    :steps:
        1. Get baseline schema CSNs
        2. Pause replication from supplier2 to supplier1
        3. Add new schema elements to supplier2
        4. Verify schema CSNs immediately to avoid keep-alive interference
        5. Resume replication and trigger data replication
    :expectedresults:
        1. Baseline CSNs are captured
        2. Agreements are paused successfully
        3. Schema is added to supplier2
        4. Schema CSNs show supplier2 has newer schema than supplier1
        5. Data replication works after resume
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    test_users = schema_csn_setup
    test_user = test_users[2]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info('Get baseline schema CSNs')
    schema1 = Schema(supplier1)
    schema2 = Schema(supplier2)
    initial_csn1 = schema1.get_schema_csn()
    initial_csn2 = schema2.get_schema_csn()

    log.info('Pause replication from supplier2 to supplier1')
    repl.disable_to_supplier(supplier1, [supplier2])

    log.info('Add new schema elements to supplier2')
    custom_at3 = create_custom_attributetype('testAttr', 3)
    schema2.add_attributetype(custom_at3)
    log.info(f"Added custom attribute type to supplier2: {custom_at3['names'][0]}")

    custom_oc3 = create_custom_objectclass('testClass', 3)
    schema2.add_objectclass(custom_oc3)
    log.info(f"Added custom object class to supplier2: {custom_oc3['names'][0]}")

    log.info('Verify schema CSNs immediately after schema changes')
    new_csn2 = schema2.get_schema_csn()
    current_csn1 = schema1.get_schema_csn()

    assert new_csn2 != initial_csn2, "Supplier2 schema CSN should have changed after schema addition"
    assert current_csn1 == initial_csn1, "Supplier1 schema CSN should be unchanged (paused agreement)"
    assert current_csn1 != new_csn2, "Schema CSNs should be different between suppliers"

    log.info('Resume replication and trigger data replication')
    repl.enable_to_supplier(supplier1, [supplier2])

    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    user_on_s2 = users2.get(test_user.get_attr_val_utf8('uid'))
    trigger_replication(repl, supplier2, supplier1, user_on_s2)

    users1 = UserAccounts(supplier1, DEFAULT_SUFFIX)
    user_on_s1 = users1.get(test_user.get_attr_val_utf8('uid'))
    assert user_on_s1.get_attr_val_utf8('description') == user_on_s2.get_attr_val_utf8('description')


def test_schema_supplier1_to_supplier2_replication(topology_m2, schema_csn_setup):
    """Test schema replication from supplier1 to supplier2 and CSN synchronization

    :id: 4a3b2c1d-0e9f-8d7c-6b5a-4a3b2c1d0e9f
    :setup: Two supplier replication topology
    :steps:
        1. Add schema changes to supplier1
        2. Trigger replication from supplier1 to supplier2
        3. Verify schema CSNs synchronize
    :expectedresults:
        1. Schema changes are applied to supplier1
        2. Replication pushes schema changes to supplier2
        3. Schema CSNs synchronize between suppliers
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    test_users = schema_csn_setup
    test_user = test_users[3]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info('Add schema changes to supplier1')
    schema1 = Schema(supplier1)

    custom_at4 = create_custom_attributetype('testAttr', 4)
    schema1.add_attributetype(custom_at4)

    custom_oc4 = create_custom_objectclass('testClass', 4)
    schema1.add_objectclass(custom_oc4)

    log.info('Trigger replication from supplier1 to supplier2')
    trigger_replication(repl, supplier1, supplier2, test_user)

    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    user2 = users2.get(test_user.get_attr_val_utf8('uid'))
    assert user2.get_attr_val_utf8('description') == test_user.get_attr_val_utf8('description')

    log.info('Verify schema CSNs synchronize after schema push')
    wait_for_schema_csn_sync(supplier1, supplier2)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
