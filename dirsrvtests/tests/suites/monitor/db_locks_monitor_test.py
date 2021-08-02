# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import datetime
import subprocess
from multiprocessing import Process, Queue
from lib389 import pid_from_file
from lib389.utils import ldap, os
from lib389._constants import DEFAULT_SUFFIX, ReplicaRole
from lib389.cli_base import LogCapture
from lib389.idm.user import UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.tasks import AccessLog
from lib389.backend import Backends
from lib389.ldclt import Ldclt
from lib389.dbgen import dbgen_users
from lib389.tasks import ImportTask
from lib389.index import Indexes
from lib389.plugins import AttributeUniquenessPlugin
from lib389.config import BDB_LDBMConfig
from lib389.monitor import MonitorLDBM
from lib389.topologies import create_topology, _remove_ssca_db
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier2
db_locks_monitoring_ack = pytest.mark.skipif(not os.environ.get('DB_LOCKS_MONITORING_ACK', False),
                                                                reason="DB locks monitoring tests may take hours if the feature is not present or another failure exists. "
                                                                    "Also, the feature requires a big amount of space as we set nsslapd-db-locks to 1300000.")

DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _kill_ns_slapd(inst):
    pid = str(pid_from_file(inst.ds_paths.pid_file))
    cmd = ['kill', '-9', pid]
    subprocess.Popen(cmd, stdout=subprocess.PIPE)


@pytest.fixture(scope="function")
def topology_st_fn(request):
    """Create DS standalone instance for each test case"""

    topology = create_topology({ReplicaRole.STANDALONE: 1})

    def fin():
        # Kill the hanging process at the end of test to prevent failures in the following tests
        if DEBUGGING:
            [_kill_ns_slapd(inst) for inst in topology]
        else:
            [_kill_ns_slapd(inst) for inst in topology]
            assert _remove_ssca_db(topology)
            [inst.stop() for inst in topology if inst.exists()]
            [inst.delete() for inst in topology if inst.exists()]
    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="function")
def setup_attruniq_index_be_import(topology_st_fn):
    """Enable Attribute Uniqueness, disable indexes and
    import 120000 entries to the default backend
    """
    inst = topology_st_fn.standalone

    inst.config.loglevel([AccessLog.DEFAULT, AccessLog.INTERNAL], service='access')
    inst.config.set('nsslapd-plugin-logging', 'on')
    inst.restart()

    attruniq = AttributeUniquenessPlugin(inst, dn="cn=attruniq,cn=plugins,cn=config")
    attruniq.create(properties={'cn': 'attruniq'})
    for cn in ['uid', 'cn', 'sn', 'uidNumber', 'gidNumber', 'homeDirectory', 'givenName', 'description']:
        attruniq.add_unique_attribute(cn)
    attruniq.add_unique_subtree(DEFAULT_SUFFIX)
    attruniq.enable_all_subtrees()
    attruniq.enable()

    indexes = Indexes(inst)
    for cn in ['uid', 'cn', 'sn', 'uidNumber', 'gidNumber', 'homeDirectory', 'givenName', 'description']:
        indexes.ensure_state(properties={
            'cn': cn,
            'nsSystemIndex': 'false',
            'nsIndexType': 'none'})

    bdb_config = BDB_LDBMConfig(inst)
    bdb_config.replace("nsslapd-db-locks", "130000")
    inst.restart()

    ldif_dir = inst.get_ldif_dir()
    import_ldif = ldif_dir + '/perf_import.ldif'

    # Valid online import
    import_task = ImportTask(inst)
    dbgen_users(inst, 120000, import_ldif, DEFAULT_SUFFIX, entry_name="userNew")
    import_task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait()
    assert import_task.is_complete()


def create_user_wrapper(q, users):
    try:
        users.create_test_user()
    except Exception as ex:
        q.put(ex)


