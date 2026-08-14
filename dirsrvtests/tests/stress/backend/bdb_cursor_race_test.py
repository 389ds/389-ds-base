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
import ldap
import ldap.modlist as modlist
from threading import Thread, Event, Lock
import random
import time
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PW_DM
from lib389.idm.user import UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnits

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

# Test configuration
SEARCH_THREADS = 30
MODIFY_THREADS = 15
DELETE_THREADS = 8
CHURN_THREADS = 5
DEFAULT_TIMEOUT = 60 * 60  # 60 minutes in seconds

# Statistics and crash detection
stats_lock = Lock()
stats = {
    'searches': 0,
    'modifies': 0,
    'deletes': 0,
    'adds': 0,
    'errors': 0,
    'start_time': None
}

# Global flag to track if server crashed
server_crashed = False
crash_lock = Lock()

HIGH_CONTENTION_FILTERS = [
    "(objectClass=inetOrgPerson)",
    "(objectClass=person)",
    "(objectClass=*)",
    "(uid=*)",
    "(cn=*)",
    "(mail=*)",
    "(description=*)",
    "(telephoneNumber=*)",
]


def create_raw_connection(host, port):
    """Create a raw LDAP connection independent of the instance connection pool

    :param host: LDAP server hostname
    :param port: LDAP server port
    :returns: Raw LDAP connection
    """
    # Use ldap.initialize to create a completely independent connection
    uri = f"ldap://{host}:{port}"
    conn = ldap.initialize(uri)
    conn.protocol_version = ldap.VERSION3

    # Short timeout to fail fast
    conn.set_option(ldap.OPT_NETWORK_TIMEOUT, 5.0)
    conn.set_option(ldap.OPT_TIMEOUT, 5.0)

    # Bind as Directory Manager
    conn.simple_bind_s(DN_DM, PW_DM)
    return conn


def update_stats(operation, count=1):
    """Update operation statistics in a thread-safe manner"""
    with stats_lock:
        stats[operation] = stats.get(operation, 0) + count


def mark_server_crashed():
    """Mark that the server has crashed"""
    global server_crashed
    with crash_lock:
        server_crashed = True


def print_stats():
    """Print current statistics"""
    with stats_lock:
        if stats['start_time']:
            elapsed = time.time() - stats['start_time']
            log.info(f"Statistics after {elapsed:.1f}s: "
                    f"searches={stats['searches']}, "
                    f"modifies={stats['modifies']}, "
                    f"adds={stats['adds']}, "
                    f"deletes={stats['deletes']}, "
                    f"errors={stats['errors']}")


def setup_test_data(inst, num_entries=1000):
    """Setup test data with multi-valued attributes

    :param inst: DirSrv instance
    :param num_entries: Number of test entries to create
    """
    log.info(f"Setting up {num_entries} test entries with multi-valued attributes...")

    # Create entries with many multi-valued attributes using raw LDAP
    created = 0
    for i in range(num_entries):
        try:
            dn = f"uid=test{i},ou=people,{DEFAULT_SUFFIX}"

            descriptions = [f"description_{i}_{j}_{'x'*50}".encode() for j in range(20)]
            phones = [f"+1-555-{i:04d}-{j:04d}".encode() for j in range(10)]
            mails = [f"test{i}.{j}@example.com".encode() for j in range(5)]

            attrs = {
                'objectClass': [b'top', b'person', b'organizationalPerson', b'inetOrgPerson'],
                'uid': [f'test{i}'.encode()],
                'cn': [f'Test User {i}'.encode()],
                'sn': [f'User{i}'.encode()],
                'description': descriptions,
                'telephoneNumber': phones,
                'mail': mails,
                'userPassword': [b'password'],
            }

            inst.add_s(dn, modlist.addModlist(attrs))
            created += 1

            if (i + 1) % 100 == 0:
                log.info(f"Created {i + 1}/{num_entries} entries...")

        except ldap.ALREADY_EXISTS:
            pass
        except Exception as e:
            log.warning(f"Error creating entry {i}: {e}")

    log.info(f"Test data setup complete. Created {created} new entries.")


