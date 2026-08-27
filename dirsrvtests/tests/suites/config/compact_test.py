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
import os
import time
import datetime
from lib389.utils import get_default_db_lib
from lib389.tasks import DBCompactTask
from lib389.backend import DatabaseConfig
from lib389._mapped_object import DSLdapObject
from test389.topologies import topology_m1 as topo
from lib389.utils import ldap, ds_is_older
from lib389.idm.user import UserAccounts
from lib389._constants import DEFAULT_SUFFIX


pytestmark = pytest.mark.tier2
log = logging.getLogger(__name__)

BDB_CONFIG_DN = "cn=bdb,cn=config,cn=ldbm database,cn=plugins,cn=config"

# The checkpoint thread re-evaluates compaction scheduling roughly every 2.5s
# (DBLAYER_SLEEP_INTERVAL * 10 in bdb_layer.c), independent of any configured
# checkpoint/compaction interval. Polling on that cadence instead of a single
# long time.sleep() lets these tests fail fast and finish quickly.


def _wait_for_log_line(inst, pattern, timeout=10, poll_interval=1):
    """Poll the error log for a pattern; return as soon as it appears."""
    elapsed = 0
    while elapsed < timeout:
        if inst.searchErrorsLog(pattern):
            return True
        time.sleep(poll_interval)
        elapsed += poll_interval
    return inst.searchErrorsLog(pattern)


def _assert_no_log_line_for(inst, patterns, duration, poll_interval=2):
    """Poll repeatedly for `duration` seconds, failing immediately if any
    pattern appears rather than only checking once at the end."""
    elapsed = 0
    while elapsed < duration:
        for pattern in patterns:
            assert not inst.searchErrorsLog(pattern), \
                "Unexpected log line found: %s" % pattern
        time.sleep(poll_interval)
        elapsed += poll_interval


