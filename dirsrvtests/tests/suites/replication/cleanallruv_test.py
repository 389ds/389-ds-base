# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import threading
import pytest
import random
from lib389 import DirSrv
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m4, topology_m2
from lib389._constants import *

from lib389.idm.directorymanager import DirectoryManager
from lib389.replica import ReplicationManager, Replicas
from lib389.tasks import CleanAllRUVTask
from lib389.idm.user import UserAccounts
from lib389.config import LDBMConfig
from lib389.config import CertmapLegacy
from lib389.idm.services import ServiceAccounts

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


class AddUsers(threading.Thread):
    def __init__(self, inst, num_users):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.num_users = num_users

    def run(self):
        """Start adding users"""

        dm = DirectoryManager(self.inst)
        conn = dm.bind()

        users = UserAccounts(conn, DEFAULT_SUFFIX)

        u_range = list(range(self.num_users))
        random.shuffle(u_range)

        for idx in u_range:
            try:
                users.create(properties={
                    'uid': 'testuser%s' % idx,
                    'cn' : 'testuser%s' % idx,
                    'sn' : 'user%s' % idx,
                    'uidNumber' : '%s' % (1000 + idx),
                    'gidNumber' : '%s' % (1000 + idx),
                    'homeDirectory' : '/home/testuser%s' % idx
                })
            # One of the masters was probably put into read only mode - just break out
            except ldap.UNWILLING_TO_PERFORM:
                break
            except ldap.ALREADY_EXISTS:
                pass
        conn.close()


def remove_master4_agmts(msg, topology_m4):
    """Remove all the repl agmts to master4. """

    log.info('%s: remove all the agreements to master 4...' % msg)
    repl = ReplicationManager(DEFAULT_SUFFIX)
    # This will delete m4 frm the topo *and* remove all incoming agreements
    # to m4.
    repl.remove_master(topology_m4.ms["master4"],
        [topology_m4.ms["master1"], topology_m4.ms["master2"], topology_m4.ms["master3"]])


def check_ruvs(msg, topology_m4, m4rid):
    """Check masters 1- 3 for master 4's rid."""
    for inst in (topology_m4.ms["master1"], topology_m4.ms["master2"], topology_m4.ms["master3"]):
        clean = False
        replicas = Replicas(inst)
        replica = replicas.get(DEFAULT_SUFFIX)

        count = 0
        while not clean and count < 20:
            ruv = replica.get_ruv()
            if m4rid in ruv._rids:
                time.sleep(5)
                count = count + 1
            else:
                clean = True
        if not clean:
            raise Exception("Master %s was not cleaned in time." % inst.serverid)
    return True


def task_done(topology_m4, task_dn, timeout=60):
    """Check if the task is complete"""

    attrlist = ['nsTaskLog', 'nsTaskStatus', 'nsTaskExitCode',
                'nsTaskCurrentItem', 'nsTaskTotalItems']
    done = False
    count = 0

    while not done and count < timeout:
        try:
            entry = topology_m4.ms["master1"].getEntry(task_dn, attrlist=attrlist)
            if entry is not None:
                if entry.hasAttr('nsTaskExitCode'):
                    done = True
                    break
            else:
                done = True
                break
        except ldap.NO_SUCH_OBJECT:
            done = True
            break
        except ldap.LDAPError:
            break
        time.sleep(1)
        count += 1

    return done


def restore_master4(topology_m4):
    """In our tests will always be removing master 4, so we need a common
    way to restore it for another test
    """

    # Restart the remaining masters to allow rid 4 to be reused.
    for inst in topology_m4.ms.values():
        inst.restart()

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.join_master(topology_m4.ms["master1"], topology_m4.ms["master4"])

    # Add the 2,3 -> 4 agmt.
    repl.ensure_agreement(topology_m4.ms["master2"], topology_m4.ms["master4"])
    repl.ensure_agreement(topology_m4.ms["master3"], topology_m4.ms["master4"])
    # And in reverse ...
    repl.ensure_agreement(topology_m4.ms["master4"], topology_m4.ms["master2"])
    repl.ensure_agreement(topology_m4.ms["master4"], topology_m4.ms["master3"])

    log.info('Master 4 has been successfully restored.')


