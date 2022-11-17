# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
<<<<<<< HEAD

=======
#
>>>>>>> 4a5f6a8c2 (Issue 5534 - Add copyright text to the repository files)
import logging
import pytest
import os
import time
import datetime
from lib389.utils import get_default_db_lib
from lib389.tasks import DBCompactTask
from lib389.backend import DatabaseConfig
from lib389.topologies import topology_m1 as topo
from lib389.utils import ldap, ds_is_older

pytestmark = pytest.mark.tier2
log = logging.getLogger(__name__)


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

    # Calculate the compaction time (1 minute from now)
    now = datetime.datetime.now()
    current_hour = now.hour
    current_minute = now.minute + 2
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
    time.sleep(60)
    assert not inst.searchErrorsLog("Compacting databases")

    time.sleep(61)
    assert inst.searchErrorsLog("Compacting databases")
    inst.deleteErrorLogs(restart=False)


@pytest.mark.ds4778
@pytest.mark.bz1748441
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


@pytest.mark.ds4778
@pytest.mark.bz1748441
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

