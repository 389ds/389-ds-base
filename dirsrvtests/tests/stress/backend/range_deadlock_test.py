# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

"""Stress tests for transient BDB lock conflicts on indexed range searches.

Since #7125 index cursors run inside DB_TXN_NOWAIT transactions, so a
range scan (e.g. SSSD's "(entryUSN>=N)") fails with DBI_RC_RETRY (-12795)
whenever it hits a writer's page lock. Without a retry in
index_range_read_ext() every conflict failed the search with err=1:

    ERR - idl_new_range_fetch - Failed to build range candidate list on
          entryusn index. Error is -12795
    ERR - build_candidate_list - Database error -12795

This mirrors IPA production: the USN plugin updates the entryusn index on
every write while SSSD issues "(entryUSN>=N)" refresh searches. A fixed
server retries with backoff, logs transient attempts at debug level only,
and emits one ERR line per search that exhausts its retry budget.
"""

import os
import subprocess
import pytest
import logging
import ldap
from threading import Thread, Event, Lock
import random
import time
from test389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import USNPlugin
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.monitor import MonitorDatabase
from lib389.rootdse import RootDSE
from lib389.utils import get_default_db_lib

pytestmark = [pytest.mark.tier3,
              pytest.mark.skipif(get_default_db_lib() == "mdb",
                                 reason="Transient cursor deadlocks are BDB specific")]

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

# Test configuration (overridable from the environment)
NUM_USERS = int(os.getenv('RANGE_DEADLOCK_USERS', 2500))
DURATION = int(os.getenv('RANGE_DEADLOCK_DURATION', 300))
INSTR_DURATION = int(os.getenv('RANGE_DEADLOCK_INSTR_DURATION', 60))
STALE_DURATION = int(os.getenv('RANGE_DEADLOCK_STALE_DURATION', 60))
# Rewriting a small entry set keeps deleting the walks' start keys
HOT_SET = min(50, NUM_USERS)
# Full sweeps use a bound every run has entries above
SWEEP_MAX_USN = min(50, NUM_USERS)
RANGE_SEARCH_THREADS = int(os.getenv('RANGE_DEADLOCK_SEARCHERS', 16))
MODIFY_THREADS = int(os.getenv('RANGE_DEADLOCK_MODIFIERS', 12))
CHURN_THREADS = int(os.getenv('RANGE_DEADLOCK_CHURNERS', 4))
LDCLT_THREADS = 8
# Max fraction of searches that may fail with a logged retry exhaustion
MAX_SHED_RATIO = float(os.getenv('RANGE_DEADLOCK_MAX_SHED_RATIO', '0.01'))

PEOPLE_SUBTREE = f"ou=people,{DEFAULT_SUFFIX}"
# nsslapd-errorlog-level bit for SLAPI_LOG_BACKLDBM (proto-slap.h)
LDAP_DEBUG_BACKLDBM = 0x00080000

# Errors-log markers. FATAL/EXHAUST mean a search actually failed or gave
# up; ATTEMPT is the per-conflict ERR line only pre-fix servers emit; the
# rest are debug lines visible only with LDAP_DEBUG_BACKLDBM raised
# (ldbm_nasty() renders DBI_RC_RETRY as "... WARNING 4, err=-12795").
FATAL_LOG_PATTERN = '.*build_candidate_list - Database error -12795.*'
RANGE_EXHAUST_LOG_PATTERN = '.*Range read on the .* index gave up after .* attempts.*'
EQ_EXHAUST_LOG_PATTERN = '.*Index read on .* gave up after .* attempts.*'
ATTEMPT_LOG_PATTERN = '.*Failed to build range candidate list.*'
COLLISION_LOG_PATTERN = '.*WARNING 4, err=-12795.*'
RETRY_LOG_PATTERN = '.*DBI_RC_RETRY on range fetch retry.*'
EQ_RETRY_LOG_PATTERN = '.*index read retrying transaction WARNING 1045.*'
# -12797 means a walk gave up on a deleted start key; a fixed server
# resumes instead and logs it at debug level
NOTFOUND_LOG_PATTERN = '.*Failed to build range candidate list.*Error is -12797.*'
STALE_START_LOG_PATTERN = '.*Start key on .* was removed, resuming at its successor.*'

# Shared state (reset by each test)
stats_lock = Lock()
stats = {}
range_failures = []
empty_results = []
server_crashed = False


def _reset_state():
    """Reset the module-level shared state before a test run"""
    global stats, range_failures, empty_results, server_crashed
    with stats_lock:
        stats = {
            'searches': 0,
            'modifies': 0,
            'adds': 0,
            'deletes': 0,
            'benign_errors': 0,
            'start_time': time.time(),
        }
        range_failures = []
        empty_results = []
        server_crashed = False


def update_stats(operation, count=1):
    """Update operation statistics in a thread-safe manner"""
    with stats_lock:
        stats[operation] = stats.get(operation, 0) + count


def record_range_failure(thread_id, filter_str, err):
    """Record a client-visible range search failure (the bug symptom)"""
    with stats_lock:
        range_failures.append((thread_id, filter_str, str(err)))


def record_empty_result(thread_id, filter_str):
    """Record a success that returned an impossible empty result"""
    with stats_lock:
        empty_results.append((thread_id, filter_str))


def mark_server_crashed():
    """Mark that the server has crashed"""
    global server_crashed
    with stats_lock:
        server_crashed = True


