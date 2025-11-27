# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""
DNA Plugin Stress Test - Shared Config Locking Stress

Strategy:
    - Create 4 instances with replication
    - Create many backends with shared config containers
    - Spawn threads to continuously add/modify/delete shared config entries
    - Enable MemberOf with nested groups for additional pre-op load
    - Add cn=config modification worker

Configure via environment variables:
    DNA_STRESS_NUM_BACKENDS  - Number of backends (default: 50)
    DNA_STRESS_DURATION      - Test duration in seconds (default: 120)
    DNA_STRESS_THREADS       - Threads per backend for modifications (default: 2)

Example:
    # LMDB test (default)
    pytest -vs dirsrvtests/tests/stress/dna/dna_stress_test.py::test_dna_shared_config_stress_lmdb

    # BDB test
    NSSLAPD_DB_LIB=bdb pytest -vs dirsrvtests/tests/stress/dna/dna_stress_test.py::test_dna_shared_config_stress_bdb

    # Custom settings
    DNA_STRESS_NUM_BACKENDS=100 DNA_STRESS_DURATION=300 pytest -vs ...
"""
import pytest
import logging
import time
import threading
import ldap
import uuid
import random
import os
from lib389.topologies import topology_i4
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.plugins import DNAPlugin, DNAPluginConfigs, DNAPluginSharedConfigs, MemberOfPlugin
from lib389.backend import Backends
from lib389.config import BDB_LDBMConfig, LMDB_LDBMConfig
from lib389.replica import ReplicationManager
from lib389.utils import get_default_db_lib
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD

pytestmark = pytest.mark.tier3

log = logging.getLogger(__name__)

# Configuration from environment
NUM_BACKENDS = int(os.environ.get('DNA_STRESS_NUM_BACKENDS', '50'))
RUN_DURATION = int(os.environ.get('DNA_STRESS_DURATION', '120'))
THREADS_PER_BACKEND = int(os.environ.get('DNA_STRESS_THREADS', '2'))


class StressStats:
    """Thread-safe statistics collector."""

    def __init__(self):
        self._lock = threading.Lock()
        self._counts = {
            'add': 0,
            'modify': 0,
            'delete': 0,
            'errors': 0,
            'users_created': 0,
            'config_mods': 0,
        }

    def increment(self, key, count=1):
        with self._lock:
            self._counts[key] += count

    def get(self):
        with self._lock:
            return self._counts.copy()


def setup_backends_and_dna(instances, num_backends, db_lib):
    """Create backends and configure DNA plugin on all instances.

    Args:
        instances: List of DirSrv instances
        num_backends: Number of additional backends to create
        db_lib: Database library (mdb or bdb)

    Returns:
        dict with backend_suffixes, ou_ranges_by_suffix, and instances
    """
    inst = instances[0]
    log.info(f"Setting up {num_backends} backends with {db_lib} backend on {len(instances)} instances")

    for inst_tune in instances:
        if db_lib == "bdb":
            bdb_config = BDB_LDBMConfig(inst_tune)
            required_locks = max(50000, 20000 + (num_backends * 500))
            bdb_config.replace("nsslapd-db-locks", str(required_locks))
            log.info(f"Set nsslapd-db-locks={required_locks} on {inst_tune.serverid}")
        else:
            # LMDB needs sufficient max-dbs for all backends and their indexes.
            total_suffixes = num_backends + 1  # +1 for default suffix
            indexes_per_backend = 32  # 29 template + 3 from instance.c
            non_index_dbis_per_backend = 3  # id2entry, changelog, @long-entryrdn
            global_dbis = 1  # __DBNAMES
            margin = 100  # Extra safety margin
            required_max_dbs = (total_suffixes * (indexes_per_backend + non_index_dbis_per_backend)) + \
                              global_dbis + margin
            try:
                mdb_config = LMDB_LDBMConfig(inst_tune)
                mdb_config.replace("nsslapd-mdb-max-dbs", str(required_max_dbs))
                log.info(f"Set nsslapd-mdb-max-dbs={required_max_dbs} on {inst_tune.serverid}")
            except ldap.NO_SUCH_ATTRIBUTE:
                log.warning(f"Could not set nsslapd-mdb-max-dbs on {inst_tune.serverid}")

        inst_tune.restart()

    backend_suffixes = []
    backends = Backends(inst)

    for i in range(num_backends):
        suffix = f"dc=be{i}"
        be_name = f"be{i}"
        backend_suffixes.append(suffix)

        try:
            backends.get(be_name)
        except ldap.NO_SUCH_OBJECT:
            backends.create(properties={
                'cn': be_name,
                'nsslapd-suffix': suffix,
            })
            try:
                Domain(inst, suffix).create(properties={'dc': f'be{i}'})
            except ldap.ALREADY_EXISTS:
                pass

        if (i + 1) % 10 == 0:
            log.info(f"Created {i + 1}/{num_backends} backends")

    for other_inst in instances[1:]:
        other_backends = Backends(other_inst)
        for i, suffix in enumerate(backend_suffixes):
            be_name = f"be{i}"
            try:
                other_backends.get(be_name)
            except ldap.NO_SUCH_OBJECT:
                other_backends.create(properties={
                    'cn': be_name,
                    'nsslapd-suffix': suffix,
                })
                try:
                    Domain(other_inst, suffix).create(properties={'dc': f'be{i}'})
                except ldap.ALREADY_EXISTS:
                    pass

    ou_ranges_by_suffix = {}
    all_suffixes = [DEFAULT_SUFFIX] + backend_suffixes

    for suffix in all_suffixes:
        ous = OrganizationalUnits(inst, suffix)

        try:
            ous.get('People')
        except ldap.NO_SUCH_OBJECT:
            ous.create(properties={'ou': 'People'})

        try:
            ou = ous.get('dna_ranges')
            # Clean existing shared configs
            for cfg in DNAPluginSharedConfigs(inst, ou.dn).list():
                cfg.delete()
        except ldap.NO_SUCH_OBJECT:
            ou = ous.create(properties={'ou': 'dna_ranges'})
        ou_ranges_by_suffix[suffix] = ou

    shared_cfg_ou = ou_ranges_by_suffix[DEFAULT_SUFFIX]

    for idx, inst_cfg in enumerate(instances):
        dna_plugin = DNAPlugin(inst_cfg)
        configs = DNAPluginConfigs(inst_cfg, dna_plugin.dn)

        try:
            configs.get('stress uidNumber').delete()
        except ldap.NO_SUCH_OBJECT:
            pass

        base_value = 100000 + (idx * 100000)
        configs.create(properties={
            'cn': 'stress uidNumber',
            'dnaType': 'uidNumber',
            'dnaNextValue': str(base_value),
            'dnaMaxValue': str(base_value + 99999),
            'dnaMagicRegen': '-1',
            'dnaFilter': '(objectclass=posixAccount)',
            'dnaScope': DEFAULT_SUFFIX,
            'dnaThreshold': '100',
            'dnaSharedCfgDN': shared_cfg_ou.dn,
            'dnaRemoteBindDN': DN_DM,
            'dnaRemoteBindCred': PASSWORD
        })

        dna_plugin.enable()

    for suffix in all_suffixes:
        ou = ou_ranges_by_suffix[suffix]
        shared_configs = DNAPluginSharedConfigs(inst, ou.dn)
        for inst_cfg in instances:
            shared_configs.create(properties={
                'dnaHostname': inst_cfg.host,
                'dnaPortNum': str(inst_cfg.port),
                'dnaRemainingValues': '100000',
                'dnaRemoteBindMethod': 'SIMPLE',
                'dnaRemoteConnProtocol': 'LDAP'
            })

    for inst_cfg in instances:
        inst_cfg.restart()

    log.info(f"Setup complete: {len(all_suffixes)} backends with DNA shared configs on {len(instances)} instances")

    return {
        'backend_suffixes': backend_suffixes,
        'all_suffixes': all_suffixes,
        'ou_ranges_by_suffix': ou_ranges_by_suffix,
        'instances': instances,
    }


def setup_memberof_and_groups(instances, all_suffixes):
    """Enable MemberOf plugin and create nested groups across backends.

    Args:
        instances: List of DirSrv instances
        all_suffixes: List of all suffixes (backends)

    Returns:
        dict with groups and users created
    """
    log.info("Setting up MemberOf plugin and nested groups...")

    for inst in instances:
        memberof = MemberOfPlugin(inst)
        memberof.enable()
        memberof.set('memberofgroupattr', 'member')
        memberof.set_autoaddoc('nsMemberOf')
        inst.restart()

    inst = instances[0]

    created_groups = {}
    for idx, suffix in enumerate(all_suffixes[:5]):  # Use first 5 backends
        ous = OrganizationalUnits(inst, suffix)
        try:
            ous.get('groups')
        except ldap.NO_SUCH_OBJECT:
            ous.create(properties={'ou': 'groups'})

        groups = Groups(inst, suffix, rdn='ou=groups')
        group_name = f'stress_group_{idx}'
        try:
            group = groups.get(group_name)
        except ldap.NO_SUCH_OBJECT:
            group = groups.create(properties={'cn': group_name})
        created_groups[suffix] = group

    top_groups = Groups(inst, DEFAULT_SUFFIX, rdn='ou=groups')
    try:
        top_group = top_groups.get('top_stress_group')
    except ldap.NO_SUCH_OBJECT:
        top_group = top_groups.create(properties={'cn': 'top_stress_group'})

    for suffix, group in created_groups.items():
        if suffix != DEFAULT_SUFFIX:
            try:
                top_group.add('member', group.dn)
            except ldap.TYPE_OR_VALUE_EXISTS:
                pass

    log.info(f"Created {len(created_groups)} groups with nested structure")
    return {'groups': created_groups, 'top_group': top_group}


def run_stress_test(instances, setup_data, duration, threads_per_backend, memberof_data):
    """Run the stress test.

    Args:
        instances: List of DirSrv instances
        setup_data: Dict from setup_backends_and_dna
        duration: Test duration in seconds
        threads_per_backend: Number of threads per backend
        memberof_data: Dict with groups and top_group from setup_memberof_and_groups
    """
    inst = instances[0]
    all_suffixes = setup_data['all_suffixes']
    ou_ranges_by_suffix = setup_data['ou_ranges_by_suffix']
    groups = memberof_data['groups']
    # top_group is the nested group containing other groups (created in setup_memberof_and_groups)

    stats = StressStats()
    stop_event = threading.Event()
    threads = []

    def shared_config_worker(suffix_idx, target_inst):
        """Worker that adds/modifies/deletes shared config entries."""
        suffix = all_suffixes[suffix_idx % len(all_suffixes)]
        ou = ou_ranges_by_suffix[suffix]
        shared_configs = DNAPluginSharedConfigs(target_inst, ou.dn)
        local_add = 0
        local_mod = 0
        local_del = 0

        while not stop_event.is_set():
            try:
                # Add a new shared config entry
                new_cfg = shared_configs.create(properties={
                    'dnaHostname': f'stress-{suffix_idx}-{uuid.uuid4().hex[:8]}.example.com',
                    'dnaPortNum': str(30000 + random.randint(0, 9999)),
                    'dnaRemainingValues': str(random.randint(1000, 100000)),
                    'dnaRemoteBindMethod': 'SIMPLE',
                    'dnaRemoteConnProtocol': 'LDAP'
                })
                local_add += 1

                # Modify the entry
                new_cfg.replace('dnaRemainingValues', str(random.randint(1, 50000)))
                local_mod += 1

                # Delete the entry
                new_cfg.delete()
                local_del += 1

            except ldap.SERVER_DOWN:
                stats.increment('errors')
                log.error("Server down!")
                stop_event.set()
                break
            except ldap.LDAPError:
                stats.increment('errors')
            except Exception:
                stats.increment('errors')

        stats.increment('add', local_add)
        stats.increment('modify', local_mod)
        stats.increment('delete', local_del)

    def user_creation_worker(target_inst, suffix_list):
        """Worker that creates users across backends to exercise DNA and MemberOf."""
        local_count = 0
        group_list = list(groups.values())

        while not stop_event.is_set():
            try:
                # Spread users across different backends
                suffix = random.choice(suffix_list)
                users = UserAccounts(target_inst, suffix)
                uid = f'stress_{uuid.uuid4().hex[:8]}'
                user = users.create(properties={
                    'uid': uid,
                    'cn': f'Stress {uid}',
                    'sn': 'StressUser',
                    'uidNumber': '-1',  # DNA will assign
                    'gidNumber': '1000',
                    'homeDirectory': f'/home/{uid}',
                })
                local_count += 1

                # Add user to a random group (triggers MemberOf)
                if group_list and random.random() < 0.5:
                    try:
                        random_group = random.choice(group_list)
                        random_group.add('member', user.dn)
                    except ldap.LDAPError:
                        pass

                # Delete after a brief delay to avoid filling up
                time.sleep(0.01)
                user.delete()

            except ldap.SERVER_DOWN:
                stats.increment('errors')
                log.error("Server down!")
                stop_event.set()
                break
            except ldap.LDAPError:
                stats.increment('errors')
            except Exception:
                stats.increment('errors')

        stats.increment('users_created', local_count)

    def config_worker(target_inst):
        """Worker that modifies DNA config entries (dnaThreshold) to trigger pre-op."""
        dna_plugin = DNAPlugin(target_inst)
        configs = DNAPluginConfigs(target_inst, dna_plugin.dn)
        local_config_mods = 0
        threshold_value = 100

        while not stop_event.is_set():
            try:
                config = configs.get('stress uidNumber')
                # Toggle dnaThreshold between 100 and 101 to trigger config reload
                threshold_value = 101 if threshold_value == 100 else 100
                config.replace('dnaThreshold', str(threshold_value))
                local_config_mods += 1
                time.sleep(0.1)  # Don't hammer too fast

            except ldap.SERVER_DOWN:
                stats.increment('errors')
                log.error("Server down!")
                stop_event.set()
                break
            except ldap.LDAPError:
                stats.increment('errors')
            except Exception:
                stats.increment('errors')

        stats.increment('config_mods', local_config_mods)

    num_workers = len(all_suffixes) * threads_per_backend
    log.info(f"Starting {num_workers} shared config worker threads across {len(instances)} instances")

    for i in range(num_workers):
        target_inst = instances[i % len(instances)]
        t = threading.Thread(
            target=shared_config_worker,
            args=(i, target_inst),
            name=f"SharedConfigWorker-{i}",
            daemon=True
        )
        threads.append(t)

    num_user_threads = len(instances) * 2
    log.info(f"Starting {num_user_threads} user creation threads")
    for i in range(num_user_threads):
        target_inst = instances[i % len(instances)]
        t = threading.Thread(
            target=user_creation_worker,
            args=(target_inst, all_suffixes),
            name=f"UserWorker-{i}",
            daemon=True
        )
        threads.append(t)

    log.info("Starting 1 config worker thread")
    t = threading.Thread(
        target=config_worker,
        args=(instances[0],),
        name="ConfigWorker",
        daemon=True
    )
    threads.append(t)

    log.info(f"=== DNA STRESS TEST STARTING ===")
    log.info(f"Duration: {duration}s, Backends: {len(all_suffixes)}, "
             f"Instances: {len(instances)}, Threads: {len(threads)}")

    for t in threads:
        t.start()

    start_time = time.time()
    last_report = start_time

    while time.time() - start_time < duration:
        time.sleep(1)
        current = time.time()

        if current - last_report >= 30:
            elapsed = int(current - start_time)
            s = stats.get()
            ops_per_sec = (s['add'] + s['modify'] + s['delete']) / elapsed if elapsed > 0 else 0
            log.info(f"[{elapsed}s/{duration}s] Add: {s['add']}, Mod: {s['modify']}, "
                    f"Del: {s['delete']}, Users: {s['users_created']}, "
                    f"ConfigMods: {s.get('config_mods', 0)}, "
                    f"Errors: {s['errors']}, Ops/sec: {ops_per_sec:.1f}")
            last_report = current

        if stop_event.is_set():
            log.error("Test stopped early due to server failure")
            break

    log.info("Stopping worker threads...")
    stop_event.set()

    for t in threads:
        t.join(timeout=5)

    hung = [t.name for t in threads if t.is_alive()]
    if hung:
        log.warning(f"Threads did not finish: {hung[:5]}...")

    log.info("Running MemberOf fixup task...")
    memberof = MemberOfPlugin(inst)
    try:
        task = memberof.fixup(basedn=DEFAULT_SUFFIX)
        task.wait(timeout=120)
        log.info(f"MemberOf fixup task completed with exit code: {task.get_exit_code()}")
    except Exception as e:
        log.warning(f"MemberOf fixup task failed: {e}")

    elapsed = time.time() - start_time
    s = stats.get()
    total_ops = s['add'] + s['modify'] + s['delete']

    log.info(f"=== TEST COMPLETED - {elapsed:.0f}s ===")
    log.info(f"Total shared config operations: {total_ops}")
    log.info(f"  - Adds: {s['add']}")
    log.info(f"  - Modifies: {s['modify']}")
    log.info(f"  - Deletes: {s['delete']}")
    log.info(f"Users created: {s['users_created']}")
    log.info(f"Config modifications: {s.get('config_mods', 0)}")
    log.info(f"Errors: {s['errors']}")
    log.info(f"Operations per second: {total_ops / elapsed:.1f}")

    for inst_check in instances:
        try:
            inst_check.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE, "(objectclass=*)", ['dn'])
            log.info(f"Server {inst_check.serverid} is alive and responding")
        except ldap.LDAPError as e:
            log.error(f"Server {inst_check.serverid} not responding: {e}")
            assert False, f"Server {inst_check.serverid} crashed during stress test"

    return s


@pytest.fixture(scope="function")
def stress_setup(topology_i4, request):
    """Setup for stress test with 4 instances and replication."""
    instances = [
        topology_i4.ins["standalone1"],
        topology_i4.ins["standalone2"],
        topology_i4.ins["standalone3"],
        topology_i4.ins["standalone4"],
    ]
    db_lib = get_default_db_lib()

    setup_data = setup_backends_and_dna(instances, NUM_BACKENDS, db_lib)

    log.info("Setting up replication topology...")
    repl = ReplicationManager(DEFAULT_SUFFIX)

    inst1 = instances[0]
    repl.create_first_supplier(inst1)

    for inst in instances[1:]:
        log.info(f"Joining {inst.serverid} to {inst1.serverid}...")
        repl.join_supplier(inst1, inst)

    for inst_from in instances:
        for inst_to in instances:
            if inst_from is inst_to:
                continue
            log.info(f"Ensuring agreement {inst_from.serverid} -> {inst_to.serverid}...")
            repl.ensure_agreement(inst_from, inst_to)

    time.sleep(3)
    for inst in instances[1:]:
        repl.wait_for_replication(inst1, inst)

    log.info("Replication topology setup complete")

    memberof_data = setup_memberof_and_groups(instances, setup_data['all_suffixes'])

    def fin():
        for inst in instances:
            try:
                DNAPlugin(inst).disable()
                MemberOfPlugin(inst).disable()
                inst.restart()
            except:
                pass

    request.addfinalizer(fin)
    return {
        'instances': instances,
        'setup_data': setup_data,
        'db_lib': db_lib,
        'memberof_data': memberof_data,
    }


@pytest.mark.skipif(get_default_db_lib() == "bdb", reason="Use test_dna_shared_config_stress_bdb for BDB")
def test_dna_shared_config_stress_lmdb(stress_setup):
    """Stress test for DNA shared config locking with LMDB backend.

    :id: 2974edfb-ec34-4754-8539-68e4407509fb
    :setup: 4 instances with replication, LMDB, many backends with DNA shared configs,
            MemberOf plugin enabled with nested groups
    :steps:
        1. Create multiple threads per backend modifying shared config entries
        2. Each modification triggers dna_load_shared_servers()
        3. Create users across backends with DNA and MemberOf processing
        4. Modify cn=config entries to trigger pre-op processing
        5. Run for configured duration
        6. Run MemberOf fixup task
    :expectedresults:
        1. No crashes from dna_load_shared_servers() race conditions
        2. All servers remain responsive throughout
        3. Operations complete without server failures
        4. MemberOf fixup completes successfully
    """
    instances = stress_setup['instances']
    setup_data = stress_setup['setup_data']
    memberof_data = stress_setup['memberof_data']

    stats = run_stress_test(instances, setup_data, RUN_DURATION, THREADS_PER_BACKEND, memberof_data)

    assert stats['add'] > 0, "Expected some add operations"
    assert stats['modify'] > 0, "Expected some modify operations"
    assert stats['delete'] > 0, "Expected some delete operations"

    log.info("=== LMDB STRESS TEST PASSED ===")


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Use test_dna_shared_config_stress_lmdb for LMDB")
def test_dna_shared_config_stress_bdb(stress_setup):
    """Stress test for DNA shared config locking with BDB backend.

    :id: 0ebbe83c-1823-45c0-9914-15c7c15e2d19
    :setup: 4 instances with replication, BDB, many backends with DNA shared configs,
            MemberOf plugin enabled with nested groups
    :steps:
        1. Create multiple threads per backend modifying shared config entries
        2. Each modification triggers dna_load_shared_servers()
        3. Create users across backends with DNA and MemberOf processing
        4. Modify cn=config entries to trigger pre-op processing
        5. Run for configured duration (BDB may need more conservative settings)
        6. Run MemberOf fixup task
    :expectedresults:
        1. No crashes from dna_load_shared_servers() race conditions
        2. All servers remain responsive throughout
        3. Operations complete without server failures
        4. MemberOf fixup completes successfully
    """
    instances = stress_setup['instances']
    setup_data = stress_setup['setup_data']
    memberof_data = stress_setup['memberof_data']

    bdb_threads = max(1, THREADS_PER_BACKEND // 2)

    stats = run_stress_test(instances, setup_data, RUN_DURATION, bdb_threads, memberof_data)

    assert stats['add'] > 0, "Expected some add operations"
    assert stats['modify'] > 0, "Expected some modify operations"
    assert stats['delete'] > 0, "Expected some delete operations"

    log.info("=== BDB STRESS TEST PASSED ===")


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-xvs", CURRENT_FILE])
