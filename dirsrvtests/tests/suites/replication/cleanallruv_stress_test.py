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
import pytest
import os
import random
import time
import threading
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_m4
from lib389.tasks import CleanAllRUVTask
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.user import UserAccounts
from lib389.replica import ReplicationManager, Replicas
from lib389.config import LDBMConfig

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

def remove_supplier4_agmts(msg, topology_m4):
    """Remove all the repl agmts to supplier4. """

    log.info('%s: remove all the agreements to supplier 4...' % msg)
    repl = ReplicationManager(DEFAULT_SUFFIX)
    # This will delete m4 from the topo *and* remove all incoming agreements
    # to m4.
    repl.remove_supplier(topology_m4.ms["supplier4"],
        [topology_m4.ms["supplier1"], topology_m4.ms["supplier2"], topology_m4.ms["supplier3"]])

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


@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_stress_clean(topology_m4):
    """Put each server(m1 - m4) under a stress, and perform the entire clean process

    :id: a8263cd6-f068-4357-86e0-e7c34504c8c5
    :setup: Replication setup with four suppliers
    :steps:
        1. Add a bunch of updates to all suppliers
        2. Put supplier 4 to read-only mode
        3. Disable replication on supplier 4
        4. Remove agreements to supplier 4 from other suppliers
        5. Run a cleanallruv task on supplier 1
        6. Check that everything was cleaned
    :expectedresults:
        1. Operation should be successful
        2. Supplier 4 should be put to read-only mode
        3. Replication on supplier 4 should be disabled
        4. Agreements to supplier 4 should be removed
        5. Operation should be successful
        6. Everything should be cleaned
    """

    log.info('Running test_stress_clean...')
    log.info('test_stress_clean: put all the suppliers under load...')

    ldbm_config = LDBMConfig(topology_m4.ms["supplier4"])

    # Put all the suppliers under load
    # not too high load else it takes a long time to converge and
    # the test result becomes instable
    m1_add_users = AddUsers(topology_m4.ms["supplier1"], 200)
    m1_add_users.start()
    m2_add_users = AddUsers(topology_m4.ms["supplier2"], 200)
    m2_add_users.start()
    m3_add_users = AddUsers(topology_m4.ms["supplier3"], 200)
    m3_add_users.start()
    m4_add_users = AddUsers(topology_m4.ms["supplier4"], 200)
    m4_add_users.start()

    # Allow sometime to get replication flowing in all directions
    log.info('test_stress_clean: allow some time for replication to get flowing...')
    time.sleep(5)

    # Put supplier 4 into read only mode
    ldbm_config.set('nsslapd-readonly', 'on')
    # We need to wait for supplier 4 to push its changes out
    log.info('test_stress_clean: allow some time for supplier 4 to push changes out (60 seconds)...')
    time.sleep(60)

    # Remove the agreements from the other suppliers that point to supplier 4
    repl = ReplicationManager(DEFAULT_SUFFIX)
    m4rid = repl.get_rid(topology_m4.ms["supplier4"])
    remove_supplier4_agmts("test_stress_clean", topology_m4)

    # Run the task
    cruv_task = CleanAllRUVTask(topology_m4.ms["supplier1"])
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

    # Check the other supplier's RUV for 'replica 4'
    log.info('test_stress_clean: check if all the replicas have been cleaned...')
    clean = check_ruvs("test_stress_clean", topology_m4, m4rid)
    assert clean

    log.info('test_stress_clean:  PASSED, restoring supplier 4...')

    # Sleep for a bit to replication complete
    log.info("Sleep for 120 seconds to allow replication to complete...")
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication_topology([
        topology_m4.ms["supplier1"],
        topology_m4.ms["supplier2"],
        topology_m4.ms["supplier3"],
        ], timeout=120)

    # Turn off readonly mode
    ldbm_config.set('nsslapd-readonly', 'off')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