def print_stats():
    """Print current statistics"""
    with stats_lock:
        if stats.get('start_time'):
            elapsed = time.time() - stats['start_time']
            log.info(f"Statistics after {elapsed:.1f}s: "
                     f"searches={stats['searches']}, "
                     f"modifies={stats['modifies']}, "
                     f"adds={stats['adds']}, "
                     f"deletes={stats['deletes']}, "
                     f"range_failures={len(range_failures)}, "
                     f"benign_errors={stats['benign_errors']}")


def open_worker_conn(inst):
    """Open an independent DM connection with short timeouts"""
    conn = inst.clone()
    conn.open()

    # Short timeout to fail fast
    conn.set_option(ldap.OPT_NETWORK_TIMEOUT, 5.0)
    conn.set_option(ldap.OPT_TIMEOUT, 30.0)
    return conn


def get_lastusn(conn):
    """Read lastusn;userroot from the rootdse, None if unavailable"""
    try:
        return RootDSE(conn).get_attr_val_int('lastusn;userroot')
    except (ldap.LDAPError, TypeError, ValueError):
        return None


def range_search_worker(stop_event, inst, thread_id):
    """Issue SSSD-style entryusn range searches.

    The searches stay raw on purpose: DSLdapObjects.filter() would AND
    objectClass terms into them, and the exact filter shape (bare vs AND
    form) decides how the server walks the entryusn index.

    ldap.OPERATIONS_ERROR here is the bug symptom and fails the test.
    """
    try:
        conn = open_worker_conn(inst)
    except Exception as e:
        log.error(f"Range search worker {thread_id} could not connect: {e}")
        return
    log.info(f"Range search worker {thread_id} started")
    search_count = 0
    lastusn = None
    filter_str = None

    try:
        while not stop_event.is_set():
            try:
                # Refresh lastusn periodically like an SSSD smart refresh would
                if lastusn is None or search_count % 50 == 0:
                    lastusn = get_lastusn(conn)

                dice = random.random()
                full_sweep = False
                if lastusn and dice < 0.4:
                    # Incremental refresh near the index tail
                    usn = max(1, lastusn - random.randint(0, 500))
                    filter_str = f"(entryusn>={usn})"
                elif lastusn and dice < 0.8:
                    # AND form: the range read is not sizelimit-bounded
                    usn = max(1, lastusn - random.randint(0, 2000))
                    filter_str = f"(&(objectClass=person)(entryusn>={usn}))"
                else:
                    # Full index sweep: can never be legitimately empty
                    filter_str = f"(entryusn>={random.randint(1, SWEEP_MAX_USN)})"
                    full_sweep = True

                entries = conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                        filter_str, attrlist=['1.1'])
                if full_sweep and not entries:
                    # a silent empty here means the candidate list was lost
                    record_empty_result(thread_id, filter_str)
                search_count += 1

                if search_count % 100 == 0:
                    update_stats('searches', 100)

            except ldap.SERVER_DOWN:
                log.error(f"Range search worker {thread_id}: SERVER DOWN - ns-slapd crashed!")
                mark_server_crashed()
                stop_event.set()
                break
            except ldap.OPERATIONS_ERROR as e:
                record_range_failure(thread_id, filter_str, e)
            except (ldap.SIZELIMIT_EXCEEDED, ldap.TIMELIMIT_EXCEEDED,
                    ldap.ADMINLIMIT_EXCEEDED):
                # Partial results are fine, the candidate list was built
                search_count += 1
            except Exception as e:
                update_stats('benign_errors')
                if search_count < 10:
                    log.debug(f"Range search worker {thread_id} error: {e}")
    finally:
        try:
            conn.close()
        except Exception:
            pass
        update_stats('searches', search_count % 100)
        log.info(f"Range search worker {thread_id} stopped (total searches: {search_count})")


def modify_worker(stop_event, inst, thread_id):
    """Modify random entries: with the USN plugin each modify is a
    delete+insert in the entryusn index."""
    try:
        conn = open_worker_conn(inst)
    except Exception as e:
        log.error(f"Modify worker {thread_id} could not connect: {e}")
        return
    log.info(f"Modify worker {thread_id} started")
    modify_count = 0

    try:
        while not stop_event.is_set():
            try:
                entry_num = random.randint(0, NUM_USERS - 1)
                user = UserAccount(conn,
                                   f"uid=user{entry_num:04d},{PEOPLE_SUBTREE}")
                value = f"mod_{thread_id}_{int(time.time() * 1000000)}"
                user.replace('description', value)
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
                update_stats('benign_errors')
                if modify_count < 10:
                    log.debug(f"Modify worker {thread_id} error: {e}")
    finally:
        try:
            conn.close()
        except Exception:
            pass
        update_stats('modifies', modify_count % 50)
        log.info(f"Modify worker {thread_id} stopped (total modifies: {modify_count})")


