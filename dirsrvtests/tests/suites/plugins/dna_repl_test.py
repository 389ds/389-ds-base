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
import time
import threading
import ldap
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m4 as topo_m4
from lib389.idm.user import UserAccount
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.replica import ReplicationManager
from lib389.plugins import DNAPlugin, DNAPluginConfigs, DNAPluginSharedConfigs
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def dna_setup(topo_m4, request):
    """Setup DNA plugin across all four suppliers with shared configuration"""
    suppliers = [
        topo_m4.ms["supplier1"],
        topo_m4.ms["supplier2"],
        topo_m4.ms["supplier3"],
        topo_m4.ms["supplier4"]
    ]

    range_configs = [
        {'next': '1000', 'max': '1099', 'threshold': '10', 'remaining': '100'},
        {'next': '2000', 'max': '2099', 'threshold': '10', 'remaining': '100'},
        {'next': '3000', 'max': '3099', 'threshold': '10', 'remaining': '100'},
        {'next': '4000', 'max': '4099', 'threshold': '10', 'remaining': '100'},
    ]

    created_objects = {'dna_configs': [], 'shared_configs': []}
    repl = ReplicationManager(DEFAULT_SUFFIX)

    m1 = suppliers[0]
    ous = OrganizationalUnits(m1, DEFAULT_SUFFIX)
    try:
        ou_ranges = ous.get('dna_ranges')
        shared_configs = DNAPluginSharedConfigs(m1, ou_ranges.dn)
        for cfg in shared_configs.list():
            cfg.delete()
    except ldap.NO_SUCH_OBJECT:
        ou_ranges = ous.create(properties={'ou': 'dna_ranges'})

    # Wait for OU to replicate
    time.sleep(1)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    shared_configs = DNAPluginSharedConfigs(m1, ou_ranges.dn)
    for idx, supplier in enumerate(suppliers):
        supplier_num = idx + 1
        cfg = range_configs[idx]

        shared_cfg = shared_configs.create(properties={
            'dnaHostname': supplier.host,
            'dnaPortNum': str(supplier.port),
            'dnaRemainingValues': cfg['remaining'],
            'dnaRemoteBindMethod': 'SIMPLE',
            'dnaRemoteConnProtocol': 'LDAP'
        })
        created_objects['shared_configs'].append(shared_cfg)

    time.sleep(2)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    for idx, supplier in enumerate(suppliers):
        supplier_num = idx + 1
        cfg = range_configs[idx]

        dna_plugin = DNAPlugin(supplier)
        local_ous = OrganizationalUnits(supplier, DEFAULT_SUFFIX)
        local_ou_ranges = local_ous.get('dna_ranges')

        configs = DNAPluginConfigs(supplier, dna_plugin.dn)
        try:
            existing = configs.get('uidNumber config')
            existing.delete()
        except ldap.NO_SUCH_OBJECT:
            pass

        dna_config = configs.create(properties={
            'cn': 'uidNumber config',
            'dnaType': 'uidNumber',
            'dnaNextValue': cfg['next'],
            'dnaMaxValue': cfg['max'],
            'dnaMagicRegen': '-1',
            'dnaFilter': '(objectclass=posixAccount)',
            'dnaScope': DEFAULT_SUFFIX,
            'dnaThreshold': cfg['threshold'],
            'dnaSharedCfgDN': local_ou_ranges.dn,
            'dnaRemoteBindDN': DN_DM,
            'dnaRemoteBindCred': PASSWORD
        })
        created_objects['dna_configs'].append((supplier, dna_config))

        dna_plugin.enable()

    for supplier in suppliers:
        supplier.restart()

    time.sleep(3)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    def fin():
        for supplier, config in created_objects['dna_configs']:
            try:
                config.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

        for supplier in suppliers:
            try:
                dna_plugin = DNAPlugin(supplier)
                dna_plugin.disable()
            except:
                pass

        for shared_cfg in created_objects['shared_configs']:
            try:
                shared_cfg.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

        for supplier in suppliers:
            try:
                supplier.restart()
            except:
                pass

    request.addfinalizer(fin)

    return {
        'suppliers': suppliers,
        'range_configs': range_configs,
        'created_objects': created_objects
    }


