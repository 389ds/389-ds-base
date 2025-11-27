# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""
DNA Plugin Stress Test with MemberOf Fixup Race Condition Testing

This module contains a single comprehensive stress test for DNA plugin
in a multi-supplier replication topology with concurrent MemberOf fixup
tasks. The test targets race conditions in dna_load_shared_servers().

Configure via environment variables:
    DNA_STRESS_DURATION      - Test duration in seconds (default: 720)
    DNA_STRESS_NUM_GROUPS    - Number of groups for memberOf load (default: 100)
    DNA_STRESS_INITIAL_USERS - Users to pre-create before test (default: 10000)

Example:
    DNA_STRESS_DURATION=1200 DNA_STRESS_NUM_GROUPS=200 pytest -s -k test_dna_stress
"""
import pytest
import logging
import time
import threading
import ldap
import uuid
import random
import os
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_i4
from lib389.dbgen import dbgen_users
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.replica import ReplicationManager
from lib389.plugins import DNAPlugin, DNAPluginConfigs, DNAPluginSharedConfigs, MemberOfPlugin
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD

pytestmark = pytest.mark.tier3

log = logging.getLogger(__name__)

run_duration = int(os.environ.get('DNA_STRESS_DURATION', '720'))
num_groups = int(os.environ.get('DNA_STRESS_NUM_GROUPS', '100'))
initial_users = int(os.environ.get('DNA_STRESS_INITIAL_USERS', '10000'))


@pytest.fixture(scope="module")
def topology_i4_with_repl(topology_i4, request):
    """Import users on topology_i4, then set up replication.

    This fixture takes the 4 standalone instances from topology_i4,
    imports initial users via dbgen on the first instance, then sets up
    multi-supplier replication. This ensures replication is configured
    after the import, avoiding the issue of import destroying replication
    service accounts.
    """
    topo = topology_i4

    # Get instances list - they are standalone1-4
    instances = [topo.ins[f"standalone{i}"] for i in range(1, 5)]
    inst1 = instances[0]

    # Step 1: Import initial users via dbgen on first instance (before replication)
    initial_users = int(os.environ.get('DNA_STRESS_INITIAL_USERS', '10000'))
    log.info(f"Importing {initial_users} users via dbgen on standalone1...")

    ldif_dir = inst1.get_ldif_dir()
    import_ldif = f'{ldif_dir}/dna_stress_users.ldif'

    # Generate LDIF with generic users (uidNumber based on index, outside DNA ranges)
    dbgen_users(inst1, initial_users, import_ldif, DEFAULT_SUFFIX, generic=True)

    # Import users via online import task
    import_task = ImportTask(inst1)
    import_task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait(timeout=600)
    log.info(f"Import completed with exit code: {import_task.get_exit_code()}")

    # Clean up LDIF file
    if os.path.exists(import_ldif):
        os.remove(import_ldif)

    # Step 2: Set up replication AFTER import
    log.info("Setting up replication topology...")
    repl = ReplicationManager(DEFAULT_SUFFIX)

    # Create first supplier
    repl.create_first_supplier(inst1)

    # Join other instances as suppliers
    for inst in instances[1:]:
        log.info(f"Joining {inst.serverid} to {inst1.serverid}...")
        repl.join_supplier(inst1, inst)

    # Mesh all supplier agreements
    for inst_from in instances:
        for inst_to in instances:
            if inst_from is inst_to:
                continue
            log.info(f"Ensuring agreement {inst_from.serverid} -> {inst_to.serverid}...")
            repl.ensure_agreement(inst_from, inst_to)

    # Wait for replication to settle
    time.sleep(3)
    for inst in instances[1:]:
        repl.wait_for_replication(inst1, inst)

    log.info("Replication topology setup complete")

    # Store instances list for easy access (now they act as suppliers)
    topo.suppliers = instances

    return topo


@pytest.fixture(scope="function")
def dna_stress_setup(topology_i4_with_repl, request):
    """Setup DNA and MemberOf plugins for continuous stress testing.

    Creates:
    - Shared config entries on first instance that replicate to all instances
    - DNA plugin configs on each instance with distinct ranges
    - MemberOf plugin enabled on all instances
    - Configurable number of groups for memberOf fixup load
    """
    topo = topology_i4_with_repl
    suppliers = topo.suppliers  # 4 instances now acting as suppliers

    # Each supplier gets a distinct range
    # Instances 1-3: tight ranges (2510) - just enough for initial users, forces exhaustion
    # Instance 4: larger range (20000) - backup pool for range requests
    # Low threshold triggers more shared config updates
    range_configs = [
        {'next': '100000', 'max': '102509', 'threshold': '200', 'remaining': '2510'},
        {'next': '200000', 'max': '202509', 'threshold': '200', 'remaining': '2510'},
        {'next': '300000', 'max': '302509', 'threshold': '200', 'remaining': '2510'},
        {'next': '400000', 'max': '419999', 'threshold': '200', 'remaining': '20000'},
    ]

    created_objects = {'dna_configs': [], 'shared_configs': [], 'groups': [], 'users': []}
    repl = ReplicationManager(DEFAULT_SUFFIX)

    m1 = suppliers[0]
    ous = OrganizationalUnits(m1, DEFAULT_SUFFIX)

    # Get imported users and add them to tracking list
    log.info("Collecting imported users...")
    imported_users = UserAccounts(m1, DEFAULT_SUFFIX)
    user_count = 0
    for user in imported_users.list():
        if user.rdn.startswith('uid=user'):
            created_objects['users'].append(user)
            user_count += 1

    log.info(f"Found {user_count} imported users")

    # Create shared config container on supplier1
    try:
        ou_ranges = ous.get('dna_stress_ranges')
        shared_configs = DNAPluginSharedConfigs(m1, ou_ranges.dn)
        for cfg in shared_configs.list():
            cfg.delete()
    except ldap.NO_SUCH_OBJECT:
        ou_ranges = ous.create(properties={'ou': 'dna_stress_ranges'})

    # Wait for OU to replicate
    time.sleep(1)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    # Create shared config entries for all suppliers on supplier1
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
        log.info(f"Created shared config for {supplier.serverid}: {supplier.host}:{supplier.port}")

    # Wait for shared configs to replicate
    log.info("Waiting for shared configs to replicate...")
    time.sleep(2)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    # Configure DNA and MemberOf plugins on each supplier
    for idx, supplier in enumerate(suppliers):
        cfg = range_configs[idx]

        dna_plugin = DNAPlugin(supplier)
        local_ous = OrganizationalUnits(supplier, DEFAULT_SUFFIX)
        local_ou_ranges = local_ous.get('dna_stress_ranges')

        configs = DNAPluginConfigs(supplier, dna_plugin.dn)
        try:
            existing = configs.get('stress uidNumber config')
            existing.delete()
        except ldap.NO_SUCH_OBJECT:
            pass

        dna_config = configs.create(properties={
            'cn': 'stress uidNumber config',
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
        log.info(f"Configured DNA on {supplier.serverid}: range {cfg['next']}-{cfg['max']}")

        dna_plugin.enable()

        # Configure MemberOf plugin
        memberof_plugin = MemberOfPlugin(supplier)
        memberof_plugin.enable()
        memberof_plugin.set_autoaddoc('nsMemberOf')

    # Restart all suppliers to activate plugins
    for supplier in suppliers:
        supplier.restart()

    time.sleep(3)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    # Create groups for memberOf fixup load
    log.info(f"Creating {num_groups} groups for fixup stress testing...")
    groups = Groups(m1, DEFAULT_SUFFIX)
    for i in range(num_groups):
        try:
            g = groups.get(f'stressgroup{i}')
            g.delete()
        except ldap.NO_SUCH_OBJECT:
            pass
        g = groups.create(properties={'cn': f'stressgroup{i}'})
        created_objects['groups'].append(g)

    # Create parent groups for nested memberOf (more complex hierarchy)
    num_parent_groups = num_groups // 5  # 10 parent groups for 50 child groups
    log.info(f"Creating {num_parent_groups} parent groups for nested memberOf...")
    for i in range(num_parent_groups):
        try:
            pg = groups.get(f'parentgroup{i}')
            pg.delete()
        except ldap.NO_SUCH_OBJECT:
            pass
        pg = groups.create(properties={'cn': f'parentgroup{i}'})
        created_objects['groups'].append(pg)

        # Add 5 child groups to each parent group
        start_idx = i * 5
        end_idx = min(start_idx + 5, num_groups)
        for child_idx in range(start_idx, end_idx):
            try:
                child_group = created_objects['groups'][child_idx]
                pg.add_member(child_group.dn)
            except ldap.LDAPError:
                pass

    # Wait for groups to replicate
    time.sleep(2)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    # Add users to groups for memberOf load
    log.info("Adding users to groups...")
    m1_groups = Groups(m1, DEFAULT_SUFFIX)
    for user in created_objects['users']:
        num_groups_to_add = min(random.randint(6, 16), len(created_objects['groups']))
        for g in random.sample(created_objects['groups'], num_groups_to_add):
            try:
                local_g = m1_groups.get(g.rdn)
                local_g.add_member(user.dn)
            except ldap.LDAPError:
                pass

    # Wait for group memberships to replicate
    time.sleep(3)
    for supplier in suppliers[1:]:
        repl.wait_for_replication(m1, supplier)

    log.info(f"Setup complete: {len(created_objects['users'])} initial users, "
             f"{len(created_objects['groups'])} groups, "
             f"{len(created_objects['shared_configs'])} shared configs")

    def fin():
        for supplier, config in created_objects['dna_configs']:
            try:
                config.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

        for supplier in suppliers:
            try:
                DNAPlugin(supplier).disable()
                MemberOfPlugin(supplier).disable()
            except Exception:
                pass

        for shared_cfg in created_objects['shared_configs']:
            try:
                shared_cfg.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

        for group in created_objects['groups']:
            try:
                group.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

        for supplier in suppliers:
            try:
                supplier.restart()
            except Exception:
                pass

    request.addfinalizer(fin)

    return {
        'suppliers': suppliers,
        'range_configs': range_configs,
        'created_objects': created_objects,
        'ou_ranges': ou_ranges,
        'num_groups': num_groups,
        'initial_users': initial_users
    }


def test_dna_stress(topology_i4_with_repl, dna_stress_setup, request):
    """Continuous stress test for DNA and MemberOf race conditions.

    :id: 096b437e-610e-45d5-b129-c6152ab14148
    :setup: Four suppliers with DNA and MemberOf plugins, 10000+ initial users,
            configurable groups
    :steps:
        1. Pre-create 10000+ users across all suppliers with group memberships
        2. Start continuous worker threads:
           - User creation on rotating suppliers (DNA range jumping)
           - DNA shared config add/delete cycles
           - DNA shared config value modifications
           - MemberOf fixup tasks on each supplier
           - Group membership modifications
           - Group create/delete cycles
        3. Monitor server health throughout test duration
        4. Verify all suppliers remain stable
        5. Analyze DNA range distribution
    :expectedresults:
        1. Initial users created with DNA-assigned uidNumbers
        2. All workers run continuously until stop signal
        3. No server crashes or unresponsive states
        4. All suppliers responsive after test
        5. DNA values assigned from multiple supplier ranges
    """
    suppliers = dna_stress_setup['suppliers']
    created_objects = dna_stress_setup['created_objects']
    ou_ranges = dna_stress_setup['ou_ranges']
    num_groups = dna_stress_setup['num_groups']
    initial_users_count = dna_stress_setup['initial_users']
    test_groups = created_objects['groups']
    pre_created_users = created_objects['users']
    repl = ReplicationManager(DEFAULT_SUFFIX)

    m1 = suppliers[0]

    # Shared state - start with initial users already available
    stop_event = threading.Event()
    server_dead = threading.Event()
    created_users = list(pre_created_users)  # Start with pre-created users
    users_lock = threading.Lock()
    created_groups = list(test_groups)
    groups_lock = threading.Lock()

    # Metrics - count initial users
    stats = {
        'users_created': len(pre_created_users),  # Start with initial count
        'shared_config_ops': 0,
        'group_membership_ops': 0,
        'fixup_tasks': 0,
        'groups_churned': 0,
    }
    stats_lock = threading.Lock()
    uid_numbers_by_range = {0: [], 1: [], 2: [], 3: []}
    uid_lock = threading.Lock()

    def get_range_index(uid_num):
        """Determine which supplier range a uidNumber belongs to"""
        uid = int(uid_num)
        if 100000 <= uid < 200000:
            return 0
        elif 200000 <= uid < 300000:
            return 1
        elif 300000 <= uid < 400000:
            return 2
        elif 400000 <= uid < 500000:
            return 3
        return -1

    def check_server_health(supplier):
        """Check if server process is running and responsive"""
        try:
            # First check if process is running
            if not supplier.status():
                return False
            # Then check LDAP responsiveness
            supplier.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE, "(objectclass=*)", ['dn'])
            return True
        except Exception:
            return False

    # Track initial users' UID ranges
    for user in pre_created_users:
        try:
            uid_num = user.get_attr_val_utf8('uidNumber')
            range_idx = get_range_index(int(uid_num))
            if range_idx >= 0:
                uid_numbers_by_range[range_idx].append(int(uid_num))
        except Exception:
            pass

    log.info(f"Starting with {len(pre_created_users)} pre-created users")

    def fin():
        stop_event.set()
        time.sleep(2)
        with users_lock:
            for user in created_users:
                if user not in pre_created_users:
                    try:
                        user.delete()
                    except Exception:
                        pass
        with groups_lock:
            for g in created_groups:
                if g not in test_groups:
                    try:
                        g.delete()
                    except Exception:
                        pass

    request.addfinalizer(fin)

    # Lock to prevent multiple threads from restarting the same supplier
    restart_locks = [threading.Lock() for _ in suppliers]
    # Track restart counts per supplier
    restart_counts = [0] * len(suppliers)
    restart_counts_lock = threading.Lock()

    # === WORKER THREADS ===

    def create_users_continuous(thread_id):
        """Continuously create users on rotating suppliers for DNA range jumping.

        Each user is added to multiple groups for heavy memberOf load.
        Periodically restarts suppliers to interrupt memberOf operations.
        Runs until stop_event is set.
        """
        user_counter = 0
        restart_interval = 10  # Restart supplier every N users created by this thread
        log.info(f"UserCreate-{thread_id}: Starting continuous user creation")

        while not stop_event.is_set() and not server_dead.is_set():
            # Rotate through suppliers for DNA range jumping
            supplier_idx = (thread_id + user_counter) % len(suppliers)
            supplier = suppliers[supplier_idx]

            try:
                users = UserAccounts(supplier, DEFAULT_SUFFIX)
                local_groups = Groups(supplier, DEFAULT_SUFFIX)

                unique_id = f"stress-{thread_id}-{uuid.uuid4().hex[:8]}"
                user = users.create(properties={
                    'uid': f'stress_{unique_id}',
                    'cn': f'Stress User {unique_id}',
                    'sn': f'Stress{thread_id}',
                    'uidNumber': '-1',
                    'gidNumber': '1000',
                    'homeDirectory': f'/home/stress_{unique_id}',
                })

                uid_num = user.get_attr_val_utf8('uidNumber')
                range_idx = get_range_index(uid_num)

                with users_lock:
                    created_users.append(user)
                with uid_lock:
                    if range_idx >= 0:
                        uid_numbers_by_range[range_idx].append(int(uid_num))
                with stats_lock:
                    stats['users_created'] += 1

                user_counter += 1

                # Add user to multiple groups for memberOf load
                with groups_lock:
                    if created_groups:
                        num_to_add = min(random.randint(6, 10), len(created_groups))
                        for g in random.sample(created_groups, num_to_add):
                            try:
                                local_g = local_groups.get(g.rdn)
                                local_g.add_member(user.dn)
                            except ldap.LDAPError:
                                pass

                # Periodic restart to interrupt memberOf operations
                if user_counter % restart_interval == 0:
                    # Try to acquire lock - only one thread restarts a supplier at a time
                    if restart_locks[supplier_idx].acquire(blocking=False):
                        try:
                            log.info(f"UserCreate-{thread_id}: Restarting {supplier.serverid} "
                                    f"to interrupt memberOf...")
                            supplier.restart()
                            with restart_counts_lock:
                                restart_counts[supplier_idx] += 1
                            time.sleep(2)  # Wait for server to stabilize
                        except Exception:
                            pass
                        finally:
                            restart_locks[supplier_idx].release()

                # Small delay to prevent overwhelming the server
                time.sleep(0.05)

            except ldap.SERVER_DOWN:
                # Server might be restarting - wait and retry
                time.sleep(2)
                if not check_server_health(supplier):
                    log.warning(f"UserCreate-{thread_id}: {supplier.serverid} appears dead")
                    server_dead.set()
                    break
                # Server came back up, continue with loop
            except ldap.LDAPError:
                time.sleep(0.1)
            except Exception:
                time.sleep(0.1)

        log.info(f"UserCreate-{thread_id}: Finished, created {user_counter} users")

    def add_delete_shared_configs(thread_id):
        """Continuously add/delete DNA shared config entries.

        Triggers dna_load_shared_servers() on all suppliers when replicated.
        Runs until stop_event is set.
        """
        op_count = 0
        log.info(f"SharedConfig-{thread_id}: Starting continuous add/delete")
        local_shared_configs = DNAPluginSharedConfigs(m1, ou_ranges.dn)

        while not stop_event.is_set() and not server_dead.is_set():
            try:
                new_config = local_shared_configs.create(properties={
                    'dnaHostname': f'stress-{thread_id}-{uuid.uuid4().hex[:6]}.example.com',
                    'dnaPortNum': str(30000 + random.randint(0, 9999)),
                    'dnaRemainingValues': str(random.randint(100, 50000)),
                    'dnaRemoteBindMethod': 'SIMPLE',
                    'dnaRemoteConnProtocol': 'LDAP'
                })
                time.sleep(0.02)  # Brief delay for replication
                new_config.delete()
                op_count += 1
                with stats_lock:
                    stats['shared_config_ops'] += 2

            except ldap.SERVER_DOWN:
                server_dead.set()
                break
            except ldap.LDAPError:
                pass
            except Exception:
                pass

            time.sleep(0.05)

        log.info(f"SharedConfig-{thread_id}: Finished, {op_count} add/delete cycles")

    def modify_shared_configs(thread_id):
        """Continuously modify DNA shared config entries.

        Triggers dna_load_shared_servers() on all suppliers.
        Runs until stop_event is set.
        """
        op_count = 0
        log.info(f"ModifyConfig-{thread_id}: Starting continuous modifications")
        targets = created_objects['shared_configs']
        local_shared_configs = DNAPluginSharedConfigs(m1, ou_ranges.dn)

        while not stop_event.is_set() and not server_dead.is_set():
            try:
                target = random.choice(targets)
                local_target = local_shared_configs.get(dn=target.dn)
                local_target.replace('dnaRemainingValues', str(random.randint(1000, 100000)))
                op_count += 1
                with stats_lock:
                    stats['shared_config_ops'] += 1

            except ldap.SERVER_DOWN:
                server_dead.set()
                break
            except ldap.LDAPError:
                pass
            except Exception:
                pass

            time.sleep(0.02)

        log.info(f"ModifyConfig-{thread_id}: Finished, {op_count} modifications")

    def run_memberof_fixup(thread_id):
        """Run MemberOf fixup tasks one at a time, waiting for completion.

        These are INTERNAL ops that bypass dna_config_check_post_op(),
        creating contention with external DNA config changes.
        Runs until stop_event is set.
        """
        fixup_count = 0
        supplier_idx = thread_id % len(suppliers)
        supplier = suppliers[supplier_idx]
        memberof_plugin = MemberOfPlugin(supplier)
        log.info(f"Fixup-{thread_id}: Starting fixup on {supplier.serverid}")

        while not stop_event.is_set() and not server_dead.is_set():
            try:
                task = memberof_plugin.fixup(DEFAULT_SUFFIX)
                # Wait for task to complete before starting another
                task.wait(timeout=120)
                fixup_count += 1
                with stats_lock:
                    stats['fixup_tasks'] += 1

            except ldap.SERVER_DOWN:
                server_dead.set()
                break
            except ldap.UNWILLING_TO_PERFORM:
                # Another fixup running - wait and retry
                time.sleep(1)
            except ldap.LDAPError:
                time.sleep(0.5)
            except Exception:
                time.sleep(0.5)

        log.info(f"Fixup-{thread_id}: Finished, {fixup_count} fixup tasks")

    def modify_group_membership(thread_id):
        """Continuously modify group memberships.

        Creates constant memberOf updates that contend with DNA operations.
        Runs until stop_event is set.
        """
        op_count = 0
        supplier_idx = thread_id % len(suppliers)
        supplier = suppliers[supplier_idx]
        log.info(f"GroupMod-{thread_id}: Starting continuous membership modifications")

        while not stop_event.is_set() and not server_dead.is_set():
            try:
                with users_lock:
                    if not created_users:
                        time.sleep(0.1)
                        continue
                    user = random.choice(created_users)
                    user_dn = user.dn

                with groups_lock:
                    if not created_groups:
                        time.sleep(0.1)
                        continue
                    groups_to_mod = random.sample(created_groups, min(6, len(created_groups)))

                local_groups = Groups(supplier, DEFAULT_SUFFIX)
                for group in groups_to_mod:
                    try:
                        local_group = local_groups.get(group.rdn)
                        if random.random() > 0.5:
                            try:
                                local_group.add_member(user_dn)
                            except ldap.TYPE_OR_VALUE_EXISTS:
                                local_group.remove_member(user_dn)
                        else:
                            try:
                                local_group.remove_member(user_dn)
                            except ldap.NO_SUCH_ATTRIBUTE:
                                local_group.add_member(user_dn)
                        op_count += 1
                        with stats_lock:
                            stats['group_membership_ops'] += 1
                    except ldap.LDAPError:
                        pass

            except ldap.SERVER_DOWN:
                server_dead.set()
                break
            except ldap.LDAPError:
                pass
            except Exception:
                pass

            time.sleep(0.02)

        log.info(f"GroupMod-{thread_id}: Finished, {op_count} membership operations")

    def create_delete_groups(thread_id):
        """Continuously create and delete groups with members.

        Stresses MemberOf with group lifecycle operations.
        Runs until stop_event is set.
        """
        churn_count = 0
        supplier_idx = thread_id % len(suppliers)
        supplier = suppliers[supplier_idx]
        thread_groups = Groups(supplier, DEFAULT_SUFFIX)
        log.info(f"GroupChurn-{thread_id}: Starting continuous group churn")

        while not stop_event.is_set() and not server_dead.is_set():
            try:
                group_name = f'churn-{thread_id}-{uuid.uuid4().hex[:6]}'
                new_group = thread_groups.create(properties={'cn': group_name})
                with groups_lock:
                    created_groups.append(new_group)

                # Add some users to this group
                with users_lock:
                    if created_users:
                        num_to_add = min(10, len(created_users))
                        for user in random.sample(created_users, num_to_add):
                            try:
                                new_group.add_member(user.dn)
                            except ldap.LDAPError:
                                pass

                time.sleep(0.3)

                with groups_lock:
                    if new_group in created_groups:
                        created_groups.remove(new_group)
                new_group.delete()
                churn_count += 1
                with stats_lock:
                    stats['groups_churned'] += 1

            except ldap.SERVER_DOWN:
                server_dead.set()
                break
            except ldap.LDAPError:
                pass
            except Exception:
                pass

            time.sleep(0.1)

        log.info(f"GroupChurn-{thread_id}: Finished, {churn_count} groups churned")

    # === BUILD AND START THREADS ===

    threads = []

    # User creation threads - two per instance for DNA range distribution
    for i in range(8):
        t = threading.Thread(target=create_users_continuous, args=(i,),
                            name=f"UserCreate-{i}", daemon=True)
        threads.append(t)

    # DNA shared config modification threads
    for i in range(6):
        t = threading.Thread(target=add_delete_shared_configs, args=(i,),
                            name=f"SharedConfig-{i}", daemon=True)
        threads.append(t)

    for i in range(6):
        t = threading.Thread(target=modify_shared_configs, args=(i,),
                            name=f"ModifyConfig-{i}", daemon=True)
        threads.append(t)

    # MemberOf fixup threads - one per two instances
    for i in range(2):
        t = threading.Thread(target=run_memberof_fixup, args=(i,),
                            name=f"Fixup-{i}", daemon=True)
        threads.append(t)

    # Group membership modification threads
    for i in range(8):
        t = threading.Thread(target=modify_group_membership, args=(i,),
                            name=f"GroupMod-{i}", daemon=True)
        threads.append(t)

    # Group churn threads
    for i in range(4):
        t = threading.Thread(target=create_delete_groups, args=(i,),
                            name=f"GroupChurn-{i}", daemon=True)
        threads.append(t)

    log.info(f"=== DNA STRESS TEST ===")
    log.info(f"Duration: {run_duration}s, Initial Users: {len(pre_created_users)}, "
             f"Groups: {num_groups}, Threads: {len(threads)}")
    log.info(f"Starting {len(threads)} continuous worker threads...")

    for t in threads:
        t.start()

    # === MONITORING LOOP ===

    start_time = time.time()
    last_status = start_time

    while time.time() - start_time < run_duration:
        time.sleep(1)
        current_time = time.time()

        # Check all suppliers health
        for idx, supplier in enumerate(suppliers):
            if not check_server_health(supplier):
                log.error(f"{supplier.serverid} became unresponsive!")
                server_dead.set()
                break

        if server_dead.is_set():
            break

        # Status update every 10 seconds
        if current_time - last_status >= 10:
            elapsed = int(current_time - start_time)

            with stats_lock:
                current_stats = stats.copy()
            with uid_lock:
                ranges_used = sum(1 for uids in uid_numbers_by_range.values() if uids)

            log.info(f"[{elapsed}s/{run_duration}s] Users: {current_stats['users_created']}, "
                    f"SharedConfigOps: {current_stats['shared_config_ops']}, "
                    f"GroupOps: {current_stats['group_membership_ops']}, "
                    f"Fixups: {current_stats['fixup_tasks']}, "
                    f"Ranges: {ranges_used}/4")
            last_status = current_time

    # === SHUTDOWN ===

    log.info("Stopping all worker threads...")
    stop_event.set()
    time.sleep(2)

    hung_threads = []
    for t in threads:
        t.join(timeout=5)
        if t.is_alive():
            hung_threads.append(t.name)

    if hung_threads:
        log.warning(f"Threads did not finish cleanly: {hung_threads}")

    # === RESULTS ===

    elapsed = time.time() - start_time
    log.info(f"=== TEST COMPLETED - {elapsed:.0f}s ===")

    with stats_lock:
        final_stats = stats.copy()

    log.info(f"Total users: {final_stats['users_created']} "
             f"(initial: {len(pre_created_users)}, new: {final_stats['users_created'] - len(pre_created_users)})")
    log.info(f"Total shared config ops: {final_stats['shared_config_ops']}")
    log.info(f"Total group membership ops: {final_stats['group_membership_ops']}")
    log.info(f"Total fixup tasks: {final_stats['fixup_tasks']}")
    log.info(f"Total groups churned: {final_stats['groups_churned']}")

    # Restart counts
    with restart_counts_lock:
        total_restarts = sum(restart_counts)
        restart_details = ', '.join(f's{i+1}:{c}' for i, c in enumerate(restart_counts))
        log.info(f"Total supplier restarts: {total_restarts} ({restart_details})")

    # DNA range distribution analysis
    log.info("=== DNA Range Distribution ===")
    with uid_lock:
        ranges_used = 0
        for range_idx, uids in uid_numbers_by_range.items():
            if uids:
                ranges_used += 1
                log.info(f"Range {range_idx} (standalone{range_idx+1}): {len(uids)} users, "
                        f"values {min(uids)}-{max(uids)}")

    log.info(f"Ranges used: {ranges_used}/4")

    # Verify all instances still running
    for supplier in suppliers:
        if not supplier.status():
            pytest.fail(f"{supplier.serverid} crashed during test!")
        assert check_server_health(supplier), f"{supplier.serverid} not responsive after test"

    # Verify DNA used multiple ranges (proves cross-instance operation)
    assert ranges_used >= 2, f"Expected DNA to use multiple ranges, but only {ranges_used} used"

    # Run final fixup on each instance
    log.info("Running final MemberOf fixup on all instances...")
    for supplier in suppliers:
        try:
            memberof_plugin = MemberOfPlugin(supplier)
            final_task = memberof_plugin.fixup(DEFAULT_SUFFIX)
            final_task.wait(timeout=60)
            log.info(f"{supplier.serverid} fixup completed")
        except Exception as e:
            log.warning(f"{supplier.serverid} fixup error: {e}")

    # Wait for replication to settle
    time.sleep(3)
    for supplier in suppliers[1:]:
        try:
            repl.wait_for_replication(m1, supplier)
        except Exception:
            pass

    log.info("=== DNA STRESS TEST PASSED - No crash detected ===")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