def churn_worker(stop_event, inst, thread_id):
    """Add/delete churn at the entryusn index tail"""
    try:
        conn = open_worker_conn(inst)
    except Exception as e:
        log.error(f"Churn worker {thread_id} could not connect: {e}")
        return
    log.info(f"Churn worker {thread_id} started")
    users = UserAccounts(conn, DEFAULT_SUFFIX)
    counter = 100000 + (thread_id * 100000)
    add_count = 0

    try:
        while not stop_event.is_set():
            uid = f'churn{counter}'
            try:
                user = users.create(properties={
                    'uid': uid,
                    'cn': f'Churn {counter}',
                    'sn': f'User{counter}',
                    'uidNumber': str(counter),
                    'gidNumber': str(counter),
                    'homeDirectory': f'/home/{uid}',
                })
                add_count += 1
                update_stats('adds')

                time.sleep(0.005)

                user.delete()
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
                    UserAccount(conn, f"uid={uid},{PEOPLE_SUBTREE}").delete()
                except Exception:
                    pass
                counter += 1
            except Exception as e:
                update_stats('benign_errors')
                if add_count < 10:
                    log.debug(f"Churn worker {thread_id} error: {e}")
                counter += 1
    finally:
        try:
            conn.close()
        except Exception:
            pass
        log.info(f"Churn worker {thread_id} stopped (total adds: {add_count})")


def tail_search_worker(stop_event, inst, thread_id):
    """Issue range searches starting at the newest entryusn key, the one
    the hot modify workers keep deleting and reinserting"""
    try:
        conn = open_worker_conn(inst)
    except Exception as e:
        log.error(f"Tail search worker {thread_id} could not connect: {e}")
        return
    log.info(f"Tail search worker {thread_id} started")
    search_count = 0
    filter_str = None

    try:
        while not stop_event.is_set():
            try:
                lastusn = get_lastusn(conn)
                if lastusn:
                    usn = max(1, lastusn - random.randint(0, 3))
                    filter_str = f"(entryusn>={usn})"
                else:
                    filter_str = "(entryusn>=1)"
                conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                              filter_str, attrlist=['1.1'])
                search_count += 1

                # A hot entry is never deleted and its committed entryusn
                # only grows, so this range can never be empty
                dn = f"uid=user{random.randint(0, HOT_SET - 1):04d}," \
                     f"{PEOPLE_SUBTREE}"
                res = conn.search_s(dn, ldap.SCOPE_BASE, '(objectClass=*)',
                                    attrlist=['entryusn'])
                usn_vals = {k.lower(): v
                            for k, v in res[0][1].items()}.get('entryusn')
                if usn_vals:
                    filter_str = f"(entryusn>={int(usn_vals[0])})"
                    entries = conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                            filter_str, attrlist=['1.1'])
                    if not entries:
                        record_empty_result(thread_id, filter_str)
                    search_count += 1

                # Interleave a sweep that can never be legitimately empty
                if search_count % 20 == 0:
                    probe = "(entryusn>=1)"
                    entries = conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                            probe, attrlist=['1.1'])
                    if not entries:
                        record_empty_result(thread_id, probe)
                    update_stats('searches', 20)

            except ldap.SERVER_DOWN:
                log.error(f"Tail search worker {thread_id}: SERVER DOWN - ns-slapd crashed!")
                mark_server_crashed()
                stop_event.set()
                break
            except ldap.OPERATIONS_ERROR as e:
                record_range_failure(thread_id, filter_str, e)
            except (ldap.SIZELIMIT_EXCEEDED, ldap.TIMELIMIT_EXCEEDED,
                    ldap.ADMINLIMIT_EXCEEDED):
                search_count += 1
            except Exception as e:
                update_stats('benign_errors')
                if search_count < 10:
                    log.debug(f"Tail search worker {thread_id} error: {e}")
    finally:
        # Flush the uncounted remainder
        update_stats('searches', search_count % 20)
        try:
            conn.close()
        except Exception:
            pass
        log.info(f"Tail search worker {thread_id} stopped (total searches: {search_count})")


def hot_modify_worker(stop_event, inst, thread_id):
    """Rewrite a small fixed entry set so its entryusn keys churn at the
    index tail as fast as possible"""
    try:
        conn = open_worker_conn(inst)
    except Exception as e:
        log.error(f"Hot modify worker {thread_id} could not connect: {e}")
        return
    log.info(f"Hot modify worker {thread_id} started")
    modify_count = 0
    seq = 0

    try:
        while not stop_event.is_set():
            try:
                entry_num = seq % HOT_SET
                seq += 1
                user = UserAccount(conn,
                                   f"uid=user{entry_num:04d},{PEOPLE_SUBTREE}")
                value = f"hot_{thread_id}_{int(time.time() * 1000000)}"
                user.replace('description', value)
                modify_count += 1

                if modify_count % 50 == 0:
                    update_stats('modifies', 50)

            except ldap.SERVER_DOWN:
                log.error(f"Hot modify worker {thread_id}: SERVER DOWN - ns-slapd crashed!")
                mark_server_crashed()
                stop_event.set()
                break
            except ldap.NO_SUCH_OBJECT:
                pass
            except Exception as e:
                update_stats('benign_errors')
                if modify_count < 10:
                    log.debug(f"Hot modify worker {thread_id} error: {e}")
    finally:
        # Flush the uncounted remainder
        update_stats('modifies', modify_count % 50)
        try:
            conn.close()
        except Exception:
            pass
        log.info(f"Hot modify worker {thread_id} stopped (total modifies: {modify_count})")