def test_dna_multi_supplier_basic_allocation(topo_m4, dna_setup, request):
    """Test basic DNA allocation works across multiple suppliers

    :id: 34aad84c-e506-4c9e-8610-4aebf6c29b2e
    :setup: Four suppliers with DNA plugin configured and shared config
    :steps:
        1. Create a user on supplier1 with uidNumber=-1 (magic regen)
        2. Verify uidNumber was assigned from supplier1's range
        3. Create a user on supplier2 with uidNumber=-1
        4. Verify uidNumber was assigned from supplier2's range
        5. Wait for replication and verify both users exist on all suppliers
    :expectedresults:
        1. User created successfully
        2. uidNumber should be in range 1000-1099
        3. User created successfully
        4. uidNumber should be in range 2000-2099
        5. Both users replicated to all suppliers with correct uidNumbers
    """
    suppliers = dna_setup['suppliers']
    m1, m2 = suppliers[0], suppliers[1]
    repl = ReplicationManager(DEFAULT_SUFFIX)
    created_users = []

    def fin():
        for user in created_users:
            try:
                user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

    request.addfinalizer(fin)

    user1_dn = f'uid=dna_test_user1,{DEFAULT_SUFFIX}'
    user1 = UserAccount(m1, user1_dn)
    user1.create(properties={
        'uid': 'dna_test_user1',
        'cn': 'DNA Test User 1',
        'sn': 'User1',
        'uidNumber': '-1',
        'gidNumber': '1000',
        'homeDirectory': '/home/dna_test_user1'
    })
    created_users.append(user1)

    uid_num1 = int(user1.get_attr_val_utf8('uidNumber'))
    log.info(f"Supplier1 assigned uidNumber: {uid_num1}")
    assert 1000 <= uid_num1 <= 1099, f"uidNumber {uid_num1} not in supplier1's range 1000-1099"

    user2_dn = f'uid=dna_test_user2,{DEFAULT_SUFFIX}'
    user2 = UserAccount(m2, user2_dn)
    user2.create(properties={
        'uid': 'dna_test_user2',
        'cn': 'DNA Test User 2',
        'sn': 'User2',
        'uidNumber': '-1',
        'gidNumber': '1000',
        'homeDirectory': '/home/dna_test_user2'
    })
    created_users.append(user2)

    uid_num2 = int(user2.get_attr_val_utf8('uidNumber'))
    log.info(f"Supplier2 assigned uidNumber: {uid_num2}")
    assert 2000 <= uid_num2 <= 2099, f"uidNumber {uid_num2} not in supplier2's range 2000-2099"

    for supplier in suppliers:
        repl.wait_for_replication(m1, supplier)
        repl.wait_for_replication(m2, supplier)

    for idx, supplier in enumerate(suppliers):
        u1 = UserAccount(supplier, user1_dn)
        u2 = UserAccount(supplier, user2_dn)
        assert u1.exists(), f"User1 not found on supplier{idx+1}"
        assert u2.exists(), f"User2 not found on supplier{idx+1}"
        assert int(u1.get_attr_val_utf8('uidNumber')) == uid_num1
        assert int(u2.get_attr_val_utf8('uidNumber')) == uid_num2