def test_compact_db_task(topo):
    """Test creation of dbcompact task is successful

    :id: 1b3222ef-a336-4259-be21-6a52f76e1859
    :customerscenario: True
    :setup: Standalone Instance
    :steps:
        1. Create task
        2. Check task was successful
        3. Check errors log to show task was run
        4. Create task just for changelog
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    inst = topo.ms["supplier1"]

    task = DBCompactTask(inst)
    task.create()
    task.wait()
    assert task.get_exit_code() == 0

    # Check errors log to make sure task actually compacted db
    assert inst.searchErrorsLog("Compacting databases")
    inst.deleteErrorLogs()

    # Create new task that only compacts changelog
    task = DBCompactTask(inst)
    task_properties = {'justChangelog': 'yes'}
    task.create(properties=task_properties)
    task.wait()
    assert task.get_exit_code() == 0

    # On bdb, check errors log to make sure task only performed changelog compaction
    # Note: as mdb contains a single map file (the justChangelog flags has
    #       no impact (and whole db is compacted))
    if get_default_db_lib() == "bdb":
        assert inst.searchErrorsLog("Compacting DB") == False
        assert inst.searchErrorsLog("Compacting Replication Changelog")
    inst.deleteErrorLogs(restart=False)


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
def test_compaction_interval_and_time(topo):
    """Test dbcompact is successful when nsslapd-db-compactdb-interval and nsslapd-db-compactdb-time is set

    :id: f361bee9-d7e7-4569-9255-d7b60dd9d92e
    :customerscenario: True
    :setup: Supplier Instance
    :steps:
        1. Configure compact interval and time
        2. Check compaction occurs as expected
    :expectedresults:
        1. Success
        2. Success
    """

    inst = topo.ms["supplier1"]

    # Add and delete some entries so compaction has something to do
    log.info("Adding and deleting 100 users ...")
    users = UserAccounts(inst, DEFAULT_SUFFIX, rdn=None)
    for num in range(100):
        USER_NAME = f'test_{num}'
        user = users.create(properties={
            'uid': USER_NAME,
            'sn': USER_NAME,
            'cn': USER_NAME,
            'uidNumber': f'{num}',
            'gidNumber': f'{num}',
            'description': f'Description for {USER_NAME}',
            'homeDirectory': f'/home/{USER_NAME}'
        })
        user.delete()

    # Calculate the compaction time (1 minute from now)
    now = datetime.datetime.now()
    current_hour = now.hour
    current_minute = now.minute + 2

    if current_minute >= 60:
        # handle time wrapping/rollover
        current_minute = current_minute - 60
        # Bump to the next hour
        current_hour += 1

    if current_hour < 10:
        hour = "0" + str(current_hour)
    else:
        hour = str(current_hour)
    if current_minute < 10:
        minute = "0" + str(current_minute)
    else:
        minute = str(current_minute)

    compact_time = hour + ":" + minute

    # Get number of seconds to wait before compaction should happen
    wait_seconds = 120 - now.second

    # Set compaction TOD
    log.debug("compact time: %s", compact_time)
    log.debug("now: %s", str(now))
    config = DatabaseConfig(inst)
    config.set([('nsslapd-db-compactdb-interval', '45'),
                ('nsslapd-db-compactdb-time', compact_time)])
    inst.deleteErrorLogs(restart=True)

    # Check compaction occurred as expected
    time.sleep(25)
    assert not inst.searchErrorsLog("Compacting databases")

    # Make sure we can handle a restart correctly
    inst.stop()
    log.debug("sleeping for: %d", wait_seconds - 45)
    time.sleep(wait_seconds - 45)
    inst.start()
    time.sleep(17)

    now = datetime.datetime.now()
    log.debug("checking now: %s", str(now))
    assert inst.searchErrorsLog("Compacting databases")
    inst.deleteErrorLogs(restart=False)


@pytest.mark.skipif(ds_is_older("1.4.3.23"), reason="Not implemented")
def test_no_compaction(topo):
    """Test there is no compaction when nsslapd-db-compactdb-interval is set to 0

    :id: 80fdb0e3-a70c-42ad-9841-eebb74287b19
    :customerscenario: True
    :setup: Supplier Instance
    :steps:
        1. Configure nsslapd-db-compactdb-interval to 0
        2. Check there is no compaction
    :expectedresults:
        1. Success
        2. Success
    """

    inst = topo.ms["supplier1"]
    config = DatabaseConfig(inst)
    config.set([('nsslapd-db-compactdb-interval', '0'), ('nsslapd-db-compactdb-time', '00:01')])
    inst.deleteErrorLogs()

    time.sleep(3)
    assert not inst.searchErrorsLog("Compacting databases")
    inst.deleteErrorLogs(restart=False)


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
def test_no_compaction_near_scheduled_time(topo):
    """Test that nsslapd-db-compactdb-interval: 0 disables compaction even when
    nsslapd-db-compactdb-time is imminent, and that nsslapd-db-compactdb-starttime
    is not rewritten across a restart while compaction remains disabled.

    :id: 47265415-0ced-484d-9f20-b461ba6f673b
    :customerscenario: True
    :setup: Supplier Instance
    :steps:
        1. Set nsslapd-db-compactdb-interval to 0 and nsslapd-db-compactdb-time
           15 seconds in the future
        2. Poll past that time, failing immediately if compaction is
           scheduled or runs
        3. Record nsslapd-db-compactdb-starttime, restart the server, and
           confirm it is unchanged
    :expectedresults:
        1. Success
        2. No "database compaction scheduled for" or "Compacting databases" logged
        3. nsslapd-db-compactdb-starttime is identical before and after restart
    """
    inst = topo.ms["supplier1"]
    config = DatabaseConfig(inst)
    bdb_entry = DSLdapObject(inst, dn=BDB_CONFIG_DN)

    target = datetime.datetime.now() + datetime.timedelta(seconds=15)
    compact_time = target.strftime("%H:%M")

    log.info("Setting compactdb-interval=0, compactdb-time=%s (~15s from now)", compact_time)
    config.set([('nsslapd-db-compactdb-interval', '0'),
                ('nsslapd-db-compactdb-time', compact_time)])
    inst.deleteErrorLogs(restart=True)

    log.info("Polling past the configured compaction time-of-day ...")
    _assert_no_log_line_for(inst, ["database compaction scheduled for", "Compacting databases"],
                             duration=30, poll_interval=2)

    starttime_before = bdb_entry.get_attr_val_utf8('nsslapd-db-compactdb-starttime')
    log.info("nsslapd-db-compactdb-starttime before restart: %s", starttime_before)

    inst.restart()

    starttime_after = bdb_entry.get_attr_val_utf8('nsslapd-db-compactdb-starttime')
    log.info("nsslapd-db-compactdb-starttime after restart: %s", starttime_after)

    assert starttime_before == starttime_after, \
        "nsslapd-db-compactdb-starttime changed while compaction is disabled (interval=0)"
    assert not inst.searchErrorsLog("database compaction scheduled for")

    inst.deleteErrorLogs(restart=False)


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
def test_disable_compaction_after_already_scheduled(topo):
    """Test that disabling compaction (interval=0) after a compaction event was
    already queued prevents the queued event from actually running.

    :id: bbddd990-5f15-44fa-af4a-de84f2ec53f4
    :customerscenario: True
    :setup: Supplier Instance
    :steps:
        1. Set a short nonzero interval/time so compaction gets scheduled
        2. Poll for the "scheduled for" message to appear
        3. Set nsslapd-db-compactdb-interval to 0 before the scheduled time arrives
        4. Poll past the originally scheduled time, failing immediately if
           compaction actually runs
    :expectedresults:
        1. Success
        2. Scheduling message is logged
        3. Success
        4. Compaction does not actually run
    """
    inst = topo.ms["supplier1"]
    config = DatabaseConfig(inst)

    target = datetime.datetime.now() + datetime.timedelta(seconds=15)
    compact_time = target.strftime("%H:%M")

    log.info("Setting compactdb-interval=30, compactdb-time=%s (~15s from now)", compact_time)
    config.set([('nsslapd-db-compactdb-interval', '30'),
                ('nsslapd-db-compactdb-time', compact_time)])
    inst.deleteErrorLogs(restart=True)

    assert _wait_for_log_line(inst, "database compaction scheduled for", timeout=10, poll_interval=1), \
        "Compaction was never scheduled"

    log.info("Disabling compaction before the scheduled time arrives ...")
    config.set([('nsslapd-db-compactdb-interval', '0')])

    _assert_no_log_line_for(inst, ["Compacting databases"], duration=25, poll_interval=2)
    inst.deleteErrorLogs(restart=False)


@pytest.mark.skipif(ds_is_older("1.4.3.23"), reason="Not implemented")
def test_compaction_interval_invalid(topo):
    """Test that invalid value is rejected for nsslapd-db-compactdb-interval

    :id: 408ee3ee-727c-4565-8b08-2e07d0c6f7d7
    :customerscenario: True
    :setup: Supplier Instance
    :steps:
        1. Set nsslapd-db-compactdb-interval to 2147483650
        2. Check exception message contains invalid value and no compaction occurred
    :expectedresults:
        1. Exception is raised
        2. Success
    """

    inst = topo.ms["supplier1"]
    msg = 'value 2147483650 for attr nsslapd-db-compactdb-interval is greater than the maximum 2147483647'
    config = DatabaseConfig(inst)

    try:
        config.set([('nsslapd-db-compactdb-interval', '2147483650'), ('nsslapd-db-compactdb-time', '00:01')])
    except ldap.UNWILLING_TO_PERFORM as e:
        log.info('Got expected error: {}'.format(str(e)))
        assert msg in str(e)
        time.sleep(3)
        assert not inst.searchErrorsLog("Compacting databases")
        inst.deleteErrorLogs(restart=False)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

