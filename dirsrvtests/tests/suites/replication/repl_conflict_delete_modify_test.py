# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import logging
import os

from lib389.topologies import topology_m2
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.replica import ReplicationManager
from lib389.tombstone import Tombstones
from lib389.config import Config
from lib389._constants import DEFAULT_SUFFIX, PASSWORD

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

STAGING_OU = "staging"
PRODUCTION_OU = "production"
BIND_USER_UID = "bind_user"
TEST_USER_PREFIX = "test_account"
MAX_TEST_USERS = 5

@pytest.fixture(scope="function")
def repl_conflict_setup(topology_m2, request):
    """Setup fixture for replication conflict resolution tests"""
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]

    log.info("Setting up replication conflict test environment")

    config1 = Config(supplier1)
    config2 = Config(supplier2)
    config1.set('nsslapd-errorlog-level', '8192')
    config2.set('nsslapd-errorlog-level', '8192')

    ous_s1 = OrganizationalUnits(supplier1, DEFAULT_SUFFIX)

    if not ous_s1.exists(STAGING_OU):
        staging_ou = ous_s1.create(properties={
            'ou': STAGING_OU,
            'description': 'Staging environment for testing replication conflicts'
        })
    else:
        staging_ou = ous_s1.get(STAGING_OU)

    if not ous_s1.exists(PRODUCTION_OU):
        production_ou = ous_s1.create(properties={
            'ou': PRODUCTION_OU,
            'description': 'Production environment for testing replication conflicts'
        })
    else:
        production_ou = ous_s1.get(PRODUCTION_OU)

    users_s1 = UserAccounts(supplier1, DEFAULT_SUFFIX)
    if not users_s1.exists(BIND_USER_UID):
        bind_user = users_s1.create(properties={
            'uid': BIND_USER_UID,
            'cn': BIND_USER_UID,
            'sn': 'User',
            'givenname': 'Bind',
            'userpassword': PASSWORD,
            'uidNumber': '10001',
            'gidNumber': '10001',
            'homeDirectory': f'/home/{BIND_USER_UID}'
        })
    else:
        bind_user = users_s1.get(BIND_USER_UID)

    test_users = []
    for i in range(MAX_TEST_USERS):
        uid_num = 10100 + i
        user_name = f'{TEST_USER_PREFIX}_{i}'
        user_dn = f'cn={user_name},{staging_ou.dn}'

        if not UserAccount(supplier1, user_dn).exists():
            user = UserAccount(supplier1, user_dn)
            user.create(properties={
                'cn': user_name,
                'sn': f'User{i}',
                'uid': user_name,
                'uidNumber': str(uid_num),
                'gidNumber': str(uid_num),
                'homeDirectory': f'/home/{user_name}'
            })
        else:
            user = UserAccount(supplier1, user_dn)
        test_users.append(user)

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(supplier1, supplier2)

    def cleanup():
        """Cleanup function"""
        log.info("Cleaning up replication conflict test environment")
        try:
            config1.set('nsslapd-errorlog-level', '16384')
            config2.set('nsslapd-errorlog-level', '16384')

            for user in test_users:
                try:
                    if user.exists():
                        user.delete()
                except Exception:
                    pass

            try:
                if bind_user.exists():
                    bind_user.delete()
            except Exception:
                pass

            try:
                if staging_ou.exists():
                    staging_ou.delete()
            except Exception:
                pass

            try:
                if production_ou.exists():
                    production_ou.delete()
            except Exception:
                pass

        except Exception as e:
            log.warning(f"Cleanup warning: {e}")

    request.addfinalizer(cleanup)

    return {
        'staging_ou': staging_ou,
        'production_ou': production_ou,
        'bind_user': bind_user,
        'test_users': test_users
    }


def verify_tombstone_replication(supplier1, supplier2, expected_uid):
    """Verify tombstone exists on both suppliers"""
    repl = ReplicationManager(DEFAULT_SUFFIX)

    repl.wait_for_replication(supplier1, supplier2)
    repl.wait_for_replication(supplier2, supplier1)

    tombstones_s1 = Tombstones(supplier1, DEFAULT_SUFFIX)
    tombstones_s2 = Tombstones(supplier2, DEFAULT_SUFFIX)

    ts1_list = tombstones_s1.filter(f"(&(objectClass=nstombstone)(uid={expected_uid}*))")
    ts2_list = tombstones_s2.filter(f"(&(objectClass=nstombstone)(uid={expected_uid}*))")

    assert len(ts1_list) == 1, f"Expected exactly one tombstone on supplier1 for {expected_uid}"
    assert len(ts2_list) == 1, f"Expected exactly one tombstone on supplier2 for {expected_uid}"

    return ts1_list[0], ts2_list[0]