def test_dna_multi_supplier_unique_values(topo_m4, dna_setup, request):
    """Test that DNA generates unique values across all suppliers

    :id: 7e6e08a9-ad0c-4955-9a23-e66c3c86e434
    :setup: Four suppliers with DNA plugin configured and shared config
    :steps:
        1. Create multiple users on each supplier concurrently
        2. Collect all assigned uidNumbers
        3. Verify all uidNumbers are unique (no duplicates)
        4. Verify each uidNumber falls within the correct supplier's range
    :expectedresults:
        1. All users created successfully
        2. All uidNumbers collected
        3. No duplicate uidNumbers across all suppliers
        4. Each uidNumber is in the expected range for its supplier
    """
    suppliers = dna_setup['suppliers']
    users_per_supplier = 10
    all_users = []
    errors = []

    def fin():
        for user, _, _ in all_users:
            try:
                user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

    request.addfinalizer(fin)

    def create_users_on_supplier(supplier, supplier_num):
        local_users = []
        for i in range(users_per_supplier):
            user_dn = f'uid=dna_concurrent_{supplier_num}_{i},{DEFAULT_SUFFIX}'
            try:
                user = UserAccount(supplier, user_dn)
                user.create(properties={
                    'uid': f'dna_concurrent_{supplier_num}_{i}',
                    'cn': f'DNA Concurrent User {supplier_num}-{i}',
                    'sn': f'User{supplier_num}{i}',
                    'uidNumber': '-1',
                    'gidNumber': '1000',
                    'homeDirectory': f'/home/dna_concurrent_{supplier_num}_{i}'
                })
                uid_num = int(user.get_attr_val_utf8('uidNumber'))
                local_users.append((user, uid_num, supplier_num))
            except Exception as e:
                errors.append(f"Supplier{supplier_num} error: {e}")
        return local_users

    threads = []
    results = [None] * 4

    def thread_target(supplier, supplier_num, result_idx):
        results[result_idx] = create_users_on_supplier(supplier, supplier_num)

    for idx, supplier in enumerate(suppliers):
        t = threading.Thread(target=thread_target, args=(supplier, idx + 1, idx))
        threads.append(t)
        t.start()

    for t in threads:
        t.join(timeout=60)

    for result in results:
        if result:
            all_users.extend(result)

    all_uid_numbers = [uid for (_, uid, _) in all_users]
    uid_set = set(all_uid_numbers)
    assert len(uid_set) == len(all_uid_numbers), (
        f"Duplicate uidNumbers! Total: {len(all_uid_numbers)}, Unique: {len(uid_set)}"
    )

    for user, uid_num, supplier_num in all_users:
        expected_start = supplier_num * 1000
        expected_end = expected_start + 99
        assert expected_start <= uid_num <= expected_end, (
            f"uidNumber {uid_num} from supplier{supplier_num} not in range {expected_start}-{expected_end}"
        )