def spawn_worker_thread(function, users, log, timeout, info):
    log.info(f"Starting the thread - {info}")
    q = Queue()
    p = Process(target=function, args=(q,users,))
    p.start()

    log.info(f"Waiting for {timeout} seconds for the thread to finish")
    p.join(timeout)

    if p.is_alive():
        log.info("Killing the thread as it's still running")
        p.terminate()
        p.join()
        raise RuntimeError(f"Function call was aborted: {info}")
    result = q.get()
    if isinstance(result, Exception):
        raise result
    else:
        return result


@db_locks_monitoring_ack 
@pytest.mark.parametrize("lock_threshold", [("70"), ("80"), ("95")])
def test_exhaust_db_locks_basic(topology_st_fn, setup_attruniq_index_be_import, lock_threshold):
    """Test that when all of the locks are exhausted the instance still working
    and database is not corrupted

    :id: 299108cc-04d8-4ddc-b58e-99157fccd643
    :customerscenario: True
    :parametrized: yes
    :setup: Standalone instance with Attr Uniq plugin and user indexes disabled
    :steps: 1. Set nsslapd-db-locks to 11000
            2. Check that we stop acquiring new locks when the threshold is reached
            3. Check that we can regulate a pause interval for DB locks monitoring thread
            4. Make sure the feature works for different backends on the same suffix
    :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Success
    """

    inst = topology_st_fn.standalone
    ADDITIONAL_SUFFIX = 'ou=newpeople,dc=example,dc=com'

    backends = Backends(inst)
    backends.create(properties={'nsslapd-suffix': ADDITIONAL_SUFFIX,
                                'name': ADDITIONAL_SUFFIX[-3:]})
    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    ous.create(properties={'ou': 'newpeople'})

    bdb_config = BDB_LDBMConfig(inst)
    bdb_config.replace("nsslapd-db-locks", "11000")

    # Restart server
    inst.restart()

    for lock_enabled in ["on", "off"]:
        for lock_pause in ["100", "500", "1000"]:
            bdb_config.replace("nsslapd-db-locks-monitoring-enabled", lock_enabled)
            bdb_config.replace("nsslapd-db-locks-monitoring-threshold", lock_threshold)
            bdb_config.replace("nsslapd-db-locks-monitoring-pause", lock_pause)
            inst.restart()

            if lock_enabled == "off":
                raised_exception = (RuntimeError, ldap.SERVER_DOWN)
            else:
                raised_exception = ldap.OPERATIONS_ERROR

            users = UserAccounts(inst, DEFAULT_SUFFIX)
            with pytest.raises(raised_exception):
                spawn_worker_thread(create_user_wrapper, users, log, 30,
                                    f"Adding user with monitoring enabled='{lock_enabled}'; "
                                    f"threshold='{lock_threshold}'; pause='{lock_pause}'.")
            # Restart because we already run out of locks and the next unindexed searches will fail eventually
            if lock_enabled == "off":
                _kill_ns_slapd(inst)
                inst.restart()

            users = UserAccounts(inst, ADDITIONAL_SUFFIX, rdn=None)
            with pytest.raises(raised_exception):
                spawn_worker_thread(create_user_wrapper, users, log, 30,
                                    f"Adding user with monitoring enabled='{lock_enabled}'; "
                                    f"threshold='{lock_threshold}'; pause='{lock_pause}'.")
            # In case feature is disabled - restart for the clean up
            if lock_enabled == "off":
                _kill_ns_slapd(inst)
            inst.restart()