def monitor_worker(stop_event, duration):
    """Print stats every 30s and set the stop event when time is up"""
    log.info(f"Monitor worker started (duration: {duration}s)")
    start_time = time.time()
    last_stats_time = start_time

    while not stop_event.is_set():
        current_time = time.time()
        elapsed = current_time - start_time

        if current_time - last_stats_time >= 30:
            print_stats()
            log.info(f"Progress: {elapsed:.0f}s / {duration}s")
            last_stats_time = current_time

        if elapsed >= duration:
            log.info(f"Duration {duration}s reached")
            stop_event.set()
            break

        time.sleep(1)

    log.info("Monitor worker stopped")


@pytest.fixture(scope="module")
def range_stress_setup(topology_st):
    """Enable the USN plugin and create the user set shared by both tests"""
    inst = topology_st.standalone

    log.info("Enabling the USN plugin (entryusn maintenance)...")
    plugin = USNPlugin(inst)
    plugin.enable()
    inst.restart()

    # Buffer the access log, the run is search/write heavy
    inst.config.set('nsslapd-accesslog-logbuffering', 'on')

    # A bare instance may lack the parent OU
    OrganizationalUnits(inst, DEFAULT_SUFFIX).ensure_state(
        properties={'ou': 'people'})

    log.info(f"Creating {NUM_USERS} users under {PEOPLE_SUBTREE}...")
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    created = 0
    for i in range(NUM_USERS):
        uid = f'user{i:04d}'
        try:
            users.create(properties={
                'uid': uid,
                'cn': f'Test User {i}',
                'sn': f'User{i}',
                'uidNumber': str(i + 1000),
                'gidNumber': str(i + 1000),
                'homeDirectory': f'/home/{uid}',
                'description': f'initial description {i}',
            })
            created += 1
        except ldap.ALREADY_EXISTS:
            pass
        except ldap.NO_SUCH_OBJECT:
            pytest.fail(f"{PEOPLE_SUBTREE} does not exist - the run would be "
                        f"vacuous without test entries")
        if (i + 1) % 500 == 0:
            log.info(f"Created {i + 1}/{NUM_USERS} entries...")
    log.info(f"User setup complete ({created} new entries)")

    return topology_st


def _start_search_threads(stop_event, inst):
    """Start the range search workers, returns the thread list"""
    threads = []
    for i in range(RANGE_SEARCH_THREADS):
        t = Thread(target=range_search_worker,
                   args=(stop_event, inst, i),
                   name=f"range-search-{i}")
        t.daemon = True
        t.start()
        threads.append(t)
    return threads


def _db_monitor_snapshot(inst):
    """Sample the BDB lock/txn counters (instance must be running)"""
    wanted = ('nsslapd-db-deadlock-rate', 'nsslapd-db-lock-conflicts',
              'nsslapd-db-abort-rate', 'nsslapd-db-lock-request-rate',
              'nsslapd-db-lockers')
    snapshot = {}
    try:
        status = MonitorDatabase(inst).get_status()
        for key, val in status.items():
            if key.lower() in wanted:
                try:
                    snapshot[key.lower()] = int(val[0])
                except (ValueError, IndexError, TypeError):
                    pass
    except Exception as e:
        log.warning(f"Could not read the database monitor: {e}")
    return snapshot


def _range_log_counts(inst):
    """Return (pre-fix attempt count, fatal line list, exhaustion count,
    stale-start-key -12797 count)"""
    attempts = [line for line in inst.ds_error_log.match(ATTEMPT_LOG_PATTERN)
                if '-12795' in line]
    fatal = inst.ds_error_log.match(FATAL_LOG_PATTERN)
    exhausted = (len(inst.ds_error_log.match(RANGE_EXHAUST_LOG_PATTERN)) +
                 len(inst.ds_error_log.match(EQ_EXHAUST_LOG_PATTERN)))
    notfound = inst.ds_error_log.match(NOTFOUND_LOG_PATTERN)
    return len(attempts), fatal, exhausted, len(notfound)


def _join_workers(threads, stop_event, duration):
    """Join the workers; force the stop and fail instead of hanging if the
    monitor died before signalling the end of the run."""
    deadline = time.time() + duration + 120
    for t in threads:
        t.join(max(1.0, deadline - time.time()))
    if any(t.is_alive() for t in threads):
        stop_event.set()
        for t in threads:
            t.join(60)
        stuck = [t.name for t in threads if t.is_alive()]
        if stuck:
            pytest.fail(f"Worker threads did not stop: {stuck}")