def search_worker(stop_event, host, port, thread_id):
    """Worker thread for high-contention searches

    :param stop_event: Event to signal thread termination
    :param host: LDAP server hostname
    :param port: LDAP server port
    :param thread_id: Thread identifier
    """
    # Create independent raw LDAP connection for this thread
    conn = create_raw_connection(host, port)
    log.info(f"Search worker {thread_id} started")
    search_count = 0

    try:
        while not stop_event.is_set():
            try:
                filter_str = random.choice(HIGH_CONTENTION_FILTERS)
                scope = random.choice([ldap.SCOPE_SUBTREE, ldap.SCOPE_ONELEVEL])

                conn.search_s(
                    DEFAULT_SUFFIX,
                    scope,
                    filter_str,
                    attrlist=['dn']
                )

                search_count += 1

                if search_count % 100 == 0:
                    update_stats('searches', 100)

            except ldap.SERVER_DOWN:
                log.error(f"Search worker {thread_id}: SERVER DOWN - ns-slapd crashed!")
                mark_server_crashed()
                stop_event.set()
                break
            except Exception as e:
                update_stats('errors')
                if search_count < 10:
                    log.debug(f"Search worker {thread_id} error: {e}")
    finally:
        try:
            conn.unbind_s()
        except:
            pass
        log.info(f"Search worker {thread_id} stopped (total searches: {search_count})")


def modify_worker(stop_event, host, port, thread_id):
    """Worker thread for modifying multi-valued attributes

    :param stop_event: Event to signal thread termination
    :param host: LDAP server hostname
    :param port: LDAP server port
    :param thread_id: Thread identifier
    """
    # Create independent raw LDAP connection for this thread
    conn = create_raw_connection(host, port)
    log.info(f"Modify worker {thread_id} started")
    modify_count = 0

    try:
        while not stop_event.is_set():
            try:
                entry_num = random.randint(0, 999)
                dn = f"uid=test{entry_num},ou=people,{DEFAULT_SUFFIX}"
                attr = 'description'
                value = f"{attr}_{int(time.time() * 1000000)}_{'x'*100}"

                # Strategy 1: Add a new value (increases index size)
                if random.random() < 0.3:
                    mod_attrs = [(ldap.MOD_ADD, attr, [value.encode()])]

                # Strategy 2: Replace all values (causes delete + add)
                # This should trigger __db_ditem_nolog() in the delete phase
                elif random.random() < 0.7:
                    new_values = [f"{attr}_new_{i}_{'y'*100}".encode() for i in range(20)]
                    mod_attrs = [(ldap.MOD_REPLACE, attr, new_values)]

                # Strategy 3: Delete all values, then add multiple new ones
                else:
                    new_values = [f"{attr}_churn_{i}_{'z'*100}".encode() for i in range(15)]
                    mod_attrs = [
                        (ldap.MOD_DELETE, attr, []),
                        (ldap.MOD_ADD, attr, new_values)
                    ]

                conn.modify_s(dn, mod_attrs)
                modify_count += 1

                if modify_count % 50 == 0:
                    update_stats('modifies', 50)

            except ldap.SERVER_DOWN:
                log.error(f"Modify worker {thread_id}: SERVER DOWN - ns-slapd crashed!")
                mark_server_crashed()
                stop_event.set()
                break
            except ldap.NO_SUCH_OBJECT:
                pass
            except Exception as e:
                update_stats('errors')
                if modify_count < 10:
                    log.debug(f"Modify worker {thread_id} error: {e}")
    finally:
        try:
            conn.unbind_s()
        except:
            pass
        log.info(f"Modify worker {thread_id} stopped (total modifies: {modify_count})")


