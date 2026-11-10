# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import threading
import time
import ldap
from collections import Counter
from lib389.schema import Schema
from lib389.idm.domain import Domain
from lib389.rootdse import RootDSE
from lib389._constants import DEFAULT_SUFFIX
from test389.topologies import topology_st as topo

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Globals for cross-thread coordination
# ---------------------------------------------------------------------------
stop_event = threading.Event()
crash_detected = threading.Event()
reload_failed = threading.Event()
stats_lock = threading.Lock()
stats = Counter()

ITERATIONS = 10
SEARCH_THREADS = 12
MAX_STRESS_TEST_DURATION = 300

SEARCH_ATTRS = [
    'cn', 'sn', 'givenName', 'mail', 'uid', 'userPassword',
    'objectClass', 'description', 'telephoneNumber', 'l', 'st',
    'postalCode', 'street', 'ou', 'title', 'manager', 'secretary',
    'homeDirectory', 'loginShell', 'uidNumber', 'gidNumber',
    'memberOf', 'nsAccountLock', 'createTimestamp', 'modifyTimestamp',
    'entrydn', 'entryid', 'numSubordinates', 'hasSubordinates',
]


def _is_connection_error(exc):
    """Return True if an LDAP error likely means the server crashed."""
    if isinstance(exc, (ldap.SERVER_DOWN, ldap.CONNECT_ERROR, ldap.UNAVAILABLE)):
        return True
    if isinstance(exc, ldap.LDAPError):
        msg = str(exc).lower()
        return any(s in msg for s in (
            "can't contact", "connection reset",
            "server is unavailable", "connection refused",
            "broken pipe",
        ))
    return False


def check_server_alive(inst):
    """Quick check: can we connect and read the root DSE?"""
    try:
        return RootDSE(inst).exists()
    except ldap.LDAPError as e:
        log.error(f"check_server_alive: check_server_alive failed: {str(e)}")
        return False


def search_worker(inst, _worker_id):
    """Flood LDAP searches that exercise attribute syntax lookups.

    Each search forces the server to look up attribute syntax for the
    requested attributes (cn, sn, mail, etc.) via attr_syntax_find().
    That function acquires the read lock, looks up the asyntaxinfo node,
    bumps the refcount, and releases the lock -- creating the race window.

    We search with many attributes to maximize the number of
    attr_syntax_find() calls per operation, widening the window.
    """
    conn = inst.clone()
    conn.rebind()
    domain = Domain(conn, DEFAULT_SUFFIX)
    domain._list_attrlist = SEARCH_ATTRS

    try:
        while not stop_event.is_set() and not crash_detected.is_set():
            try:
                domain.search(scope='subtree', filter='(objectclass=*)')
                with stats_lock:
                    stats['searches_ok'] += 1
            except ldap.LDAPError as e:
                with stats_lock:
                    stats['searches_fail'] += 1
                    if _is_connection_error(e):
                        stats['connection_errors'] += 1
            except Exception as e:
                log.error(f"search_worker: failed with unknown exception: {str(e)}")
                with stats_lock:
                    stats['searches_error'] += 1
    finally:
        conn.close()


def schema_reload_worker(inst):
    """Repeatedly trigger schema reload to race against query threads.

    Schema reload is triggered by adding a temporary attribute type to
    cn=schema, then deleting it. Each add/delete causes the
    server to call attr_syntax_swap_ht(), which frees all old asyntaxinfo
    nodes without checking reference counts.
    """
    schema = Schema(inst)

    for i in range(ITERATIONS):
        if stop_event.is_set() or crash_detected.is_set():
            break

        oid = f'9.9.9.9.9.{os.getpid()}.{i}'
        attr_name = f'pocTestAttr{i}'
        attr_params = {
            'names': (attr_name,),
            'oid': oid,
            'desc': 'UAF PoC temp attr',
            'syntax': '1.3.6.1.4.1.1466.115.121.1.15',
            'single_value': 1,
            'sup': (),
            'syntax_len': None,
            'x_ordered': None,
            'collective': None,
            'obsolete': None,
            'no_user_mod': None,
            'equality': None,
            'substr': None,
            'ordering': None,
            'usage': None,
            'x_origin': ('poc-011',),
        }

        try:
            schema.add_attributetype(attr_params)
            with stats_lock:
                stats['reloads_ok'] += 1
        except ldap.LDAPError as e:
            log.error(f"schema_reload_worker: add_attributetype failed: {attr_name} - {str(e)}")
            with stats_lock:
                stats['reloads_fail'] += 1
                if _is_connection_error(e):
                    stats['reload_connection_errors'] += 1
        except Exception as e:
            log.error(f"schema_reload_worker: add_attributetype unknown exception: {attr_name} - {str(e)}")
            with stats_lock:
                stats['reloads_error'] += 1

        time.sleep(1)

        # Add anlother attirbute that should trigger a failure
        bad_attr_name = f'pocTestAttr{i+1}'
        attr_params['names'] = (bad_attr_name,)
        try:
            schema.add_attributetype(attr_params)
        except ldap.LDAPError:
            pass

        try:
            schema.remove_attributetype(attr_name)
            with stats_lock:
                stats['reloads_ok'] += 1
        except ldap.LDAPError as e:
            log.error(f"schema_reload_worker: remove_attributetype failed: {attr_name} - {str(e)}")
            with stats_lock:
                stats['reloads_fail'] += 1
        except Exception:
            pass

        task = schema.reload()
        task.wait(timeout=20)
        if task.get_exit_code() != 0:
            reload_failed.set()
            log.error(f"schema_reload_worker: reload task failed: {str(task.get_task_log())}")
            return

        time.sleep(0.01)