def verify_entry_modification_replicated(supplier1, supplier2, user_uid, expected_description):
    """Verify that entry modification has been replicated"""
    repl = ReplicationManager(DEFAULT_SUFFIX)

    repl.wait_for_replication(supplier1, supplier2)
    repl.wait_for_replication(supplier2, supplier1)

    staging_ou_s1 = OrganizationalUnits(supplier1, DEFAULT_SUFFIX).get('staging')
    staging_ou_s2 = OrganizationalUnits(supplier2, DEFAULT_SUFFIX).get('staging')

    user1_dn = f'cn={user_uid},{staging_ou_s1.dn}'
    user2_dn = f'cn={user_uid},{staging_ou_s2.dn}'

    user1 = UserAccount(supplier1, user1_dn)
    user2 = UserAccount(supplier2, user2_dn)

    if user1.exists() and user2.exists():
        desc1 = user1.get_attr_val_utf8('description')
        desc2 = user2.get_attr_val_utf8('description')

        return desc1 == expected_description and desc2 == expected_description

    return True


def test_conflict_delete_modify_with_paused_agreements(topology_m2, repl_conflict_setup):
    """Test replication conflict resolution when DELETE on M1 and MODIFY on M2 occur during paused replication

    :id: b7f8e9d6-c5b4-3c2d-1e0f-9e8d7c6b5a47
    :setup: Two supplier replication topology with test organizational structure and users
    :steps:
        1. Verify initial replication is working and entries exist on both suppliers
        2. Pause all replication agreements
        3. Delete a test entry on supplier1
        4. Modify the same entry on supplier2 (and another for verification)
        5. Resume replication agreements
        6. Verify tombstone is created and replicated correctly
        7. Verify modification on the alternate entry is replicated
        8. Verify tombstone contains the modification from supplier2
    :expectedresults:
        1. Initial replication works correctly
        2. Agreements are paused successfully
        3. Entry is deleted on supplier1 (tombstone created)
        4. Entry is modified on supplier2
        5. Replication agreements resume successfully
        6. Tombstone is replicated to supplier2
        7. Other modifications replicate correctly
        8. Tombstone on supplier1 contains the modification from supplier2
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    test_users = repl_conflict_setup['test_users']
    repl = ReplicationManager(DEFAULT_SUFFIX)

    delete_user = test_users[0]
    modify_user = test_users[1]
    verify_user = test_users[2]

    delete_uid = delete_user.get_attr_val_utf8('uid')
    modify_uid = modify_user.get_attr_val_utf8('uid')
    verify_uid = verify_user.get_attr_val_utf8('uid')

    log.info("Verify initial replication is working and entries exist on both suppliers")
    user2_delete = UserAccount(supplier2, delete_user.dn)
    user2_modify = UserAccount(supplier2, modify_user.dn)
    user2_verify = UserAccount(supplier2, verify_user.dn)

    assert user2_delete.exists(), f"User {delete_uid} should exist on supplier2"
    assert user2_modify.exists(), f"User {modify_uid} should exist on supplier2"
    assert user2_verify.exists(), f"User {verify_uid} should exist on supplier2"

    assert user2_delete.exists()
    assert user2_modify.exists()
    assert user2_verify.exists()

    log.info("Pause all replication agreements")
    topology_m2.pause_all_replicas()

    test_description_s1 = 'Modified on supplier1 for conflict test'
    test_description_s2 = 'Modified on supplier2 for conflict test'
    test_description_verify = 'Verification modification for conflict test'

    log.info("Delete test entry on supplier1")
    verify_user.replace('description', 'dummy_before_delete')
    delete_user.delete()
    modify_user.replace('description', test_description_s1)

    log.info("Modify the same entry on supplier2 and another for verification")
    user2_verify.replace('description', 'dummy_before_modify')
    user2_delete.replace('description', test_description_s2)
    user2_modify.replace('description', test_description_s2)
    user2_verify.replace('description', test_description_verify)

    log.info("Resume replication agreements")
    topology_m2.resume_all_replicas()

    repl.wait_for_replication(supplier1, supplier2)
    repl.wait_for_replication(supplier2, supplier1)

    log.info("Verify tombstone is created and replicated correctly")
    ts1, ts2 = verify_tombstone_replication(supplier1, supplier2, delete_uid)

    log.info("Verify modification on the alternate entry is replicated")
    assert verify_entry_modification_replicated(supplier1, supplier2, verify_uid, test_description_verify)

    log.info("Verify tombstone contains the modification from supplier2")
    ts1_description = ts1.get_attr_val_utf8('description')

    assert ts1_description == test_description_s2, f"Expected tombstone description '{test_description_s2}', got '{ts1_description}'"

    user1_modify = UserAccount(supplier1, modify_user.dn)
    user2_modify_final = UserAccount(supplier2, modify_user.dn)

    final_description = user1_modify.get_attr_val_utf8('description')
    assert user2_modify_final.get_attr_val_utf8('description') == final_description

    assert final_description == test_description_s2


def test_basic_organizational_structure_replication(topology_m2, repl_conflict_setup):
    """Test basic replication of organizational structure and user entries

    :id: c8f9a0e1-d6c5-4d3e-2f1a-0f9e8d7c6b58
    :setup: Two supplier replication topology with organizational structure
    :steps:
        1. Verify organizational units exist on both suppliers
        2. Verify test users exist in staging area on both suppliers
        3. Create new user and verify replication
        4. Modify user and verify replication
        5. Delete user and verify tombstone creation
    :expectedresults:
        1. OUs replicated correctly
        2. Users replicated correctly
        3. New user creation replicates
        4. User modification replicates
        5. User deletion creates tombstone
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    staging_ou = repl_conflict_setup['staging_ou']
    test_users = repl_conflict_setup['test_users']
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info("Verify organizational units exist on both suppliers")
    ous_s2 = OrganizationalUnits(supplier2, DEFAULT_SUFFIX)
    staging_ou_s2 = ous_s2.get(STAGING_OU)
    production_ou_s2 = ous_s2.get(PRODUCTION_OU)

    assert staging_ou_s2.exists()
    assert production_ou_s2.exists()

    log.info("Verify test users exist in staging area on both suppliers")
    for test_user in test_users:
        user_s2 = UserAccount(supplier2, test_user.dn)
        assert user_s2.exists()

    log.info("Create new user and verify replication")
    new_user_name = 'new_repl_test_user'
    new_user_dn = f'cn={new_user_name},{staging_ou.dn}'
    new_user = UserAccount(supplier1, new_user_dn)
    new_user.create(properties={
        'cn': new_user_name,
        'sn': 'TestUser',
        'uid': new_user_name,
        'uidNumber': '10200',
        'gidNumber': '10200',
        'homeDirectory': f'/home/{new_user_name}',
        'description': 'Created for replication test'
    })

    repl.wait_for_replication(supplier1, supplier2)

    new_user_s2_dn = f'cn={new_user_name},{staging_ou.dn}'
    new_user_s2 = UserAccount(supplier2, new_user_s2_dn)
    assert new_user_s2.exists(), f"New user {new_user_name} should exist on supplier2"
    assert new_user_s2.get_attr_val_utf8('description') == 'Created for replication test'

    log.info("Modify user and verify replication")
    test_description = 'Modified for replication test'
    new_user.replace('description', test_description)

    repl.wait_for_replication(supplier1, supplier2)

    assert new_user_s2.get_attr_val_utf8('description') == test_description

    log.info("Delete user and verify tombstone creation")
    new_user.delete()

    repl.wait_for_replication(supplier1, supplier2)

    tombstones_s1 = Tombstones(supplier1, staging_ou.dn)
    tombstones_s2 = Tombstones(supplier2, staging_ou.dn)

    ts1_count = len(tombstones_s1.list())
    ts2_count = len(tombstones_s2.list())

    assert ts1_count >= 1, f"Expected at least one tombstone on supplier1, found {ts1_count}"
    assert ts2_count >= 1, f"Expected at least one tombstone on supplier2, found {ts2_count}"