def _finish_and_assert(inst, mon_before=None, log_base=None):
    """Apply the shared pass/fail criteria to this test's window"""
    log.info("=" * 72)
    log.info("Final statistics:")
    print_stats()

    # NOWAIT rejections never engage the deadlock detector, so contention
    # shows up in the lock-conflict/abort deltas, not in deadlock-rate
    mon_delta = {}
    if mon_before is not None:
        mon_after = _db_monitor_snapshot(inst)
        for key in sorted(set(mon_before) | set(mon_after)):
            before = mon_before.get(key, 0)
            after = mon_after.get(key, 0)
            mon_delta[key] = after - before
            log.info(f"  {key}: {before} -> {after} (delta {after - before})")

    # The instance is shared by the tests: judge only this test's window
    base = log_base if log_base is not None else (0, [], 0, 0)
    attempts_total, fatal_lines, exhausted_total, notfound_total = \
        _range_log_counts(inst)
    attempts = attempts_total - base[0]
    fatal = len(fatal_lines) - len(base[1])
    exhausted = exhausted_total - base[2]
    notfound = notfound_total - base[3]
    if min(attempts, fatal, exhausted, notfound) < 0:
        log.warning("The errors log shrank during the test (rotation?) - "
                    "windowed log counts are unreliable for this run")
        attempts, fatal = max(attempts, 0), max(fatal, 0)
        exhausted = max(exhausted, 0)
        notfound = max(notfound, 0)
    contended = (mon_delta.get('nsslapd-db-lock-conflicts', 0) > 0 or
                 mon_delta.get('nsslapd-db-abort-rate', 0) > 0 or
                 attempts > 0)
    log.info(f"Pre-fix style per-attempt errors: {attempts}")
    log.info(f"Searches that failed (fatal):     {fatal}")
    log.info(f"Retry exhaustion notices:         {exhausted}")
    log.info(f"Stale start key errors (-12797):  {notfound}")
    if fatal == 0 and contended:
        log.info("The database was contended and no range search failed - "
                 "the range fetch retries correctly")
    elif fatal:
        log.info(f"{fatal} search(es) failed - either this server has no "
                 f"range-fetch retry, or contention exceeded its budget")
    else:
        log.warning("No contention signal at all (no monitor delta, no "
                    "attempt errors) - this run proves nothing either way. "
                    "Use a longer RANGE_DEADLOCK_DURATION or more "
                    "RANGE_DEADLOCK_MODIFIERS.")
    log.info("=" * 72)

    if server_crashed:
        pytest.fail("ns-slapd crashed during the range deadlock test")

    # A run where every searcher died must not pass vacuously
    searches = stats.get('searches', 0)
    assert searches > 0, \
        "no searches were performed - the search workers failed to start"

    if empty_results:
        sample = "\n".join(f"  thread={t} filter={f}"
                           for t, f in empty_results[:10])
        pytest.fail(f"{len(empty_results)} range search(es) silently returned "
                    f"an empty result for a filter that cannot be empty "
                    f"(stale start key lost the candidate list), sample:\n"
                    f"{sample}")

    # err=1 is allowed only as rare load shedding: every failure needs a
    # logged retry exhaustion and the rate must stay small
    failures = len(range_failures)
    if failures > exhausted:
        sample = "\n".join(f"  thread={t} filter={f} err={e}"
                           for t, f, e in range_failures[:10])
        pytest.fail(f"{failures} range search(es) failed with "
                    f"OPERATIONS_ERROR but only {exhausted} retry exhaustion "
                    f"notice(s) were logged - failures without a retried, "
                    f"exhausted walk, sample:\n{sample}")
    if failures > searches * MAX_SHED_RATIO:
        sample = "\n".join(f"  thread={t} filter={f} err={e}"
                           for t, f, e in range_failures[:10])
        pytest.fail(f"{failures} of {searches} range search(es) failed "
                    f"({failures / searches:.2%}), above the "
                    f"{MAX_SHED_RATIO:.2%} load-shedding bound, sample:\n"
                    f"{sample}")

    assert fatal <= exhausted, (
        f"errors log gained {fatal} 'build_candidate_list - Database error "
        f"-12795' line(s) but only {exhausted} retry exhaustion notice(s): "
        f"range searches failed without exhausting the retry budget, last: "
        f"{fatal_lines[-1] if fatal_lines else ''}")

    assert notfound == 0, \
        (f"errors log gained {notfound} 'Error is -12797' line(s): a range "
         f"walk gave up because its start key was deleted instead of "
         f"resuming at its successor")


def test_entryusn_range_deadlock_threads(range_stress_setup):
    """Range searches on a write-hot entryusn index must not fail on
    transient deadlocks

    :id: 5aa79381-1b0f-43d6-960b-d1251e100927
    :setup: Standalone instance, USN plugin enabled, 2500 users
    :steps:
        1. Start threads issuing SSSD-style "(entryusn>=N)" range searches
        2. Start modify threads bumping entryUSN on random entries and
           add/delete churn threads, run the mixed load for the configured
           duration
        3. Check client results and the errors log
    :expectedresults:
        1. Success
        2. Server stays up for the whole run
        3. No silent empty results; any err=1 failure is an explained
           retry exhaustion within the load-shedding bound
    """
    inst = range_stress_setup.standalone

    log.info("=" * 72)
    log.info("entryusn range deadlock test (pure threading)")
    log.info(f"Duration: {DURATION}s, users: {NUM_USERS}, "
             f"searchers: {RANGE_SEARCH_THREADS}, modifiers: {MODIFY_THREADS}, "
             f"churners: {CHURN_THREADS}")
    log.info("=" * 72)

    _reset_state()
    mon_before = _db_monitor_snapshot(inst)
    log_base = _range_log_counts(inst)
    stop_event = Event()
    threads = _start_search_threads(stop_event, inst)

    for i in range(MODIFY_THREADS):
        t = Thread(target=modify_worker,
                   args=(stop_event, inst, i),
                   name=f"modify-{i}")
        t.daemon = True
        t.start()
        threads.append(t)

    for i in range(CHURN_THREADS):
        t = Thread(target=churn_worker,
                   args=(stop_event, inst, i),
                   name=f"churn-{i}")
        t.daemon = True
        t.start()
        threads.append(t)

    monitor_thread = Thread(target=monitor_worker,
                            args=(stop_event, DURATION),
                            name="monitor")
    monitor_thread.daemon = True
    monitor_thread.start()
    threads.append(monitor_thread)

    log.info(f"Started {len(threads)} worker threads, running for {DURATION}s...")

    _join_workers(threads, stop_event, DURATION)
    assert stats.get('modifies', 0) > 0, \
        "no modifies were performed - the write workers failed to start"
    _finish_and_assert(inst, mon_before, log_base)


