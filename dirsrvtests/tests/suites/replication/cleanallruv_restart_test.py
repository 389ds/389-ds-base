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
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_m4
from lib389.tasks import CleanAllRUVTask
from lib389.replica import ReplicationManager, Replicas

log = logging.getLogger(__name__)


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
def test_clean_restart(topology_m4):
    """Check that cleanallruv task works properly after a restart

    :id: c6233bb3-092c-4919-9ac9-80dd02cc6e02
    :setup: Replication setup with four suppliers
    :steps:
        1. Disable replication on supplier 4
        2. Remove agreements to supplier 4 from other suppliers
        3. Stop supplier 3
        4. Run a cleanallruv task on supplier 1
        5. Stop supplier 1
        6. Start supplier 3
        7. Make sure that no crash happened
        8. Start supplier 1
        9. Make sure that no crash happened
        10. Check that everything was cleaned
    :expectedresults:
        1. Operation should be successful
        2. Agreements to supplier 4 should be removed
        3. Supplier 3 should be stopped
        4. Cleanallruv task should be successfully executed
        5. Supplier 1 should be stopped
        6. Supplier 3 should be started
        7. No crash should happened
        8. Supplier 1 should be started
        9. No crash should happened
        10. Everything should be cleaned
    """
    log.info('Running test_clean_restart...')

    # Disable supplier 4
    log.info('test_clean: disable supplier 4...')

    # Remove the agreements from the other suppliers that point to supplier 4
    repl = ReplicationManager(DEFAULT_SUFFIX)
    m4rid = repl.get_rid(topology_m4.ms["supplier4"])
    remove_supplier4_agmts("test_clean", topology_m4)

    # Stop supplier 3 to keep the task running, so we can stop supplier 1...
    topology_m4.ms["supplier3"].stop()

    # Run the task
    log.info('test_clean: run the cleanAllRUV task...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["supplier1"])
    cruv_task.create(properties={
        'replica-id': m4rid,
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'no',
        'replica-certify-all': 'yes'
        })

    # Sleep a bit, then stop supplier 1
    time.sleep(5)
    topology_m4.ms["supplier1"].stop()

    # Now start supplier 3 & 1, and make sure we didn't crash
    topology_m4.ms["supplier3"].start()
    if topology_m4.ms["supplier3"].detectDisorderlyShutdown():
        log.fatal('test_clean_restart: Supplier 3 previously crashed!')
        assert False

    topology_m4.ms["supplier1"].start(timeout=30)
    if topology_m4.ms["supplier1"].detectDisorderlyShutdown():
        log.fatal('test_clean_restart: Supplier 1 previously crashed!')
        assert False

    # Check the other supplier's RUV for 'replica 4'
    log.info('test_clean_restart: check all the suppliers have been cleaned...')
    clean = check_ruvs("test_clean_restart", topology_m4, m4rid)
    assert clean

    log.info('test_clean_restart PASSED, restoring supplier 4...')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

