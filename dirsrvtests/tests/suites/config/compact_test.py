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
from lib389.topologies import topology_m1 as topo

log = logging.getLogger(__name__)


def test_compact_db_task(topo):
    """Test creation of dbcompact task is successful

    :id: 1b3222ef-a336-4259-be21-6a52f76e1859
    :setup: Standalone Instance
    :steps:
        1. Create task
        2. Check task was successful
        3. Check errors log to show task was run
        4. Create task just for replication
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

    # Check errors log to make sure task only performed changelog compaction
    assert inst.searchErrorsLog("Compacting DB") == False
    assert inst.searchErrorsLog("Compacting Replication Changelog")
    inst.deleteErrorLogs(restart=False)


def test_compaction_interval_and_time(topo):
    """Test dbcompact is successful when nsslapd-db-compactdb-interval and nsslapd-db-compactdb-time is set

    :id: f361bee9-d7e7-4569-9255-d7b60dd9d92e
    :setup: Supplier Instance
    :steps:
        1. Configure compact interval and time
        2. Check compaction occurs as expected
    :expectedresults:
        1. Success
        2. Success
    """

    inst = topo.ms["supplier1"]

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

    # Set compaction TOD
    config = DatabaseConfig(inst)
    config.set([('nsslapd-db-compactdb-interval', '2'), ('nsslapd-db-compactdb-time', compact_time)])
    inst.deleteErrorLogs(restart=True)

    # Check compaction occurred as expected
    time.sleep(45)
    assert not inst.searchErrorsLog("Compacting databases")

    time.sleep(90)
    assert inst.searchErrorsLog("Compacting databases")
    inst.deleteErrorLogs(restart=False)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

