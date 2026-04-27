# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import ldap
import pytest
import os
import threading
import time
from lib389.monitor import *
from lib389.backend import Backends, DatabaseConfig
from lib389._constants import *
from test389.topologies import topology_st as topo
from lib389._mapped_object import DSLdapObjects
from lib389.utils import get_default_db_lib
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_monitor(topo):
    """This test is to display monitor attributes to check the performace

    :id: f7c8a815-07cf-4e67-9574-d26a0937d3db
    :setup: Single instance
    :steps:
        1. Get the cn=monitor connections attributes
        2. Print connections attributes
        3. Get the cn=monitor version
        4. Print cn=monitor version
        5. Get the cn=monitor threads attributes
        6. Print cn=monitor threads attributes
        7. Get cn=monitor backends attributes
        8. Print cn=monitor backends attributes
        9. Get cn=monitor operations attributes
        10. Print cn=monitor operations attributes
        11. Get cn=monitor statistics attributes
        12. Print cn=monitor statistics attributes
    :expectedresults:
        1. cn=monitor attributes should be fetched and printed successfully.
    """

    #define the monitor object from Monitor class in lib389
    monitor = Monitor(topo.standalone)

    #get monitor connections
    connections = monitor.get_connections()
    log.info('connection: {0[0]}, currentconnections: {0[1]}, totalconnections: {0[2]}'.format(connections))

    #get monitor version
    version = monitor.get_version()
    log.info('version :: %s' %version)

    #get monitor threads
    threads = monitor.get_threads()
    log.info('threads: {0[0]},currentconnectionsatmaxthreads: {0[1]},maxthreadsperconnhits: {0[2]}'.format(threads))

    #get monitor backends
    backend = monitor.get_backends()
    log.info('nbackends: {0[0]}, backendmonitordn: {0[1]}'.format(backend))

    #get monitor operations
    operations = monitor.get_operations()
    log.info('opsinitiated: {0[0]}, opscompleted: {0[1]}'.format(operations))

    #get monitor stats
    stats = monitor.get_statistics()
    log.info('dtablesize: {0[0]},readwaiters: {0[1]},entriessent: {0[2]},bytessent: {0[3]},currenttime: {0[4]},starttime: {0[5]}'.format(stats))


def test_monitor_ldbm(topo):
    """This test is to check if we are getting the correct monitor entry

    :id: e62ba369-32f5-4b03-8865-f597a5bb6a70
    :setup: Single instance
    :steps:
        1. Get the backend library (bdb, ldbm, etc)
        2. Get the database monitor
        3. Check for expected attributes in output
        4. Check for expected DB library specific attributes
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    # Are we using BDB?
    db_config = DatabaseConfig(topo.standalone)
    db_lib = db_config.get_db_lib()

    # Get the database monitor entry
    monitor = MonitorLDBM(topo.standalone).get_status()

    # Check that known attributes exist (only NDN cache stats)
    assert 'normalizeddncachehits' in monitor
    # Check for library specific attributes
    if db_lib == 'bdb':
        assert 'dbcachehits' in monitor
        assert 'nsslapd-db-configured-locks' in monitor
    elif db_lib == 'mdb':
        assert 'dbcachehits' not in monitor
    else:
        # Unknown - the server would probably fail to start but check it anyway
        log.fatal(f'Unknown backend library: {db_lib}')
        assert False


def test_monitor_backend(topo):
    """This test is to check if we are getting the correct backend monitor entry

    :id: 27b0534f-a18c-4c95-aa2b-936bc1886a7b
    :setup: Single instance
    :steps:
        1. Get the backend library (bdb, ldbm, etc)
        2. Get the backend monitor
        3. Check for expected attributes in output
        4. Check for expected DB library specific attributes
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    # Are we using BDB?
    db_lib = topo.standalone.get_db_lib()

    # Get the backend monitor
    be = Backends(topo.standalone).list()[0]
    monitor = be.get_monitor().get_status()

    # Check for expected attributes
    assert 'entrycachehits' in monitor

    # Check for library specific attributes
    if db_lib == 'bdb':
        assert 'dncachehits' in monitor
        assert 'dbfilename-0' in monitor
    elif db_lib == 'mdb':
        assert 'dbiname-1' in monitor
        pass
    else:
        # Unknown - the server would probably fail to start but check it anyway
        log.fatal(f'Unknown backend library: {db_lib}')
        assert False


def test_num_subordinates_with_monitor_suffix(topo):
    """This test is to compare the numSubordinates value on the root entry with the actual number of direct subordinate(s).

    :id: fdcfe0ac-33c3-4252-bf38-79819ec58a51
    :setup: Single instance
    :steps:
        1. Create sample entries and perform a search with basedn as cn=monitor, filter as "(objectclass=*)" and scope as base.
        2. Extract the numSubordinates value.
        3. Perform another search with basedn as cn=monitor, filter as "(\|(objectclass=*)(objectclass=ldapsubentry))" and scope as one.
        4. Compare numSubordinates value with the number of sub-entries.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Should be same
    """

    raw_objects = DSLdapObjects(topo.standalone, basedn='cn=monitor')
    filter1 = raw_objects.filter("(objectclass=*)", scope=0)
    num_subordinates_val = filter1[0].get_attr_val_int('numSubordinates')
    filter2 = raw_objects.filter("(|(objectclass=*)(objectclass=ldapsubentry))",scope=1)
    assert len(filter2) == num_subordinates_val