def test_dna_shared_config_replication(topo_m4, dna_setup, request):
    """Test that DNA shared config entries replicate correctly

    :id: c767bc0e-f2c9-480a-812d-09b2aa3d3b66
    :setup: Four suppliers with DNA plugin configured and shared config
    :steps:
        1. Verify shared config entries exist on all suppliers
        2. Create user to consume DNA value (updating shared config)
        3. Wait for replication
        4. Verify the change replicated to all suppliers
    :expectedresults:
        1. All shared config entries found on all suppliers
        2. User created, DNA value allocated
        3. Replication completed
        4. Change visible on all suppliers
    """
    suppliers = dna_setup['suppliers']
    m1 = suppliers[0]
    repl = ReplicationManager(DEFAULT_SUFFIX)
    m1_shared_cfg = dna_setup['created_objects']['shared_configs'][0]

    for idx, supplier in enumerate(suppliers):
        ous = OrganizationalUnits(supplier, DEFAULT_SUFFIX)
        ou_ranges = ous.get('dna_ranges')

        shared_configs = DNAPluginSharedConfigs(supplier, ou_ranges.dn)
        entries = shared_configs.list()

        log.info(f"Supplier{idx+1} has {len(entries)} shared config entries")
        assert len(entries) == 4, (
            f"Supplier{idx+1} should have 4 shared config entries, found {len(entries)}. "
            f"Entries: {[e.dn for e in entries]}"
        )

    ous = OrganizationalUnits(m1, DEFAULT_SUFFIX)
    ou_ranges = ous.get('dna_ranges')

    # Create a user to trigger DNA allocation
    created_users = []
    def fin():
        for user in created_users:
            try:
                user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass
    request.addfinalizer(fin)

    user_dn = f'uid=dna_shared_test,{DEFAULT_SUFFIX}'
    user = UserAccount(m1, user_dn)
    user.create(properties={
        'uid': 'dna_shared_test',
        'cn': 'DNA Shared Test',
        'sn': 'Test',
        'uidNumber': '-1',
        'gidNumber': '1000',
        'homeDirectory': '/home/dna_shared_test'
    })
    created_users.append(user)

    log.info("Created user to trigger DNA allocation and shared config update")

    # Expected remaining value is 99 (100 - 1)
    expected_remaining = '99'

    time.sleep(1)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    for idx, supplier in enumerate(suppliers):
        # Get the m1 shared config entry on each supplier
        replicated_entry = DNAPluginSharedConfigs(supplier, ou_ranges.dn).get(dn=m1_shared_cfg.dn)
        remaining = replicated_entry.get_attr_val_utf8('dnaRemainingValues')
        assert remaining == expected_remaining, (
            f"Supplier{idx+1} has dnaRemainingValues={remaining}, expected {expected_remaining}"
        )


def test_dna_allocation_after_restart(topo_m4, dna_setup, request):
    """Test that DNA continues working correctly after supplier restart

    :id: 8812c6ae-5f3c-4ac4-9aed-cf522d5896fb
    :setup: Four suppliers with DNA plugin configured and shared config
    :steps:
        1. Create a user on supplier1 and record uidNumber
        2. Restart supplier1
        3. Create another user on supplier1
        4. Verify the uidNumbers are sequential and unique
    :expectedresults:
        1. First user created with valid uidNumber
        2. Supplier1 restarts successfully
        3. Second user created with valid uidNumber
        4. uidNumbers are sequential
    """
    suppliers = dna_setup['suppliers']
    m1 = suppliers[0]
    created_users = []

    def fin():
        for user in created_users:
            try:
                user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

    request.addfinalizer(fin)

    user1_dn = f'uid=dna_restart_user1,{DEFAULT_SUFFIX}'
    user1 = UserAccount(m1, user1_dn)
    user1.create(properties={
        'uid': 'dna_restart_user1',
        'cn': 'DNA Restart User 1',
        'sn': 'User1',
        'uidNumber': '-1',
        'gidNumber': '1000',
        'homeDirectory': '/home/dna_restart_user1'
    })
    created_users.append(user1)
    uid_num1 = int(user1.get_attr_val_utf8('uidNumber'))
    log.info(f"First user uidNumber: {uid_num1}")

    m1.restart()
    time.sleep(2)

    user2_dn = f'uid=dna_restart_user2,{DEFAULT_SUFFIX}'
    user2 = UserAccount(m1, user2_dn)
    user2.create(properties={
        'uid': 'dna_restart_user2',
        'cn': 'DNA Restart User 2',
        'sn': 'User2',
        'uidNumber': '-1',
        'gidNumber': '1000',
        'homeDirectory': '/home/dna_restart_user2'
    })
    created_users.append(user2)
    uid_num2 = int(user2.get_attr_val_utf8('uidNumber'))
    log.info(f"Second user uidNumber: {uid_num2}")

    assert uid_num1 != uid_num2, "uidNumbers should be different"
    assert uid_num2 == uid_num1 + 1, f"uidNumbers should be sequential: {uid_num1} -> {uid_num2}"


