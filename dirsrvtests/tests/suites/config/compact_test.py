import logging
import pytest
import os
import time
from lib389.utils import get_default_db_lib
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

    # On bdb, check errors log to make sure task only performed changelog compaction
    # Note: as mdb contains a single map file (the justChangelog flags has
    #       no impact (and whole db is compacted))
    if get_default_db_lib() == "bdb":
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
    config = DatabaseConfig(inst)
    config.set([('nsslapd-db-compactdb-interval', '2'), ('nsslapd-db-compactdb-time', '00:01')])
    inst.deleteErrorLogs()

    time.sleep(6)
    assert inst.searchErrorsLog("Compacting databases")
    inst.deleteErrorLogs(restart=False)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

