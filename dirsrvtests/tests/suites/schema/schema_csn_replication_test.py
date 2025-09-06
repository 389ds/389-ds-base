
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
from lib389.replica import ReplicationManager, Replicas
from lib389.agreement import Agreements
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

def create_standard_attributetype_override():
    """Create a redefinition of a standard attribute (cosPriority)"""
    return {
        'names': ('cosPriority',),
        'oid': '2.16.840.1.113730.3.1.569',
        'desc': 'Netscape defined attribute type - modified for test',
        'sup': (),
        'syntax': '1.3.6.1.4.1.1466.115.121.1.27',
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
        'x_origin': ('Netscape Directory Server - Test Modified',)
    }

def create_standard_objectclass_override():
    """Create a redefinition of a standard object class (trustAccount)"""
    return {
        'names': ('trustAccount',),
        'oid': '5.3.6.1.1.1.2.0',
        'desc': 'Sets trust accounts information - modified for test',
        'sup': ('top',),
        'kind': 2,  # AUXILIARY
        'must': ('trustModel',),
        'may': ('accessTo', 'ou'),
        'x_origin': ('nss_ldap/pam_ldap - Test Modified',)
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

    log.error(f"Schema CSN sync failed after {timeout}s: {instance1.serverid} CSN={csn1}, {instance2.serverid} CSN={csn2}")
    pytest.fail(f"Schema CSN did not sync in {timeout} seconds")


def trigger_replication(repl_manager, supplier1, supplier2, test_user):
    """Trigger replication by modifying a test entry"""
    test_user.replace('description', f'Replication trigger at {time.time()}')
    repl_manager.wait_for_replication(supplier1, supplier2)


def test_schema_csn_replication_basic_sync(topology_m2, schema_csn_setup):
    """Test basic schema replication and CSN synchronization

    :id: 8a9b5c42-6d7e-4f3c-a5b2-1e9f8d7c6b5a
    :setup: Two supplier replication topology with test users
    :steps:
        1. Add custom attribute types and object classes to supplier2
        2. Modify standard schema definitions on supplier2
        3. Trigger replication from supplier1 to supplier2
        4. Verify schema CSN synchronization
    :expectedresults:
        1. Schema changes are applied successfully
        2. Replication works correctly
        3. Schema CSNs are synchronized between suppliers
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

    log.info('Trigger replication from supplier1 to supplier2')
    trigger_replication(repl, supplier2, supplier1, test_user)

    users1 = UserAccounts(supplier1, DEFAULT_SUFFIX)
    user1 = users1.get(test_user.get_attr_val_utf8('uid'))
    assert user1.get_attr_val_utf8('description') == test_user.get_attr_val_utf8('description')

    log.info('Verify schema CSN synchronization')
    wait_for_schema_csn_sync(supplier1, supplier2)


def test_schema_csn_after_data_replication(topology_m2, schema_csn_setup):
    """Test that schema CSN synchronizes after normal data replication

    :id: 7f8e9d6c-5b4a-3c2d-1e0f-9e8d7c6b5a49
    :setup: Two supplier replication topology with existing schema changes
    :steps:
        1. Perform a data modification on supplier1
        2. Wait for replication to complete
        3. Check schema CSN synchronization with longer wait if needed
    :expectedresults:
        1. Data replication works correctly
        2. Schema CSNs eventually synchronize between suppliers
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    test_users = schema_csn_setup
    test_user = test_users[1]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info('Perform a data modification on supplier1')
    trigger_replication(repl, supplier1, supplier2, test_user)

    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    user2 = users2.get(test_user.get_attr_val_utf8('uid'))
    assert user2.get_attr_val_utf8('description') == test_user.get_attr_val_utf8('description')

    log.info('Check schema CSN synchronization with longer wait if needed')
    wait_for_schema_csn_sync(supplier1, supplier2)


def test_schema_learning_with_paused_agreement(topology_m2, schema_csn_setup, request):
    """Test schema learning when replication agreement is paused

    :id: 9c8d7b6a-5e4f-3a2b-1c0d-8e7d6c5b4a38
    :setup: Two supplier replication topology
    :steps:
        1. Pause replication agreement from supplier2 to supplier1
        2. Add new schema elements to supplier2
        3. Trigger replication from supplier1 to supplier2 (one direction only)
        4. Verify schema CSNs show expected difference
    :expectedresults:
        1. Agreement is paused successfully
        2. Schema is added to supplier2
        3. Replication works in one direction
        4. Schema CSNs show supplier2 has newer schema than supplier1
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    test_users = schema_csn_setup
    test_user = test_users[2]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info('Testing schema learning with paused agreement')

    replica2 = Replicas(supplier2).get(DEFAULT_SUFFIX)
    agreements2 = Agreements(supplier2, replica2.dn)
    agmt_list = agreements2.list()
    assert len(agmt_list) >= 1

    agmt_to_pause = agmt_list[0]
    agmt_to_pause.pause()
    request.addfinalizer(agmt_to_pause.resume)
    log.info(f"Paused agreement: {agmt_to_pause.dn}")

    schema2 = Schema(supplier2)

    custom_at3 = create_custom_attributetype('testAttr', 3)
    schema2.add_attributetype(custom_at3)
    log.info(f"Added custom attribute type to supplier2: {custom_at3['names'][0]}")

    custom_oc3 = create_custom_objectclass('testClass', 3)
    schema2.add_objectclass(custom_oc3)
    log.info(f"Added custom object class to supplier2: {custom_oc3['names'][0]}")

    trigger_replication(repl, supplier1, supplier2, test_user)

    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    user2 = users2.get(test_user.get_attr_val_utf8('uid'))
    assert user2.get_attr_val_utf8('description') == test_user.get_attr_val_utf8('description')

    log.info('Verify schema CSNs are different (replication was paused)')
    schema1 = Schema(supplier1)
    schema2 = Schema(supplier2)

    schema_csn_supplier1 = schema1.get_schema_csn()
    schema_csn_supplier2 = schema2.get_schema_csn()

    assert schema_csn_supplier1 is not None, "Supplier1 schema CSN should not be None"
    assert schema_csn_supplier2 is not None, "Supplier2 schema CSN should not be None"
    assert schema_csn_supplier1 != schema_csn_supplier2


def test_schema_csn_sync_after_agreement_resume(topology_m2, schema_csn_setup):
    """Test schema CSN synchronization after resuming paused agreement

    :id: 6b5a4c3d-2e1f-9e8d-7c6b-5a4c3d2e1f90
    :setup: Two supplier replication topology with paused agreement
    :steps:
        1. Add schema changes to supplier1
        2. Trigger replication multiple times to push schema
        3. Verify schema CSNs eventually synchronize
    :expectedresults:
        1. Schema is added to supplier1
        2. Multiple replication triggers work
        3. Schema CSNs synchronize between suppliers
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    test_users = schema_csn_setup
    test_user = test_users[3]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info('Testing schema CSN synchronization after agreement resume')

    schema1 = Schema(supplier1)

    custom_at4 = create_custom_attributetype('testAttr', 4)
    schema1.add_attributetype(custom_at4)
    log.info(f"Added custom attribute type to supplier1: {custom_at4['names'][0]}")

    custom_oc4 = create_custom_objectclass('testClass', 4)
    schema1.add_objectclass(custom_oc4)
    log.info(f"Added custom object class to supplier1: {custom_oc4['names'][0]}")

    log.info("Triggering replication to update schema")
    trigger_replication(repl, supplier1, supplier2, test_user)

    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    user2 = users2.get(test_user.get_attr_val_utf8('uid'))
    assert user2.get_attr_val_utf8('description') == test_user.get_attr_val_utf8('description')

    log.info("Triggering replication again to push schema")
    test_user.replace('description', f'Second replication trigger at {time.time()}')
    repl.wait_for_replication(supplier1, supplier2)

    user2 = users2.get(test_user.get_attr_val_utf8('uid'))
    assert user2.get_attr_val_utf8('description') == test_user.get_attr_val_utf8('description')

    log.info("Verify schema CSNs synchronize after replication resumes")
    wait_for_schema_csn_sync(supplier1, supplier2)


def test_basic_replication_verification(topology_m2, schema_csn_setup):
    """Test basic replication functionality for setup verification

    :id: 5a4b3c2d-1e0f-8e7d-6c5b-4a3c2d1e0f89
    :setup: Two supplier replication topology
    :steps:
        1. Verify initial replication is working
        2. Check that test entries exist on both suppliers
    :expectedresults:
        1. Replication is functioning correctly
        2. Test entries exist on both suppliers
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    test_users = schema_csn_setup
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info('Verify initial replication is working')
    repl.test_replication(supplier1, supplier2)

    log.info('Check that test entries exist on both suppliers')
    users1 = UserAccounts(supplier1, DEFAULT_SUFFIX)
    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)

    test_user = test_users[0]
    user_uid = test_user.get_attr_val_utf8('uid')

    user1 = users1.get(user_uid)
    user2 = users2.get(user_uid)

    assert user1 is not None
    assert user2 is not None
    assert user1.get_attr_val_utf8('uid') == user2.get_attr_val_utf8('uid')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