def test_entryusn_range_deadlock_ldclt(range_stress_setup):
    """Range searches must survive an ldclt-driven write storm on the
    entryusn index

    :id: c026d4ac-cd43-445c-af9c-578f1081bc53
    :setup: Standalone instance, USN plugin enabled, 2500 users
    :steps:
        1. Start an ldclt modify storm replacing description on random
           users (each modify bumps entryUSN)
        2. Run SSSD-style "(entryusn>=N)" range search threads alongside
           it for the configured duration
        3. Stop ldclt, check client results and the errors log
    :expectedresults:
        1. Success
        2. Server stays up for the whole run
        3. No silent empty results; any err=1 failure is an explained
           retry exhaustion within the load-shedding bound
    """
    inst = range_stress_setup.standalone

    ldclt_bin = os.path.join(inst.get_bin_dir(), 'ldclt')
    if not os.path.exists(ldclt_bin):
        pytest.skip("ldclt binary is not available")

    # Random-target description replace; -I 32 ignores NO_SUCH_OBJECT and
    # the mask width must match the {i:04d} DN format of the created users
    digits = max(4, len(str(NUM_USERS - 1)))
    cmd = [
        ldclt_bin,
        '-h', inst.host,
        '-p', str(inst.port),
        '-D', inst.binddn,
        '-w', inst.bindpw,
        '-b', PEOPLE_SUBTREE,
        '-f', f"uid=user{'X' * digits}",
        '-e', 'random',
        '-r0',
        f'-R{NUM_USERS - 1}',
        '-e', f"attreplace=description: ldclt stress {'X' * digits}",
        '-n', str(LDCLT_THREADS),
        '-I', '32',
    ]

    log.info("=" * 72)
    log.info("entryusn range deadlock test (ldclt write storm)")
    log.info(f"Duration: {DURATION}s, ldclt threads: {LDCLT_THREADS}, "
             f"searchers: {RANGE_SEARCH_THREADS}")
    log.info(f"ldclt command: {' '.join(cmd)}")
    log.info("=" * 72)

    _reset_state()
    mon_before = _db_monitor_snapshot(inst)
    log_base = _range_log_counts(inst)
    stop_event = Event()

    # Keep ldclt's output: an early exit is undiagnosable without it
    ldclt_out_path = os.path.join(os.getcwd(), 'range_deadlock_ldclt.out')
    ldclt_out = open(ldclt_out_path, 'w+b')
    ldclt_proc = subprocess.Popen(cmd, stdout=ldclt_out,
                                  stderr=subprocess.STDOUT)

    def ldclt_tail():
        """Read back whatever ldclt has written so far"""
        try:
            ldclt_out.flush()
            with open(ldclt_out_path, 'r', errors='replace') as fh:
                return fh.read()[-2000:]
        except Exception:
            return '(no output captured)'

    try:
        # Fail early if ldclt exited immediately (bad flags, bind failure)
        time.sleep(2)
        if ldclt_proc.poll() is not None:
            pytest.skip(f"ldclt exited immediately with rc="
                        f"{ldclt_proc.returncode}, command: {' '.join(cmd)}\n"
                        f"output:\n{ldclt_tail()}")

        threads = _start_search_threads(stop_event, inst)

        monitor_thread = Thread(target=monitor_worker,
                                args=(stop_event, DURATION),
                                name="monitor")
        monitor_thread.daemon = True
        monitor_thread.start()
        threads.append(monitor_thread)

        log.info(f"ldclt storm running (pid {ldclt_proc.pid}), "
                 f"{len(threads)} python threads, running for {DURATION}s...")

        _join_workers(threads, stop_event, DURATION)

        # ldclt must survive the full run or the pass means nothing; a
        # failure already collected wins over the skip
        if ldclt_proc.poll() is not None:
            stop_event.set()
            if range_failures:
                sample = "\n".join(f"  thread={t} filter={f} err={e}"
                                   for t, f, e in range_failures[:10])
                pytest.fail(f"ldclt exited early AND {len(range_failures)} "
                            f"range search(es) failed with OPERATIONS_ERROR, "
                            f"sample:\n{sample}")
            pytest.skip(f"ldclt exited early (rc={ldclt_proc.returncode}) so "
                        f"the write load was not sustained for the full "
                        f"{DURATION}s; this run is inconclusive.\n"
                        f"output:\n{ldclt_tail()}")

        _finish_and_assert(inst, mon_before, log_base)
    finally:
        stop_event.set()
        if ldclt_proc.poll() is None:
            ldclt_proc.terminate()
            try:
                ldclt_proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                ldclt_proc.kill()
                ldclt_proc.wait()
        try:
            ldclt_out.close()
        except Exception:
            pass