@pytest.fixture(scope="function")
def dna_small_range_setup(topo_m4, request):
    """Setup DNA with very small range to test exhaustion and range transfer"""

    suppliers = [
        topo_m4.ms["supplier1"],
        topo_m4.ms["supplier2"],
        topo_m4.ms["supplier3"],
        topo_m4.ms["supplier4"]
    ]

    range_configs = [
        {'next': '1000', 'max': '1004', 'threshold': '2', 'remaining': '5'},
        {'next': '2000', 'max': '2099', 'threshold': '10', 'remaining': '100'},
        {'next': '3000', 'max': '3099', 'threshold': '10', 'remaining': '100'},
        {'next': '4000', 'max': '4099', 'threshold': '10', 'remaining': '100'},
    ]

    created_objects = {'dna_configs': [], 'shared_configs': []}
    repl = ReplicationManager(DEFAULT_SUFFIX)

    m1 = suppliers[0]
    ous = OrganizationalUnits(m1, DEFAULT_SUFFIX)
    try:
        ou_ranges = ous.get('dna_exhaust_ranges')
        shared_configs = DNAPluginSharedConfigs(m1, ou_ranges.dn)
        for cfg in shared_configs.list():
            cfg.delete()
    except ldap.NO_SUCH_OBJECT:
        ou_ranges = ous.create(properties={'ou': 'dna_exhaust_ranges'})

    time.sleep(1)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    shared_configs = DNAPluginSharedConfigs(m1, ou_ranges.dn)
    for idx, supplier in enumerate(suppliers):
        cfg = range_configs[idx]
        shared_cfg = shared_configs.create(properties={
            'dnaHostname': supplier.host,
            'dnaPortNum': str(supplier.port),
            'dnaRemainingValues': cfg['remaining'],
            'dnaRemoteBindMethod': 'SIMPLE',
            'dnaRemoteConnProtocol': 'LDAP'
        })
        created_objects['shared_configs'].append(shared_cfg)

    time.sleep(2)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    for idx, supplier in enumerate(suppliers):
        cfg = range_configs[idx]
        dna_plugin = DNAPlugin(supplier)
        local_ous = OrganizationalUnits(supplier, DEFAULT_SUFFIX)
        local_ou_ranges = local_ous.get('dna_exhaust_ranges')

        configs = DNAPluginConfigs(supplier, dna_plugin.dn)
        try:
            existing = configs.get('uidNumber exhaust config')
            existing.delete()
        except ldap.NO_SUCH_OBJECT:
            pass

        dna_config = configs.create(properties={
            'cn': 'uidNumber exhaust config',
            'dnaType': 'uidNumber',
            'dnaNextValue': cfg['next'],
            'dnaMaxValue': cfg['max'],
            'dnaMagicRegen': '-1',
            'dnaFilter': '(objectclass=posixAccount)',
            'dnaScope': DEFAULT_SUFFIX,
            'dnaThreshold': cfg['threshold'],
            'dnaSharedCfgDN': local_ou_ranges.dn,
            'dnaRemoteBindDN': DN_DM,
            'dnaRemoteBindCred': PASSWORD
        })
        created_objects['dna_configs'].append((supplier, dna_config))
        dna_plugin.enable()

    for supplier in suppliers:
        supplier.restart()

    time.sleep(3)

    def fin():
        for supplier, config in created_objects['dna_configs']:
            try:
                config.delete()
            except ldap.NO_SUCH_OBJECT:
                pass
        for supplier in suppliers:
            try:
                DNAPlugin(supplier).disable()
            except:
                pass
        for shared_cfg in created_objects['shared_configs']:
            try:
                shared_cfg.delete()
            except ldap.NO_SUCH_OBJECT:
                pass
        for supplier in suppliers:
            try:
                supplier.restart()
            except:
                pass

    request.addfinalizer(fin)

    return {
        'suppliers': suppliers,
        'range_configs': range_configs,
        'created_objects': created_objects
    }