@pytest.fixture()
def m4rid(request, topology_m4):
    log.debug("Wait a bit before the reset - it is required for the slow machines")
    time.sleep(5)
    log.debug("-------------- BEGIN RESET of m4 -----------------")
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication_topology(topology_m4.ms.values())
    # What is master4's rid?
    m4rid = repl.get_rid(topology_m4.ms["master4"])

    def fin():
        try:
            # Restart the masters and rerun cleanallruv
            for inst in topology_m4.ms.values():
                inst.restart()

            cruv_task = CleanAllRUVTask(topology_m4.ms["master1"])
            cruv_task.create(properties={
                'replica-id': m4rid,
                'replica-base-dn': DEFAULT_SUFFIX,
                'replica-force-cleaning': 'no',
                })
            cruv_task.wait()
        except ldap.UNWILLING_TO_PERFORM:
            # In some casse we already cleaned rid4, so if we fail, it's okay
            pass
        restore_master4(topology_m4)
        # Make sure everything works.
        repl.test_replication_topology(topology_m4.ms.values())
    request.addfinalizer(fin)
    log.debug("-------------- FINISH RESET of m4 -----------------")
    return m4rid


def test_clean(topology_m4, m4rid):
    """Check that cleanallruv task works properly

    :id: e9b3ce5c-e17c-409e-aafc-e97d630f2878
    :setup: Replication setup with four masters
    :steps:
        1. Check that replication works on all masters
        2. Disable replication on master 4
        3. Remove agreements to master 4 from other masters
        4. Run a cleanallruv task on master 1 with a 'force' option 'on'
        5. Check that everything was cleaned
    :expectedresults:
        1. Replication should work properly on all masters
        2. Operation should be successful
        3. Agreements to master 4 should be removed
        4. Cleanallruv task should be successfully executed
        5. Everything should be cleaned
    """

    log.info('Running test_clean...')
    # Disable master 4
    # Remove the agreements from the other masters that point to master 4
    log.info('test_clean: disable master 4...')
    remove_master4_agmts("test_clean", topology_m4)

    # Run the task
    log.info('test_clean: run the cleanAllRUV task...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["master1"])
    cruv_task.create(properties={
        'replica-id': m4rid,
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'no'
        })
    cruv_task.wait()

    # Check the other master's RUV for 'replica 4'
    log.info('test_clean: check all the masters have been cleaned...')
    clean = check_ruvs("test_clean", topology_m4, m4rid)
    assert clean

    log.info('test_clean PASSED, restoring master 4...')


def test_clean_restart(topology_m4, m4rid):
    """Check that cleanallruv task works properly after a restart

    :id: c6233bb3-092c-4919-9ac9-80dd02cc6e02
    :setup: Replication setup with four masters
    :steps:
        1. Disable replication on master 4
        2. Remove agreements to master 4 from other masters
        3. Stop master 3
        4. Run a cleanallruv task on master 1
        5. Stop master 1
        6. Start master 3
        7. Make sure that no crash happened
        8. Start master 1
        9. Make sure that no crash happened
        10. Check that everything was cleaned
    :expectedresults:
        1. Operation should be successful
        2. Agreements to master 4 should be removed
        3. Master 3 should be stopped
        4. Cleanallruv task should be successfully executed
        5. Master 1 should be stopped
        6. Master 3 should be started
        7. No crash should happened
        8. Master 1 should be started
        9. No crash should happened
        10. Everything should be cleaned
    """
    log.info('Running test_clean_restart...')

    # Disable master 4
    log.info('test_clean: disable master 4...')
    # Remove the agreements from the other masters that point to master 4
    remove_master4_agmts("test_clean", topology_m4)

    # Stop master 3 to keep the task running, so we can stop master 1...
    topology_m4.ms["master3"].stop()

    # Run the task
    log.info('test_clean: run the cleanAllRUV task...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["master1"])
    cruv_task.create(properties={
        'replica-id': m4rid,
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'no',
        'replica-certify-all': 'yes'
        })

    # Sleep a bit, then stop master 1
    time.sleep(5)
    topology_m4.ms["master1"].stop()

    # Now start master 3 & 1, and make sure we didn't crash
    topology_m4.ms["master3"].start()
    if topology_m4.ms["master3"].detectDisorderlyShutdown():
        log.fatal('test_clean_restart: Master 3 previously crashed!')
        assert False

    topology_m4.ms["master1"].start(timeout=30)
    if topology_m4.ms["master1"].detectDisorderlyShutdown():
        log.fatal('test_clean_restart: Master 1 previously crashed!')
        assert False

    # Check the other master's RUV for 'replica 4'
    log.info('test_clean_restart: check all the masters have been cleaned...')
    clean = check_ruvs("test_clean_restart", topology_m4, m4rid)
    assert clean

    log.info('test_clean_restart PASSED, restoring master 4...')


def test_clean_force(topology_m4, m4rid):
    """Check that multiple tasks with a 'force' option work properly

    :id: f8810dfe-d2d2-4dd9-ba03-5fc14896fabe
    :setup: Replication setup with four masters
    :steps:
        1. Stop master 3
        2. Add a bunch of updates to master 4
        3. Disable replication on master 4
        4. Start master 3
        5. Remove agreements to master 4 from other masters
        6. Run a cleanallruv task on master 1 with a 'force' option 'on'
        7. Check that everything was cleaned
    :expectedresults:
        1. Master 3 should be stopped
        2. Operation should be successful
        3. Replication on master 4 should be disabled
        4. Master 3 should be started
        5. Agreements to master 4 should be removed
        6. Operation should be successful
        7. Everything should be cleaned
    """

    log.info('Running test_clean_force...')

    # Stop master 3, while we update master 4, so that 3 is behind the other masters
    topology_m4.ms["master3"].stop()

    # Add a bunch of updates to master 4
    m4_add_users = AddUsers(topology_m4.ms["master4"], 1500)
    m4_add_users.start()
    m4_add_users.join()

    # Start master 3, it should be out of sync with the other replicas...
    topology_m4.ms["master3"].start()

    # Remove the agreements from the other masters that point to master 4
    remove_master4_agmts("test_clean_force", topology_m4)

    # Run the task, use "force" because master 3 is not in sync with the other replicas
    # in regards to the replica 4 RUV
    log.info('test_clean: run the cleanAllRUV task...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["master1"])
    cruv_task.create(properties={
        'replica-id': m4rid,
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'yes'
        })
    cruv_task.wait()

    # Check the other master's RUV for 'replica 4'
    log.info('test_clean_force: check all the masters have been cleaned...')
    clean = check_ruvs("test_clean_force", topology_m4, m4rid)
    assert clean

    log.info('test_clean_force PASSED, restoring master 4...')


def test_abort(topology_m4, m4rid):
    """Test the abort task basic functionality

    :id: b09a6887-8de0-4fac-8e41-73ccbaaf7a08
    :setup: Replication setup with four masters
    :steps:
        1. Disable replication on master 4
        2. Remove agreements to master 4 from other masters
        3. Stop master 2
        4. Run a cleanallruv task on master 1
        5. Run a cleanallruv abort task on master 1
    :expectedresults: No hanging tasks left
        1. Replication on master 4 should be disabled
        2. Agreements to master 4 should be removed
        3. Master 2 should be stopped
        4. Operation should be successful
        5. Operation should be successful
    """

    log.info('Running test_abort...')
    # Remove the agreements from the other masters that point to master 4
    remove_master4_agmts("test_abort", topology_m4)

    # Stop master 2
    log.info('test_abort: stop master 2 to freeze the cleanAllRUV task...')
    topology_m4.ms["master2"].stop()

    # Run the task
    log.info('test_abort: add the cleanAllRUV task...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["master1"])
    cruv_task.create(properties={
        'replica-id': m4rid,
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'no',
        'replica-certify-all': 'yes'
        })
    # Wait a bit
    time.sleep(2)

    # Abort the task
    cruv_task.abort()

    # Check master 1 does not have the clean task running
    log.info('test_abort: check master 1 no longer has a cleanAllRUV task...')
    if not task_done(topology_m4, cruv_task.dn):
        log.fatal('test_abort: CleanAllRUV task was not aborted')
        assert False

    # Start master 2
    log.info('test_abort: start master 2 to begin the restore process...')
    topology_m4.ms["master2"].start()

    log.info('test_abort PASSED, restoring master 4...')


def test_abort_restart(topology_m4, m4rid):
    """Test the abort task can handle a restart, and then resume

    :id: b66e33d4-fe85-4e1c-b882-75da80f70ab3
    :setup: Replication setup with four masters
    :steps:
        1. Disable replication on master 4
        2. Remove agreements to master 4 from other masters
        3. Stop master 3
        4. Run a cleanallruv task on master 1
        5. Run a cleanallruv abort task on master 1
        6. Restart master 1
        7. Make sure that no crash happened
        8. Start master 3
        9. Check master 1 does not have the clean task running
        10. Check that errors log doesn't have 'Aborting abort task' message
    :expectedresults:
        1. Replication on master 4 should be disabled
        2. Agreements to master 4 should be removed
        3. Master 3 should be stopped
        4. Operation should be successful
        5. Operation should be successful
        6. Master 1 should be restarted
        7. No crash should happened
        8. Master 3 should be started
        9. Check master 1 shouldn't have the clean task running
        10. Errors log shouldn't have 'Aborting abort task' message
    """

    log.info('Running test_abort_restart...')
    # Remove the agreements from the other masters that point to master 4
    remove_master4_agmts("test_abort", topology_m4)

    # Stop master 3
    log.info('test_abort_restart: stop master 3 to freeze the cleanAllRUV task...')
    topology_m4.ms["master3"].stop()

    # Run the task
    log.info('test_abort_restart: add the cleanAllRUV task...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["master1"])
    cruv_task.create(properties={
        'replica-id': m4rid,
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'no',
        'replica-certify-all': 'yes'
        })
    # Wait a bit
    time.sleep(2)

    # Abort the task
    cruv_task.abort(certify=True)

    # Check master 1 does not have the clean task running
    log.info('test_abort_abort: check master 1 no longer has a cleanAllRUV task...')
    if not task_done(topology_m4, cruv_task.dn):
        log.fatal('test_abort_restart: CleanAllRUV task was not aborted')
        assert False

    # Now restart master 1, and make sure the abort process completes
    topology_m4.ms["master1"].restart()
    if topology_m4.ms["master1"].detectDisorderlyShutdown():
        log.fatal('test_abort_restart: Master 1 previously crashed!')
        assert False

    # Start master 3
    topology_m4.ms["master3"].start()

    # Need to wait 5 seconds before server processes any leftover tasks
    time.sleep(6)

    # Check master 1 tried to run abort task.  We expect the abort task to be aborted.
    if not topology_m4.ms["master1"].searchErrorsLog('Aborting abort task'):
        log.fatal('test_abort_restart: Abort task did not restart')
        assert False

    log.info('test_abort_restart PASSED, restoring master 4...')


def test_abort_certify(topology_m4, m4rid):
    """Test the abort task with a replica-certify-all option

    :id: 78959966-d644-44a8-b98c-1fcf21b45eb0
    :setup: Replication setup with four masters
    :steps:
        1. Disable replication on master 4
        2. Remove agreements to master 4 from other masters
        3. Stop master 2
        4. Run a cleanallruv task on master 1
        5. Run a cleanallruv abort task on master 1 with a replica-certify-all option
    :expectedresults: No hanging tasks left
        1. Replication on master 4 should be disabled
        2. Agreements to master 4 should be removed
        3. Master 2 should be stopped
        4. Operation should be successful
        5. Operation should be successful
    """

    log.info('Running test_abort_certify...')

    # Remove the agreements from the other masters that point to master 4
    remove_master4_agmts("test_abort_certify", topology_m4)

    # Stop master 2
    log.info('test_abort_certify: stop master 2 to freeze the cleanAllRUV task...')
    topology_m4.ms["master2"].stop()

    # Run the task
    log.info('test_abort_certify: add the cleanAllRUV task...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["master1"])
    cruv_task.create(properties={
        'replica-id': m4rid,
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'no',
        'replica-certify-all': 'yes'
        })
    # Wait a bit
    time.sleep(2)

    # Abort the task
    log.info('test_abort_certify: abort the cleanAllRUV task...')
    abort_task = cruv_task.abort(certify=True)

    # Wait a while and make sure the abort task is still running
    log.info('test_abort_certify...')

    if task_done(topology_m4, abort_task.dn, 10):
        log.fatal('test_abort_certify: abort task incorrectly finished')
        assert False

    # Now start master 2 so it can be aborted
    log.info('test_abort_certify: start master 2 to allow the abort task to finish...')
    topology_m4.ms["master2"].start()

    # Wait for the abort task to stop
    if not task_done(topology_m4, abort_task.dn, 90):
        log.fatal('test_abort_certify: The abort CleanAllRUV task was not aborted')
        assert False

    # Check master 1 does not have the clean task running
    log.info('test_abort_certify: check master 1 no longer has a cleanAllRUV task...')
    if not task_done(topology_m4, cruv_task.dn):
        log.fatal('test_abort_certify: CleanAllRUV task was not aborted')
        assert False

    log.info('test_abort_certify PASSED, restoring master 4...')


def test_stress_clean(topology_m4, m4rid):
    """Put each server(m1 - m4) under a stress, and perform the entire clean process

    :id: a8263cd6-f068-4357-86e0-e7c34504c8c5
    :setup: Replication setup with four masters
    :steps:
        1. Add a bunch of updates to all masters
        2. Put master 4 to read-only mode
        3. Disable replication on master 4
        5. Remove agreements to master 4 from other masters
        6. Run a cleanallruv task on master 1
        7. Check that everything was cleaned
    :expectedresults:
        1. Operation should be successful
        2. Master 4 should be put to read-only mode
        3. Replication on master 4 should be disabled
        2. Agreements to master 4 should be removed
        5. Agreements to master 4 should be removed
        6. Operation should be successful
        7. Everything should be cleaned
    """

    log.info('Running test_stress_clean...')
    log.info('test_stress_clean: put all the masters under load...')

    ldbm_config = LDBMConfig(topology_m4.ms["master4"])

    # Put all the masters under load
    m1_add_users = AddUsers(topology_m4.ms["master1"], 2000)
    m1_add_users.start()
    m2_add_users = AddUsers(topology_m4.ms["master2"], 2000)
    m2_add_users.start()
    m3_add_users = AddUsers(topology_m4.ms["master3"], 2000)
    m3_add_users.start()
    m4_add_users = AddUsers(topology_m4.ms["master4"], 2000)
    m4_add_users.start()

    # Allow sometime to get replication flowing in all directions
    log.info('test_stress_clean: allow some time for replication to get flowing...')
    time.sleep(5)

    # Put master 4 into read only mode
    ldbm_config.set('nsslapd-readonly', 'on')
    # We need to wait for master 4 to push its changes out
    log.info('test_stress_clean: allow some time for master 4 to push changes out (60 seconds)...')
    time.sleep(30)

    # Remove the agreements from the other masters that point to master 4
    remove_master4_agmts("test_stress_clean", topology_m4)

    # Run the task
    cruv_task = CleanAllRUVTask(topology_m4.ms["master1"])
    cruv_task.create(properties={
        'replica-id': m4rid,
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'no'
        })
    cruv_task.wait()

    # Wait for the update to finish
    log.info('test_stress_clean: wait for all the updates to finish...')
    m1_add_users.join()
    m2_add_users.join()
    m3_add_users.join()
    m4_add_users.join()

    # Check the other master's RUV for 'replica 4'
    log.info('test_stress_clean: check if all the replicas have been cleaned...')
    clean = check_ruvs("test_stress_clean", topology_m4, m4rid)
    assert clean

    log.info('test_stress_clean:  PASSED, restoring master 4...')

    # Sleep for a bit to replication complete
    log.info("Sleep for 120 seconds to allow replication to complete...")
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication_topology([
        topology_m4.ms["master1"],
        topology_m4.ms["master2"],
        topology_m4.ms["master3"],
        ], timeout=120)

    # Turn off readonly mode
    ldbm_config.set('nsslapd-readonly', 'off')


def test_multiple_tasks_with_force(topology_m4, m4rid):
    """Check that multiple tasks with a 'force' option work properly

    :id: eb76a93d-8d1c-405e-9f25-6e8d5a781098
    :setup: Replication setup with four masters
    :steps:
        1. Stop master 3
        2. Add a bunch of updates to master 4
        3. Disable replication on master 4
        4. Start master 3
        5. Remove agreements to master 4 from other masters
        6. Run a cleanallruv task on master 1 with a 'force' option 'on'
        7. Run one more cleanallruv task on master 1 with a 'force' option 'off'
        8. Check that everything was cleaned
    :expectedresults:
        1. Master 3 should be stopped
        2. Operation should be successful
        3. Replication on master 4 should be disabled
        4. Master 3 should be started
        5. Agreements to master 4 should be removed
        6. Operation should be successful
        7. Operation should be successful
        8. Everything should be cleaned
    """

    log.info('Running test_multiple_tasks_with_force...')

    # Stop master 3, while we update master 4, so that 3 is behind the other masters
    topology_m4.ms["master3"].stop()

    # Add a bunch of updates to master 4
    m4_add_users = AddUsers(topology_m4.ms["master4"], 1500)
    m4_add_users.start()
    m4_add_users.join()

    # Start master 3, it should be out of sync with the other replicas...
    topology_m4.ms["master3"].start()

    # Disable master 4
    # Remove the agreements from the other masters that point to master 4
    remove_master4_agmts("test_multiple_tasks_with_force", topology_m4)

    # Run the task, use "force" because master 3 is not in sync with the other replicas
    # in regards to the replica 4 RUV
    log.info('test_multiple_tasks_with_force: run the cleanAllRUV task with "force" on...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["master1"])
    cruv_task.create(properties={
        'replica-id': m4rid,
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'yes',
        'replica-certify-all': 'no'
        })

    log.info('test_multiple_tasks_with_force: run the cleanAllRUV task with "force" off...')

    # NOTE: This must be try not py.test raises, because the above may or may
    # not have completed yet ....
    try:
        cruv_task_fail = CleanAllRUVTask(topology_m4.ms["master1"])
        cruv_task_fail.create(properties={
            'replica-id': m4rid,
            'replica-base-dn': DEFAULT_SUFFIX,
            'replica-force-cleaning': 'no',
            'replica-certify-all': 'no'
            })
        cruv_task_fail.wait()
    except ldap.UNWILLING_TO_PERFORM:
        pass
    # Wait for the force task ....
    cruv_task.wait()

    # Check the other master's RUV for 'replica 4'
    log.info('test_multiple_tasks_with_force: check all the masters have been cleaned...')
    clean = check_ruvs("test_clean_force", topology_m4, m4rid)
    assert clean
    # Check master 1 does not have the clean task running
    log.info('test_abort: check master 1 no longer has a cleanAllRUV task...')
    if not task_done(topology_m4, cruv_task.dn):
        log.fatal('test_abort: CleanAllRUV task was not aborted')
        assert False


@pytest.mark.bz1466441
@pytest.mark.ds50370
def test_clean_shutdown_crash(topology_m2):
    """Check that server didn't crash after shutdown when running CleanAllRUV task

    :id: c34d0b40-3c3e-4f53-8656-5e4c2a310aaf
    :setup: Replication setup with two masters
    :steps:
        1. Enable TLS on both masters
        2. Reconfigure both agreements to use TLS Client auth
        3. Stop master2
        4. Run the CleanAllRUV task
        5. Restart master1
        6. Check if master1 didn't crash
        7. Restart master1 again
        8. Check if master1 didn't crash

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """

    m1 = topology_m2.ms["master1"]
    m2 = topology_m2.ms["master2"]

    repl = ReplicationManager(DEFAULT_SUFFIX)

    cm_m1 = CertmapLegacy(m1)
    cm_m2 = CertmapLegacy(m2)

    certmaps = cm_m1.list()
    certmaps['default']['DNComps'] = None
    certmaps['default']['CmapLdapAttr'] = 'nsCertSubjectDN'

    cm_m1.set(certmaps)
    cm_m2.set(certmaps)

    log.info('Enabling TLS')
    [i.enable_tls() for i in topology_m2]

    log.info('Creating replication dns')
    services = ServiceAccounts(m1, DEFAULT_SUFFIX)
    repl_m1 = services.get('%s:%s' % (m1.host, m1.sslport))
    repl_m1.set('nsCertSubjectDN', m1.get_server_tls_subject())

    repl_m2 = services.get('%s:%s' % (m2.host, m2.sslport))
    repl_m2.set('nsCertSubjectDN', m2.get_server_tls_subject())

    log.info('Changing auth type')
    replica_m1 = Replicas(m1).get(DEFAULT_SUFFIX)
    agmt_m1 = replica_m1.get_agreements().list()[0]
    agmt_m1.replace_many(
        ('nsDS5ReplicaBindMethod', 'SSLCLIENTAUTH'),
        ('nsDS5ReplicaTransportInfo', 'SSL'),
        ('nsDS5ReplicaPort', '%s' % m2.sslport),
    )

    agmt_m1.remove_all('nsDS5ReplicaBindDN')

    replica_m2 = Replicas(m2).get(DEFAULT_SUFFIX)
    agmt_m2 = replica_m2.get_agreements().list()[0]

    agmt_m2.replace_many(
        ('nsDS5ReplicaBindMethod', 'SSLCLIENTAUTH'),
        ('nsDS5ReplicaTransportInfo', 'SSL'),
        ('nsDS5ReplicaPort', '%s' % m1.sslport),
    )
    agmt_m2.remove_all('nsDS5ReplicaBindDN')

    log.info('Stopping master2')
    m2.stop()

    log.info('Run the cleanAllRUV task')
    cruv_task = CleanAllRUVTask(m1)
    cruv_task.create(properties={
        'replica-id': repl.get_rid(m1),
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'no',
        'replica-certify-all': 'yes'
    })

    m1.restart()

    log.info('Check if master1 crashed')
    assert not m1.detectDisorderlyShutdown()

    log.info('Repeat')
    m1.restart()
    assert not m1.detectDisorderlyShutdown()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