def delete_worker(stop_event, host, port, thread_id):
    """Worker thread for deleting entries

    :param stop_event: Event to signal thread termination
    :param host: LDAP server hostname
    :param port: LDAP server port
    :param thread_id: Thread identifier
    """
    # Create independent raw LDAP connection for this thread
    conn = create_raw_connection(host, port)
    log.info(f"Delete worker {thread_id} started")
    delete_count = 0
    delete_range_start = 5000 + (thread_id * 1000)

    try:
        while not stop_event.is_set():
            try:
                entry_num = delete_range_start + (delete_count % 1000)
                dn = f"uid=delete{entry_num},ou=people,{DEFAULT_SUFFIX}"

                conn.delete_s(dn)
                delete_count += 1

                if delete_count % 20 == 0:
                    update_stats('deletes', 20)

                time.sleep(0.01)

            except ldap.SERVER_DOWN:
                log.error(f"Delete worker {thread_id}: SERVER DOWN - ns-slapd crashed!")
                mark_server_crashed()
                stop_event.set()
                break
            except ldap.NO_SUCH_OBJECT:
                pass
            except Exception as e:
                update_stats('errors')
                if delete_count < 10:
                    log.debug(f"Delete worker {thread_id} error: {e}")
    finally:
        try:
            conn.unbind_s()
        except:
            pass
        log.info(f"Delete worker {thread_id} stopped (total deletes: {delete_count})")


def churn_worker(stop_event, host, port, thread_id):
    """Worker thread for add/delete churn

    :param stop_event: Event to signal thread termination
    :param host: LDAP server hostname
    :param port: LDAP server port
    :param thread_id: Thread identifier
    """
    # Create independent raw LDAP connection for this thread
    conn = create_raw_connection(host, port)
    log.info(f"Churn worker {thread_id} started")
    counter = 10000 + (thread_id * 10000)
    add_count = 0

    try:
        while not stop_event.is_set():
            try:
                dn = f"uid=churn{counter},ou=people,{DEFAULT_SUFFIX}"

                attrs = {
                    'objectClass': [b'top', b'person', b'inetOrgPerson'],
                    'uid': [f'churn{counter}'.encode()],
                    'cn': [f'Churn {counter}'.encode()],
                    'sn': [f'User{counter}'.encode()],
                    'description': [f'desc_{i}'.encode() for i in range(15)],
                    'telephoneNumber': [f'+1-555-churn-{i:04d}'.encode() for i in range(8)],
                    'userPassword': [b'password'],
                }

                conn.add_s(dn, modlist.addModlist(attrs))
                add_count += 1
                update_stats('adds')

                time.sleep(0.005)

                conn.delete_s(dn)
                update_stats('deletes')

                counter += 1

                time.sleep(0.01)

            except ldap.SERVER_DOWN:
                log.error(f"Churn worker {thread_id}: SERVER DOWN - ns-slapd crashed!")
                mark_server_crashed()
                stop_event.set()
                break
            except ldap.ALREADY_EXISTS:
                try:
                    conn.delete_s(dn)
                except:
                    pass
                counter += 1
            except Exception as e:
                update_stats('errors')
                if add_count < 10:
                    log.debug(f"Churn worker {thread_id} error: {e}")
                counter += 1
    finally:
        try:
            conn.unbind_s()
        except:
            pass
        log.info(f"Churn worker {thread_id} stopped (total adds: {add_count})")


def monitor_worker(stop_event, duration):
    """Worker thread to monitor progress and enforce timeout

    :param stop_event: Event to signal thread termination
    :param duration: Test duration in seconds
    """
    log.info(f"Monitor worker started (duration: {duration}s / {duration/60:.1f}m)")

    start_time = time.time()
    last_stats_time = start_time

    while not stop_event.is_set():
        current_time = time.time()
        elapsed = current_time - start_time

        # Print stats every 30 seconds
        if current_time - last_stats_time >= 30:
            print_stats()
            log.info(f"Progress: {elapsed:.0f}s / {duration}s ({elapsed/duration*100:.1f}%)")
            last_stats_time = current_time

        # Check if duration has been reached
        if elapsed >= duration:
            log.info(f"Duration {duration}s reached. Test completed successfully!")
            stop_event.set()
            break

        time.sleep(1)

    log.info("Monitor worker stopped")