def test_monitor_connections(topo):
    """Validates connection monitoring functionality.

    :id: 34eb85c7-00cf-4e72-8467-a431040150a0
    :setup: Standalone Instance
    :steps:
        1. Clone the DirSrv object to create a new connection handle.
        2. Open a persistent connection to the server.
        3. Query the 'cn=monitor' entry to fetch connection statistics.
        4. Verify that the 'currentconnections' count is greater than 0.
        5. Close the persistent connection.
    :expectedresults:
        1. The connection is established successfully.
        2. The server's monitoring entry returns valid statistics.
        3. The number of current connections reported is at least 1.
        4. The connection is closed successfully.
    """
    inst1 = topo.standalone
    monitor = Monitor(inst1)

    conn = inst1.clone()
    try:
        conn.open()
        log.info('LDAP connection established for monitoring check via conn.open()')

        _, current_connections, _ = monitor.get_connections()
        num_conns = int(current_connections[0])
        log.info(f"Reported current connections: {num_conns}")

        assert num_conns > 0

    finally:
        conn.close()
        log.info("LDAP monitoring connection closed via conn.close()")


def test_monitor_work_queue(topo):
    """Verify work queue metrics are exposed via cn=monitor

    :id: 7e2f74ac-e662-4e70-b4f3-a010295d8b99
    :setup: Standalone Instance
    :steps:
        1. Query cn=monitor for work queue attributes
        2. Verify all 4 attributes are present and parseable as integers
        3. Verify current values are non-negative
        4. Verify max values >= current values
        5. Generate some load and verify maxbusyworkers > 0
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    inst = topo.standalone
    monitor = Monitor(inst)

    # Get work queue metrics
    (currentworkqueue, maxworkqueue,
     currentbusyworkers, maxbusyworkers) = monitor.get_work_queue()

    # Verify all attributes are present and parseable as integers
    cur_wq = int(currentworkqueue[0])
    max_wq = int(maxworkqueue[0])
    cur_bw = int(currentbusyworkers[0])
    max_bw = int(maxbusyworkers[0])

    log.info(f"currentworkqueue={cur_wq}, maxworkqueue={max_wq}, "
             f"currentbusyworkers={cur_bw}, maxbusyworkers={max_bw}")

    # Verify non-negative values
    assert cur_wq >= 0, f"currentworkqueue should be >= 0, got {cur_wq}"
    assert max_wq >= 0, f"maxworkqueue should be >= 0, got {max_wq}"
    assert cur_bw >= 0, f"currentbusyworkers should be >= 0, got {cur_bw}"
    assert max_bw >= 0, f"maxbusyworkers should be >= 0, got {max_bw}"

    # Verify max >= current
    assert max_wq >= cur_wq, f"maxworkqueue ({max_wq}) should be >= currentworkqueue ({cur_wq})"
    assert max_bw >= cur_bw, f"maxbusyworkers ({max_bw}) should be >= currentbusyworkers ({cur_bw})"

    # Generate some load - several searches to ensure at least one worker was busy
    for _ in range(10):
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=*)')

    # Re-check - maxbusyworkers should now be > 0
    (_, _, _, maxbusyworkers) = monitor.get_work_queue()
    max_bw = int(maxbusyworkers[0])
    log.info(f"After load: maxbusyworkers={max_bw}")
    assert max_bw > 0, f"maxbusyworkers should be > 0 after generating load, got {max_bw}"

    # Also verify the attributes appear in get_status()
    status = monitor.get_status()
    assert 'currentworkqueue' in status
    assert 'maxworkqueue' in status
    assert 'currentbusyworkers' in status
    assert 'maxbusyworkers' in status


def test_monitor_busy_workers_concurrent(topo):
    """Verify currentbusyworkers increments under concurrent operations

    :id: b86971df-3627-499a-975c-f46c47b199fc
    :setup: Standalone Instance
    :steps:
        1. Record maxbusyworkers before load
        2. Launch multiple threads performing searches concurrently
        3. While threads are running, sample currentbusyworkers
        4. Verify maxbusyworkers increased and currentbusyworkers
           returns to a low value after threads finish
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    inst = topo.standalone
    monitor = Monitor(inst)

    NUM_THREADS = 10
    SEARCHES_PER_THREAD = 50
    errors = []
    peak_busy = [0]
    peak_lock = threading.Lock()

    def search_worker():
        """Perform repeated searches to keep a worker thread busy"""
        try:
            conn = ldap.initialize(inst.ldapuri)
            conn.simple_bind_s(DN_DM, PW_DM)
            for _ in range(SEARCHES_PER_THREAD):
                conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                              '(objectclass=*)')
            conn.unbind_s()
        except Exception as e:
            errors.append(str(e))

    def monitor_sampler(stop_event):
        """Sample currentbusyworkers while load is running"""
        while not stop_event.is_set():
            try:
                (_, _, currentbusyworkers, _) = monitor.get_work_queue()
                val = int(currentbusyworkers[0])
                with peak_lock:
                    if val > peak_busy[0]:
                        peak_busy[0] = val
            except Exception:
                pass

    # Record baseline
    (_, _, _, maxbusyworkers_before) = monitor.get_work_queue()
    max_bw_before = int(maxbusyworkers_before[0])
    log.info(f"Before concurrent load: maxbusyworkers={max_bw_before}")

    # Start the monitor sampler
    stop_event = threading.Event()
    sampler = threading.Thread(target=monitor_sampler, args=(stop_event,))
    sampler.start()

    # Launch concurrent search threads
    threads = []
    for _ in range(NUM_THREADS):
        t = threading.Thread(target=search_worker)
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    # Stop the sampler
    stop_event.set()
    sampler.join()

    assert not errors, f"Search threads reported errors: {errors}"

    # Check that we observed concurrent busy workers
    log.info(f"Peak sampled currentbusyworkers during load: {peak_busy[0]}")
    assert peak_busy[0] > 0, \
        "currentbusyworkers was never > 0 during concurrent load"

    # Verify maxbusyworkers reflects the concurrent activity
    (_, _, currentbusyworkers, maxbusyworkers) = monitor.get_work_queue()
    max_bw_after = int(maxbusyworkers[0])
    cur_bw_after = int(currentbusyworkers[0])
    log.info(f"After concurrent load: maxbusyworkers={max_bw_after}, "
             f"currentbusyworkers={cur_bw_after}")

    assert max_bw_after > 0, \
        f"maxbusyworkers should be > 0 after concurrent load, got {max_bw_after}"
    assert max_bw_after >= max_bw_before, \
        f"maxbusyworkers should not decrease: before={max_bw_before}, after={max_bw_after}"
    assert cur_bw_after < NUM_THREADS, \
        f"currentbusyworkers should drop back after load completes, got {cur_bw_after}"


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
def test_monitor_memberof(topo):
    """Test MemberOf plugin deferred processing monitoring with real activity

    :id: 1c4b382a-ebaf-4d80-9232-67e1b3ab58d4
    :setup: Single instance
    :steps:
        1. Enable MemberOf plugin and verify default state
        2. Verify monitoring unavailable when deferred disabled
        3. Enable deferred processing and verify monitor creation
        4. Generate memberof activity and verify stats update
        5. Disable deferred processing and verify monitor cleanup
    :expectedresults:
        1. Plugin enabled, deferred setting is None by default
        2. ValueError raised for unavailable monitoring
        3. Monitor accessible with expected attributes
        4. TotalAdded counter increases with activity
        5. Monitoring becomes unavailable after disable
    """
    inst = topo.standalone
    memberof = MemberOfPlugin(inst)
    memberof.enable()

    # Verify plugin is enabled
    enabled = memberof.get_attr_val_utf8('nsslapd-pluginEnabled')
    assert enabled

    # Verify deferred processing is disabled by default (None = unset)
    deferred = memberof.get_memberofdeferredupdate()
    assert deferred is None

    # Verify monitoring not available when deferred disabled
    with pytest.raises(ValueError, match="not available"):
        MonitorMemberOf(inst).get_status()

    # Enable deferred processing
    memberof.set_memberofdeferredupdate('on')
    inst.restart()

    # Verify deferred processing is now enabled
    deferred = memberof.get_memberofdeferredupdate()
    assert deferred and deferred.lower() == "on"

    # Verify monitor is now accessible
    monitor = MonitorMemberOf(inst)
    stats = monitor.get_status()
    assert 'CurrentTasks' in stats
    assert 'TotalAdded' in stats
    assert 'TotalRemoved' in stats
    assert 'ThreadStatus' in stats

    # Get baseline stats before generating activity
    added_before = int(stats.get('TotalAdded', ['0'])[0])

    # Generate memberof activity to trigger deferred processing
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    groups = Groups(inst, DEFAULT_SUFFIX)
    user1 = users.create_test_user(uid=1000)
    user2 = users.create_test_user(uid=1001)

    # Creating group with members triggers deferred memberof processing
    group = groups.create(properties={
        'cn': 'testgroup',
        'member': [user1.dn, user2.dn]
    })

    # Wait for deferred processing to complete
    time.sleep(2)

    # Verify stats were updated by the activity
    updated_stats = monitor.get_status()
    added_after = int(updated_stats.get('TotalAdded', ['0'])[0])

    # Stats should definitely increase with activity
    assert added_after > added_before, f"Expected TotalAdded to increase from {added_before}, got {added_after}"

    # Clean up test data
    group.delete()
    user1.delete()
    user2.delete()

    # Disable deferred processing
    memberof.set_memberofdeferredupdate('off')
    inst.restart()

    # Verify deferred processing is now disabled
    deferred = memberof.get_memberofdeferredupdate()
    assert deferred and deferred.lower() == "off"

    # Verify monitoring is no longer available
    with pytest.raises(ValueError, match="not available"):
        monitor.get_status()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