def test_entryusn_range_collision_rate(range_stress_setup):
    """Measure how often range fetches hit a transient lock error

    At the default log level a clean run and a run where retries silently
    absorbed thousands of conflicts look identical. Raise the log level for
    a short window and count the collisions and retries directly.

    :id: e5311429-7586-48f0-95ea-201ea8db3e1a
    :setup: Standalone instance, USN plugin enabled, 2500 users
    :steps:
        1. Raise nsslapd-errorlog-level to include LDAP_DEBUG_BACKLDBM
        2. Run the mixed range-search / write load for a short window
        3. Restore the error log level and count the debug-level collision
           and retry markers against the BDB lock counters
    :expectedresults:
        1. Success
        2. Server stays up, no search fails
        3. Collision and retry counts are reported; no search failed and no
           retry budget was exhausted
    """
    inst = range_stress_setup.standalone

    orig_level = inst.config.get_attr_val_utf8('nsslapd-errorlog-level') or '0'
    instr_level = int(orig_level) | LDAP_DEBUG_BACKLDBM

    log.info("=" * 72)
    log.info("entryusn range collision rate (BACKLDBM instrumented)")
    log.info(f"Duration: {INSTR_DURATION}s, searchers: {RANGE_SEARCH_THREADS}, "
             f"modifiers: {MODIFY_THREADS}, churners: {CHURN_THREADS}")
    log.info(f"errorlog-level: {orig_level} -> {instr_level}")
    log.info("=" * 72)

    _reset_state()
    stop_event = Event()
    threads = []
    try:
        inst.config.replace('nsslapd-errorlog-level', str(instr_level))

        mon_before = _db_monitor_snapshot(inst)
        base_collisions = len(inst.ds_error_log.match(COLLISION_LOG_PATTERN))
        base_retries = len(inst.ds_error_log.match(RETRY_LOG_PATTERN))
        base_eq_retries = len(inst.ds_error_log.match(EQ_RETRY_LOG_PATTERN))
        base_fatal = len(inst.ds_error_log.match(FATAL_LOG_PATTERN))
        base_exhausted = len(inst.ds_error_log.match(RANGE_EXHAUST_LOG_PATTERN))

        threads = _start_search_threads(stop_event, inst)
        for i in range(MODIFY_THREADS):
            t = Thread(target=modify_worker,
                       args=(stop_event, inst, i),
                       name=f"modify-{i}")
            t.daemon = True
            t.start()
            threads.append(t)
        for i in range(CHURN_THREADS):
            t = Thread(target=churn_worker,
                       args=(stop_event, inst, i),
                       name=f"churn-{i}")
            t.daemon = True
            t.start()
            threads.append(t)
        monitor_thread = Thread(target=monitor_worker,
                                args=(stop_event, INSTR_DURATION),
                                name="monitor")
        monitor_thread.daemon = True
        monitor_thread.start()
        threads.append(monitor_thread)

        log.info(f"Started {len(threads)} worker threads for {INSTR_DURATION}s...")
        _join_workers(threads, stop_event, INSTR_DURATION)

        collisions = len(inst.ds_error_log.match(COLLISION_LOG_PATTERN)) - base_collisions
        retries = len(inst.ds_error_log.match(RETRY_LOG_PATTERN)) - base_retries
        eq_retries = len(inst.ds_error_log.match(EQ_RETRY_LOG_PATTERN)) - base_eq_retries
        fatal = len(inst.ds_error_log.match(FATAL_LOG_PATTERN)) - base_fatal
        exhausted = len(inst.ds_error_log.match(RANGE_EXHAUST_LOG_PATTERN)) - base_exhausted
        mon_after = _db_monitor_snapshot(inst)
    finally:
        stop_event.set()
        try:
            inst.config.replace('nsslapd-errorlog-level', str(orig_level))
        except Exception as e:
            log.warning(f"Could not restore nsslapd-errorlog-level: {e}")

    searches = stats.get('searches', 0)
    writes = stats.get('modifies', 0) + stats.get('adds', 0) + stats.get('deletes', 0)
    lock_conflicts = (mon_after.get('nsslapd-db-lock-conflicts', 0) -
                      mon_before.get('nsslapd-db-lock-conflicts', 0))
    aborts = (mon_after.get('nsslapd-db-abort-rate', 0) -
              mon_before.get('nsslapd-db-abort-rate', 0))

    log.info("=" * 72)
    log.info("Collision rate measurement")
    log.info(f"  searches issued                 : {searches}")
    log.info(f"  writes issued                   : {writes}")
    log.info(f"  range walks hitting a conflict  : {collisions}")
    log.info(f"  range fetch retries performed   : {retries}")
    log.info(f"  equality fetch retries performed: {eq_retries}")
    log.info(f"  retry budgets exhausted         : {exhausted}")
    log.info(f"  searches that failed (fatal)    : {fatal}")
    log.info(f"  BDB lock conflicts (delta)      : {lock_conflicts}")
    log.info(f"  BDB txn aborts (delta)          : {aborts}")
    if searches:
        log.info(f"  collisions per 1000 searches    : "
                 f"{1000.0 * collisions / searches:.2f}")

    db_busy = lock_conflicts > 0 or aborts > 0
    if collisions == 0 and db_busy:
        log.info("VERDICT: the database was busy yet no range walk hit a "
                 "transient lock error - readers did not collide with "
                 "writers in this window")
    elif collisions and not fatal:
        log.info(f"VERDICT: {collisions} range walk(s) hit a transient lock "
                 f"error and the retry loop absorbed all of them")
    elif not db_busy:
        log.warning("VERDICT: no contention signal at all - this run measured "
                    "nothing. Raise RANGE_DEADLOCK_INSTR_DURATION or "
                    "RANGE_DEADLOCK_MODIFIERS.")
    log.info("=" * 72)

    if server_crashed:
        pytest.fail("ns-slapd crashed during the instrumented run")
    assert searches > 0, "no searches were performed"
    assert writes > 0, "no writes were performed"
    assert not range_failures, \
        f"{len(range_failures)} range search(es) failed with OPERATIONS_ERROR"
    assert fatal == 0, f"{fatal} search(es) failed with a database error"
    assert exhausted == 0, \
        f"{exhausted} range search(es) exhausted the retry budget"