def test_replication_agreement_pause_resume_functionality(topology_m2, repl_conflict_setup):
    """Test replication agreement pause and resume functionality using modern APIs

    :id: d9f0b1e2-e7d6-5e4f-3f2b-1f0e9d8c7b69
    :setup: Two supplier replication topology
    :steps:
        1. Verify replication is working initially
        2. Pause all replication agreements
        3. Make changes on supplier1
        4. Verify changes do not replicate immediately
        5. Resume replication agreements
        6. Verify changes replicate after resume
    :expectedresults:
        1. Initial replication works
        2. Agreements pause successfully
        3. Changes made successfully
        4. Changes do not replicate while paused
        5. Agreements resume successfully
        6. Changes replicate after resume
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    test_users = repl_conflict_setup['test_users']
    repl = ReplicationManager(DEFAULT_SUFFIX)

    test_user = test_users[0]
    test_uid = test_user.get_attr_val_utf8('uid')

    log.info("Verify replication is working initially")
    initial_description = 'Initial test for pause resume'
    test_user.replace('description', initial_description)
    repl.wait_for_replication(supplier1, supplier2)

    user_s2 = UserAccount(supplier2, test_user.dn)
    assert user_s2.get_attr_val_utf8('description') == initial_description

    log.info("Pause all replication agreements")
    topology_m2.pause_all_replicas()

    log.info("Make changes on supplier1")
    paused_description = 'Modified during pause test'
    test_user.replace('description', paused_description)

    log.info("Verify changes do not replicate immediately")
    user_s2_desc = user_s2.get_attr_val_utf8('description')
    assert user_s2_desc == initial_description, f"Expected '{initial_description}', got '{user_s2_desc}'"

    log.info("Resume replication agreements")
    topology_m2.resume_all_replicas()

    log.info("Verify changes replicate after resume")
    repl.wait_for_replication(supplier1, supplier2)

    final_description = user_s2.get_attr_val_utf8('description')
    assert final_description == paused_description, f"Expected '{paused_description}', got '{final_description}'"


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
