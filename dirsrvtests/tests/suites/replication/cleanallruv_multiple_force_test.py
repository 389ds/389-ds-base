# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import logging
import os
import pytest
import random
import time
import threading
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_m4
from lib389.tasks import CleanAllRUVTask
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.user import UserAccounts
from lib389.replica import ReplicationManager, Replicas

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
            # One of the suppliers was probably put into read only mode - just break out
            except ldap.UNWILLING_TO_PERFORM:
                break
            except ldap.ALREADY_EXISTS:
                pass
        conn.close()

def remove_some_supplier4_agmts(msg, topology_m4):
    """Remove all the repl agmts to supplier4 except from supplier3.  Used by
    the force tests."""

    log.info('%s: remove the agreements to supplier 4...' % msg)
    repl = ReplicationManager(DEFAULT_SUFFIX)
    # This will delete m4 from the topo *and* remove all incoming agreements
    # to m4.
    repl.remove_supplier(topology_m4.ms["supplier4"],
        [topology_m4.ms["supplier1"], topology_m4.ms["supplier2"]])

def task_done(topology_m4, task_dn, timeout=60):
    """Check if the task is complete"""

    attrlist = ['nsTaskLog', 'nsTaskStatus', 'nsTaskExitCode',
                'nsTaskCurrentItem', 'nsTaskTotalItems']
    done = False
    count = 0

    while not done and count < timeout:
        try:
            entry = topology_m4.ms["supplier1"].getEntry(task_dn, attrlist=attrlist)
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

def check_ruvs(msg, topology_m4, m4rid):
    """Check suppliers 1-3 for supplier 4's rid."""
    for inst in (topology_m4.ms["supplier1"], topology_m4.ms["supplier2"], topology_m4.ms["supplier3"]):
        clean = False
        replicas = Replicas(inst)
        replica = replicas.get(DEFAULT_SUFFIX)
        log.info('check_ruvs for replica %s:%s (suffix:rid)' % (replica.get_suffix(), replica.get_rid()))

        count = 0
        while not clean and count < 20:
            ruv = replica.get_ruv()
            if m4rid in ruv._rids:
                time.sleep(5)
                count = count + 1
            else:
                clean = True
        if not clean:
            raise Exception("Supplier %s was not cleaned in time." % inst.serverid)
    return True


def test_multiple_tasks_with_force(topology_m4):
    """Check that multiple tasks with a 'force' option work properly

    :id: eb76a93d-8d1c-405e-9f25-6e8d5a781098
    :setup: Replication setup with four suppliers
    :steps:
        1. Stop supplier 3
        2. Add a bunch of updates to supplier 4
        3. Disable replication on supplier 4
        4. Start supplier 3
        5. Remove agreements to supplier 4 from other suppliers
        6. Run a cleanallruv task on supplier 1 with a 'force' option 'on'
        7. Run one more cleanallruv task on supplier 1 with a 'force' option 'off'
        8. Check that everything was cleaned
    :expectedresults:
        1. Supplier 3 should be stopped
        2. Operation should be successful
        3. Replication on supplier 4 should be disabled
        4. Supplier 3 should be started
        5. Agreements to supplier 4 should be removed
        6. Operation should be successful
        7. Operation should be successful
        8. Everything should be cleaned
    """

    log.info('Running test_multiple_tasks_with_force...')

    # Stop supplier 3, while we update supplier 4, so that 3 is behind the other suppliers
    topology_m4.ms["supplier3"].stop()
    repl = ReplicationManager(DEFAULT_SUFFIX)
    m4rid = repl.get_rid(topology_m4.ms["supplier4"])

    # Add a bunch of updates to supplier 4
    m4_add_users = AddUsers(topology_m4.ms["supplier4"], 10)
    m4_add_users.start()
    m4_add_users.join()

    # Disable supplier 4
    # Remove the agreements from the other suppliers that point to supplier 4
    remove_some_supplier4_agmts("test_multiple_tasks_with_force", topology_m4)

    # Start supplier 3, it should be out of sync with the other replicas...
    topology_m4.ms["supplier3"].start()

    # Remove the agreement to replica 4
    replica = Replicas(topology_m4.ms["supplier3"]).get(DEFAULT_SUFFIX)
    replica.get_agreements().get("004").delete()

    # Run the task, use "force" because supplier 3 is not in sync with the other replicas
    # in regards to the replica 4 RUV
    log.info('test_multiple_tasks_with_force: run the cleanAllRUV task with "force" on...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["supplier1"])
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
        cruv_task_fail = CleanAllRUVTask(topology_m4.ms["supplier1"])
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

    # Check the other supplier's RUV for 'replica 4'
    log.info('test_multiple_tasks_with_force: check all the suppliers have been cleaned...')
    clean = check_ruvs("test_clean_force", topology_m4, m4rid)
    assert clean
    # Check supplier 1 does not have the clean task running
    log.info('test_abort: check supplier 1 no longer has a cleanAllRUV task...')
    if not task_done(topology_m4, cruv_task.dn):
        log.fatal('test_abort: CleanAllRUV task was not aborted')
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