@pytest.mark.slow
def test_bdb_cursor_race(topology_st):
    """Stress test to reproduce BDB cursor race condition

    :id: d53a9580-58e5-454c-96fd-57839660fd51
    :setup: Standalone instance
    :steps:
        1. Setup test data with 1000 entries containing multi-valued attributes
        2. Start search, modify, delete and churn threads
        3. Run for 60 minutes
    :expectedresults:
        1. Success
        2. Success
        3. Server doesn't crash
    """

    inst = topology_st.standalone

    log.info("=" * 80)
    log.info("BDB Cursor Race Condition Test")
    log.info(f"Instance: {inst.serverid}")
    log.info(f"Base DN: {DEFAULT_SUFFIX}")
    log.info(f"Duration: {DEFAULT_TIMEOUT}s ({DEFAULT_TIMEOUT/60:.0f} minutes)")
    log.info("=" * 80)

    # Enable log buffering for better performance
    log.info("Enabling log buffering for access and error logs...")
    inst.config.set('nsslapd-accesslog-logbuffering', 'on')
    inst.config.set('nsslapd-errorlog-logbuffering', 'on')

    # Setup test data
    setup_test_data(inst, num_entries=1000)

    # Extract connection parameters once to avoid sharing inst object
    ldap_host = inst.host
    ldap_port = inst.port

    # Reset statistics and crash flag
    global stats, server_crashed
    stats = {
        'searches': 0,
        'modifies': 0,
        'deletes': 0,
        'adds': 0,
        'errors': 0,
        'start_time': time.time()
    }
    server_crashed = False

    # Create stop event
    stop_event = Event()

    # Start all worker threads
    threads = []

    # Search workers - hammer equality indexes
    for i in range(SEARCH_THREADS):
        t = Thread(target=search_worker,
                  args=(stop_event, ldap_host, ldap_port, i),
                  name=f"search-{i}")
        t.daemon = True
        t.start()
        threads.append(t)

    # Modify workers - update multi-valued attributes
    for i in range(MODIFY_THREADS):
        t = Thread(target=modify_worker,
                  args=(stop_event, ldap_host, ldap_port, i),
                  name=f"modify-{i}")
        t.daemon = True
        t.start()
        threads.append(t)

    # Delete workers - trigger index cleanup
    for i in range(DELETE_THREADS):
        t = Thread(target=delete_worker,
                  args=(stop_event, ldap_host, ldap_port, i),
                  name=f"delete-{i}")
        t.daemon = True
        t.start()
        threads.append(t)

    # Churn workers - add/delete cycle
    for i in range(CHURN_THREADS):
        t = Thread(target=churn_worker,
                  args=(stop_event, ldap_host, ldap_port, i),
                  name=f"churn-{i}")
        t.daemon = True
        t.start()
        threads.append(t)

    # Monitor worker - enforce timeout and print stats
    monitor_thread = Thread(target=monitor_worker,
                           args=(stop_event, DEFAULT_TIMEOUT),
                           name="monitor")
    monitor_thread.daemon = True
    monitor_thread.start()
    threads.append(monitor_thread)

    log.info("=" * 80)
    log.info(f"Started {len(threads)} worker threads:")
    log.info(f"  - {SEARCH_THREADS} search threads (equality index queries)")
    log.info(f"  - {MODIFY_THREADS} modify threads (multi-valued attr updates)")
    log.info(f"  - {DELETE_THREADS} delete threads (index cleanup)")
    log.info(f"  - {CHURN_THREADS} churn threads (add/delete cycle)")
    log.info(f"  - 1 monitor thread (timeout enforcement)")
    log.info("=" * 80)
    log.info(f"Running for {DEFAULT_TIMEOUT}s ({DEFAULT_TIMEOUT/60:.0f} minutes)...")
    log.info("=" * 80)

    # Wait for all threads to finish or timeout
    for t in threads:
        t.join()

    # Final statistics
    log.info("=" * 80)
    log.info("Final Statistics:")
    print_stats()
    log.info("=" * 80)

    # Check if server crashed during the test
    if server_crashed:
        log.error("=" * 80)
        log.error("Test FAILED: Server crashed during the test!")
        log.error("=" * 80)
        pytest.fail("ns-slapd crashed during BDB cursor race test")
    else:
        log.info("=" * 80)
        log.info("Test PASSED: Completed full duration without crash")
        log.info("=" * 80)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    import os
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", "-v", CURRENT_FILE])