def crash_monitor(inst, check_interval=0.5):
    """Periodically check if the server is still alive.

    If the UAF is triggered, the server will crash (SIGSEGV or SIGABRT).
    This thread detects the crash by failing to connect.
    """
    consecutive_failures = 0
    while not stop_event.is_set() and not crash_detected.is_set():
        time.sleep(check_interval)
        try:
            alive = check_server_alive(inst)
            if alive:
                consecutive_failures = 0
            else:
                consecutive_failures += 1
                if consecutive_failures >= 2:
                    crash_detected.set()
                    with stats_lock:
                        stats['crash_detected'] = 1
        except Exception:
            consecutive_failures += 1
            if consecutive_failures >= 2:
                crash_detected.set()
                with stats_lock:
                    stats['crash_detected'] = 1


def test_schema_reload_under_load(topo):
    """Exercise schema reload while concurrent searches stress attribute syntax lookups

    Concurrent LDAP searches force repeated attr_syntax_get_by_name() calls while
    a background thread adds and removes temporary attribute types and runs the
    schema reload task. This widens the race window between readers releasing the
    attribute-syntax read lock and attr_syntax_swap_ht() replacing the hash tables,
    helping detect use-after-free or memory leaks under AddressSanitizer.

    :id: d977adc1-6f92-4246-8a99-cd66f254e698
    :setup: Standalone instance
    :steps:
        1. Verify the server responds to a root DSE read
        2. Start a crash-monitor thread and multiple search threads, each with
           its own lib389 connection, performing subtree searches that request
           many attribute types
        3. Start a schema-reload thread that adds a temporary attribute type,
           removes it, and runs the schema reload task for each iteration
        4. Wait for the reload thread to finish while logging search and reload
           progress
        5. Stop all threads and verify the server is still reachable
    :expectedresults:
        1. The server is reachable before the test begins
        2. Search threads run without connection failures attributable to a crash
        3. Each schema reload task completes successfully
        4. Progress is logged until the reload thread exits
        5. The server remains reachable after all threads stop; a crash or
           loss of connectivity during the run indicates a defect
    """

    inst = topo.standalone

    log.info('=' * 70)
    log.info('Finding 011: Use-After-Free in Schema Reload')
    log.info('           attr_syntax_swap_ht() — attrsyntax.c:1639-1665')
    log.info('=' * 70)
    log.info("")
    log.info(f'  URI:             {inst.get_ldap_uri()}')
    log.info(f'  Base DN:         {DEFAULT_SUFFIX}')
    log.info(f'  Search threads:  {SEARCH_THREADS}')
    log.info(f'  Reload iters:    {ITERATIONS}')
    log.info("")
    log.info('Race window: reader releases AS_LOCK_READ after lookup, before')
    log.info('writer acquires write lock in attr_syntax_swap_ht(). The swap')
    log.info('frees all old nodes unconditionally (ignoring refcount), so any')
    log.info('thread still holding an asyntaxinfo* gets a dangling pointer.')
    log.info("")

    log.info('[*] Pre-flight: checking server connectivity...')
    if not check_server_alive(inst):
        pytest.fail('Cannot reach server. Check host/port.')
    log.info('[+] Server is alive.')
    log.info("")

    threads = []

    monitor = threading.Thread(
        target=crash_monitor,
        args=(inst,),
        daemon=True,
        name='crash-monitor'
    )
    monitor.start()

    log.info(f'[*] Starting {SEARCH_THREADS} search flood threads...')
    for i in range(SEARCH_THREADS):
        t = threading.Thread(
            target=search_worker,
            args=(inst, i),
            daemon=True,
            name=f'search-{i}'
        )
        t.start()
        threads.append(t)

    time.sleep(1)

    log.info(f'[*] Starting schema reload thread ({ITERATIONS} iterations)...')
    log.info("")

    reload_thread = threading.Thread(
        target=schema_reload_worker,
        args=(inst,),
        name='schema-reload'
    )
    reload_thread.start()

    start_time = time.monotonic()
    try:
        while reload_thread.is_alive():
            reload_thread.join(timeout=5.0)
            elapsed = time.monotonic() - start_time# Enforce a hard upper bound so the test cannot hang indefinitely

            if elapsed > MAX_STRESS_TEST_DURATION:
                pytest.fail(
                    f"schema reload stress test exceeded max duration "
                    f"{MAX_STRESS_TEST_DURATION}s; aborting to avoid hang"
                )

            with stats_lock:
                s = dict(stats)
            reloads = s.get('reloads_ok', 0)
            searches = s.get('searches_ok', 0)
            conn_errs = (s.get('connection_errors', 0) +
                         s.get('reload_connection_errors', 0))
            crashed = s.get('crash_detected', 0)

            status = 'CRASH DETECTED' if crashed else 'running'
            log.info(f'  [{elapsed:6.1f}s] reloads={reloads}  searches={searches}  '
                  f'conn_errors={conn_errs}  status={status}')

            if crash_detected.is_set():
                break

    except KeyboardInterrupt:
        log.info('\n[!] Interrupted by user.')
        stop_event.set()

    log.info("Stopping threads...")
    stop_event.set()
    reload_thread.join(timeout=5)
    monitor.join(timeout=5)
    for t in threads:
        t.join(timeout=5)

    elapsed = time.monotonic() - start_time
    with stats_lock:
        s = dict(stats)

    log.info("")
    log.info('-' * 70)
    log.info('Results')
    log.info('-' * 70)
    log.info(f'  Elapsed time:      {elapsed:.1f}s')
    log.info(f'  Schema reloads:    {s.get("reloads_ok", 0)} ok, '
             f'{s.get("reloads_fail", 0)} fail')
    log.info(f'  LDAP searches:     {s.get("searches_ok", 0)} ok, '
             f'{s.get("searches_fail", 0)} fail')
    log.info(f'  Connection errors: {s.get("connection_errors", 0) + s.get("reload_connection_errors", 0)}')
    log.info("")

    if crash_detected.is_set():
        log.info('[+] *** CRASH DETECTED ***')
        log.info('[+] Server stopped responding during schema reload + search race.')
        log.info('[+] This is consistent with a use-after-free in attr_syntax_swap_ht().')
        log.info("")
        log.info('[+] Next steps:')
        log.info('    1. Check server logs for SIGSEGV/SIGABRT')
        log.info('    2. If running under ASan, check for heap-use-after-free report')
        log.info('    3. Check for core dump in /var/lib/dirsrv/slapd-<instance>/')
        log.info('    4. In gdb: bt should show attr_syntax_find or attr_syntax_return')
        log.info('       accessing freed memory from attr_syntax_swap_ht()')
        assert False
    else:
        alive = False
        try:
            alive = check_server_alive(inst)
        except Exception:
            pass

        if reload_failed.is_set():
            log.info('[+] *** RELOAD TASK FAILED ***')
            log.info('[+] Schema reload task failed.')
            log.info("")
            log.info('[+] Next steps:')
            log.info('    1. Check server logs for errors')
            assert False

        if not alive:
            log.info('[+] *** SERVER DOWN ***')
            log.info('[+] Server is no longer responding after the test run.')
            log.info('[+] Likely crashed from UAF during the race window.')
            log.info("")
            log.info('[+] Check server logs and core dumps.')
            assert False
        else:
            log.info('[-] Server survived all iterations.')
            log.info('    The race window was not hit in this run.')
            log.info("")
            log.info('    This is expected — the window is narrow. Suggestions:')
            log.info('    - Increase --iterations (try 2000-5000)')
            log.info('    - Increase --search-threads (try 16-32)')
            log.info('    - Run under ASan for better detection of stale accesses')
            log.info('    - Set MALLOC_CHECK_=3 to detect heap corruption earlier')
            log.info('    - Run on a system with more CPU cores for tighter interleaving')
            log.info('    - Repeat the test multiple times')


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