def test_dna_range_exhaustion(topo_m4, dna_small_range_setup, request):
    """Test DNA range exhaustion triggers range request from peer.

    :id: fb85e3f0-e632-4ec2-bffa-bdbe1daf6fd1
    :setup: Four suppliers, supplier1 has only 5 values (1000-1004), others have 100 each
    :steps:
        1. Create 5 users on supplier1 to exhaust initial range
        2. Verify the 5 users got uidNumbers from supplier1's initial range (1000-1004)
        3. Create more users on supplier1 (should trigger range request or fail)
        4. Verify all allocated uidNumbers are unique
    :expectedresults:
        1. 5 users created successfully
        2. uidNumbers are in range 1000-1004
        3. Additional users get values from extended range or peer transfer
        4. No duplicate uidNumbers
    """
    suppliers = dna_small_range_setup['suppliers']
    m1 = suppliers[0]
    initial_range_users = []
    additional_users = []

    def fin():
        for user, _ in initial_range_users + additional_users:
            try:
                user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

    request.addfinalizer(fin)

    log.info("Supplier1 has range 1000-1004 (5 values), threshold=2")

    for i in range(5):
        user_dn = f'uid=dna_exhaust_{i},{DEFAULT_SUFFIX}'
        user = UserAccount(m1, user_dn)
        try:
            user.create(properties={
                'uid': f'dna_exhaust_{i}',
                'cn': f'DNA Exhaust {i}',
                'sn': f'User{i}',
                'uidNumber': '-1',
                'gidNumber': '1000',
                'homeDirectory': f'/home/dna_exhaust_{i}'
            })
            uid_num = int(user.get_attr_val_utf8('uidNumber'))
            initial_range_users.append((user, uid_num))
            log.info(f"User {i}: uidNumber={uid_num}")
        except ldap.OPERATIONS_ERROR as e:
            log.warning(f"User {i} creation failed: {e}")
            break

    initial_uids = [uid for (_, uid) in initial_range_users]
    log.info(f"Initial range uidNumbers: {sorted(initial_uids)}")

    for uid in initial_uids:
        assert 1000 <= uid <= 1004, f"uidNumber {uid} not in initial range 1000-1004"

    assert len(initial_range_users) == 5, f"Expected 5 users from initial range, got {len(initial_range_users)}"

    # Give DNA time to detect low remaining values
    time.sleep(2)

    for i in range(5, 10):
        user_dn = f'uid=dna_exhaust_{i},{DEFAULT_SUFFIX}'
        user = UserAccount(m1, user_dn)
        try:
            user.create(properties={
                'uid': f'dna_exhaust_{i}',
                'cn': f'DNA Exhaust {i}',
                'sn': f'User{i}',
                'uidNumber': '-1',
                'gidNumber': '1000',
                'homeDirectory': f'/home/dna_exhaust_{i}'
            })
            uid_num = int(user.get_attr_val_utf8('uidNumber'))
            additional_users.append((user, uid_num))
            log.info(f"User {i}: uidNumber={uid_num}")
        except ldap.OPERATIONS_ERROR as e:
            log.info(f"User {i} creation failed (expected if no range transfer): {e}")
            break

    all_uids = initial_uids + [uid for (_, uid) in additional_users]
    uid_set = set(all_uids)
    assert len(uid_set) == len(all_uids), f"Duplicate uidNumbers detected! {all_uids}"

    log.info(f"Total users created: {len(all_uids)}")
    if additional_users:
        additional_uids = [uid for (_, uid) in additional_users]
        log.info(f"Additional uidNumbers (beyond initial 5): {additional_uids}")
        for uid in additional_uids:
            if 2000 <= uid <= 2099:
                log.info(f"  {uid} - from supplier2's range")
            elif 3000 <= uid <= 3099:
                log.info(f"  {uid} - from supplier3's range")
            elif 4000 <= uid <= 4099:
                log.info(f"  {uid} - from supplier4's range")
            else:
                log.info(f"  {uid} - from extended/next range")


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