@db_locks_monitoring_ack
def test_exhaust_db_locks_big_pause(topology_st_fn, setup_attruniq_index_be_import):
    """Test that DB lock pause setting increases the wait interval value for the monitoring thread

    :id: 7d5bf838-5d4e-4ad5-8c03-5716afb84ea6
    :customerscenario: True
    :setup: Standalone instance with Attr Uniq plugin and user indexes disabled
    :steps: 1. Set nsslapd-db-locks to 20000 while using the default threshold value (95%)
            2. Set nsslapd-db-locks-monitoring-pause to 10000 (10 seconds)
            3. Make sure that the pause is successfully increased a few times in a row
    :expectedresults:
            1. Success
            2. Success
            3. Success
    """

    inst = topology_st_fn.standalone

    bdb_config = BDB_LDBMConfig(inst)
    bdb_config.replace("nsslapd-db-locks", "20000")
    lock_pause = bdb_config.get_attr_val_int("nsslapd-db-locks-monitoring-pause")
    assert lock_pause == 500
    lock_pause = "10000"
    bdb_config.replace("nsslapd-db-locks-monitoring-pause", lock_pause)

    # Restart server
    inst.restart()

    lock_enabled = bdb_config.get_attr_val_utf8_l("nsslapd-db-locks-monitoring-enabled")
    lock_threshold = bdb_config.get_attr_val_int("nsslapd-db-locks-monitoring-threshold")
    assert lock_enabled == "on"
    assert lock_threshold == 90

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    start = datetime.datetime.now()
    with pytest.raises(ldap.OPERATIONS_ERROR):
        spawn_worker_thread(create_user_wrapper, users, log, 30,
                            f"Adding user with monitoring enabled='{lock_enabled}'; "
                            f"threshold='{lock_threshold}'; pause='{lock_pause}'. Expect it to 'Work'")
    end = datetime.datetime.now()
    time_delta = end - start
    if time_delta.seconds < 9:
        raise RuntimeError("nsslapd-db-locks-monitoring-pause attribute doesn't function correctly. "
                            f"Finished the execution in {time_delta.seconds} seconds")
    # In case something has failed - restart for the clean up
    inst.restart()


@pytest.mark.ds4623
@pytest.mark.bz1812286
@pytest.mark.skipif(ds_is_older("1.4.3.23"), reason="Not implemented")
@pytest.mark.parametrize("invalid_value", [("0"), ("1"), ("42"), ("68"), ("69"), ("96"), ("120")])
def test_invalid_threshold_range(topology_st_fn, invalid_value):
    """Test that setting nsslapd-db-locks-monitoring-threshold to 60 % is rejected

    :id: e4551de1-8582-4c13-b59d-3d5ec4701457
    :customerscenario: True
    :parametrized: yes
    :setup: Standalone instance
    :steps: 1. Set nsslapd-db-locks-monitoring-threshold to 60 %
            2. Check if exception message contains info about invalid value range
    :expectedresults:
            1. Exception is raised
            2. Success
    """

    inst = topology_st_fn.standalone
    bdb_config = BDB_LDBMConfig(inst)
    msg = 'threshold is indicated as a percentage and it must lie in range of 70 and 95'

    try:
        bdb_config.replace("nsslapd-db-locks-monitoring-threshold", invalid_value)
    except ldap.OPERATIONS_ERROR as e:
        log.info('Got expected error: {}'.format(str(e)))
        assert msg in str(e)


@pytest.mark.ds4623
@pytest.mark.bz1812286
@pytest.mark.skipif(ds_is_older("1.4.3.23"), reason="Not implemented")
@pytest.mark.parametrize("locks_invalid", [("0"), ("1"), ("9999"), ("10000")])
def test_invalid_db_locks_value(topology_st_fn, locks_invalid):
    """Test that setting nsslapd-db-locks to 0 is rejected

    :id: bbb40279-d622-4f36-a129-c54f963f494a
    :customerscenario: True
    :parametrized: yes
    :setup: Standalone instance
    :steps: 1. Set nsslapd-db-locks to 0
            2. Check if exception message contains info about invalid value
    :expectedresults:
            1. Exception is raised
            2. Success
    """

    inst = topology_st_fn.standalone
    bdb_config = BDB_LDBMConfig(inst)
    msg = 'Invalid value for nsslapd-db-locks ({}). Must be greater than 10000'.format(locks_invalid)

    try:
        bdb_config.replace("nsslapd-db-locks", locks_invalid)
    except ldap.UNWILLING_TO_PERFORM as e:
        log.info('Got expected error: {}'.format(str(e)))
        assert msg in str(e)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