def test_entryusn_range_stale_start_key(range_stress_setup):
    """A range walk whose start key was deleted must resume at the key's
    successor, not fail or return an empty result

    :id: b3f1c2d8-9a41-4f6e-8d2f-7c5a91e04b23
    :setup: Standalone instance, USN plugin enabled, 2500 users
    :steps:
        1. Raise nsslapd-errorlog-level to include LDAP_DEBUG_BACKLDBM
        2. Run searchers whose range starts at the newest entryusn key
           against modify workers that keep rewriting a small entry set,
           so retried walks find their start key deleted
        3. Restore the log level, check client results and the errors log
    :expectedresults:
        1. Success
        2. Server stays up, no search fails beyond the explained
           load-shedding bound, no search silently returns an impossible
           empty result
        3. No "Error is -12797" line in the errors log; resumptions are
           visible at debug level when the scenario occurred
    """
    inst = range_stress_setup.standalone

    orig_level = inst.config.get_attr_val_utf8('nsslapd-errorlog-level') or '0'
    instr_level = int(orig_level) | LDAP_DEBUG_BACKLDBM

    log.info("=" * 72)
    log.info("entryusn range stale start key (BACKLDBM instrumented)")
    log.info(f"Duration: {STALE_DURATION}s, tail searchers: {RANGE_SEARCH_THREADS}, "
             f"hot modifiers: {MODIFY_THREADS} over {HOT_SET} entries")
    log.info("=" * 72)

    _reset_state()
    stop_event = Event()
    threads = []
    try:
        inst.config.replace('nsslapd-errorlog-level', str(instr_level))

        mon_before = _db_monitor_snapshot(inst)
        log_base = _range_log_counts(inst)
        base_stale = len(inst.ds_error_log.match(STALE_START_LOG_PATTERN))
        base_retries = len(inst.ds_error_log.match(RETRY_LOG_PATTERN))

        for i in range(RANGE_SEARCH_THREADS):
            t = Thread(target=tail_search_worker,
                       args=(stop_event, inst, i),
                       name=f"tail-search-{i}")
            t.daemon = True
            t.start()
            threads.append(t)
        for i in range(MODIFY_THREADS):
            t = Thread(target=hot_modify_worker,
                       args=(stop_event, inst, i),
                       name=f"hot-modify-{i}")
            t.daemon = True
            t.start()
            threads.append(t)
        monitor_thread = Thread(target=monitor_worker,
                                args=(stop_event, STALE_DURATION),
                                name="monitor")
        monitor_thread.daemon = True
        monitor_thread.start()
        threads.append(monitor_thread)

        log.info(f"Started {len(threads)} worker threads for {STALE_DURATION}s...")
        _join_workers(threads, stop_event, STALE_DURATION)

        stale = len(inst.ds_error_log.match(STALE_START_LOG_PATTERN)) - base_stale
        retries = len(inst.ds_error_log.match(RETRY_LOG_PATTERN)) - base_retries
        if min(stale, retries) < 0:
            log.warning("The errors log shrank during the test (rotation?) - "
                        "windowed log counts are unreliable for this run")
            stale, retries = max(stale, 0), max(retries, 0)
    finally:
        stop_event.set()
        try:
            inst.config.replace('nsslapd-errorlog-level', str(orig_level))
        except Exception as e:
            log.warning(f"Could not restore nsslapd-errorlog-level: {e}")

    log.info("=" * 72)
    log.info("Stale start key measurement")
    log.info(f"  searches issued            : {stats.get('searches', 0)}")
    log.info(f"  modifies issued            : {stats.get('modifies', 0)}")
    log.info(f"  range fetch retries        : {retries}")
    log.info(f"  stale start keys resumed   : {stale}")
    log.info(f"  impossible empty results   : {len(empty_results)}")
    if stale:
        log.info("VERDICT: walks lost their start key and resumed at its "
                 "successor correctly")
    elif retries:
        log.info("VERDICT: retries occurred but no start key was lost in "
                 "this window - stale-key path not exercised")
    else:
        log.warning("VERDICT: no retries at all - this run proves nothing. "
                    "Raise RANGE_DEADLOCK_STALE_DURATION or "
                    "RANGE_DEADLOCK_MODIFIERS.")
    log.info("=" * 72)

    if server_crashed:
        pytest.fail("ns-slapd crashed during the stale start key test")
    assert stats.get('searches', 0) > 0, "no searches were performed"
    assert stats.get('modifies', 0) > 0, "no modifies were performed"
    _finish_and_assert(inst, mon_before, log_base)
    if stale == 0:
        pytest.skip("no start key was lost in this window; the run is "
                    "inconclusive for the stale start key scenario")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", "-v", CURRENT_FILE])
