# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
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
from lib389.tasks import DBCompactTask
from lib389.backend import DatabaseConfig
from lib389.replica import Changelog5, Replicas
from lib389.topologies import topology_m1 as topo

log = logging.getLogger(__name__)


def test_compact_db_task(topo):
    """Test compaction of database

    :id: 1b3222ef-a336-4259-be21-6a52f76e1859
    :setup: Standalone Instance
    :steps:
        1. Create task
        2. Check task was successful
        3. Check errors log to show task was run
        3. Create task just for replication
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
    inst.deleteErrorLogs(restart=False)


def test_compaction_interval_and_time(topo):
    """Test compaction interval and time for database and changelog

    :id: f361bee9-d7e7-4569-9255-d7b60dd9d92e
    :setup: Supplier Instance
    :steps:
        1. Configure compact interval and time for database and changelog
        2. Check compaction occurs as expected
    :expectedresults:
        1. Success
        2. Success
    """

    inst = topo.ms["supplier1"]

    # Calculate the compaction time (2 minutes from now)
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

    # Configure changelog compaction
    cl5 = Changelog5(inst)
    cl5.replace_many(
        ('nsslapd-changelogcompactdb-interval', '2'),
        ('nsslapd-changelogcompactdb-time', compact_time),
        ('nsslapd-changelogtrim-interval', '2')
    )
    inst.deleteErrorLogs()

    # Check compaction occurred as expected
    time.sleep(45)
    assert not inst.searchErrorsLog("compacting replication changelogs")

    time.sleep(90)
    assert inst.searchErrorsLog("compacting replication changelogs")
    inst.deleteErrorLogs(restart=False)


def test_compact_cl5_task(topo):
    """Test compaction of changelog5 database

    :id: aadfa9f7-73c0-463a-912c-0a29aa1f8167
    :setup: Standalone Instance
    :steps:
        1. Run compaction task
        2. Check errors log to show task was run
    :expectedresults:
        1. Success
        2. Success
    """
    inst = topo.ms["supplier1"]

    replicas = Replicas(inst)
    replicas.compact_changelog(log=log)

    # Check compaction occurred as expected. But instead of time.sleep(5) check 1 sec in loop
    for _ in range(5):
        time.sleep(1)
        if inst.searchErrorsLog("compacting replication changelogs"):
            break
    assert inst.searchErrorsLog("compacting replication changelogs")
    inst.deleteErrorLogs(restart=False)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

