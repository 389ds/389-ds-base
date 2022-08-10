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
from lib389.replica import ReplicationManager

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
   

@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_abort_restart(topology_m4):
    """Test the abort task can handle a restart, and then resume

    :id: b66e33d4-fe85-4e1c-b882-75da80f70ab3
    :setup: Replication setup with four suppliers
    :steps:
        1. Disable replication on supplier 4
        2. Remove agreements to supplier 4 from other suppliers
        3. Stop supplier 3
        4. Run a cleanallruv task on supplier 1
        5. Run a cleanallruv abort task on supplier 1
        6. Restart supplier 1
        7. Make sure that no crash happened
        8. Start supplier 3
        9. Check supplier 1 does not have the clean task running
        10. Check that errors log doesn't have 'Aborting abort task' message
    :expectedresults:
        1. Replication on supplier 4 should be disabled
        2. Agreements to supplier 4 should be removed
        3. Supplier 3 should be stopped
        4. Operation should be successful
        5. Operation should be successful
        6. Supplier 1 should be restarted
        7. No crash should happened
        8. Supplier 3 should be started
        9. Check supplier 1 shouldn't have the clean task running
        10. Errors log shouldn't have 'Aborting abort task' message
    """

    log.info('Running test_abort_restart...')
    # Remove the agreements from the other suppliers that point to supplier 4
    repl = ReplicationManager(DEFAULT_SUFFIX)
    m4rid = repl.get_rid(topology_m4.ms["supplier4"])
    remove_supplier4_agmts("test_abort", topology_m4)

    # Stop supplier 3
    log.info('test_abort_restart: stop supplier 3 to freeze the cleanAllRUV task...')
    topology_m4.ms["supplier3"].stop()

    # Run the task
    log.info('test_abort_restart: add the cleanAllRUV task...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["supplier1"])
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

    # Check supplier 1 does not have the clean task running
    log.info('test_abort_abort: check supplier 1 no longer has a cleanAllRUV task...')
    if not task_done(topology_m4, cruv_task.dn):
        log.fatal('test_abort_restart: CleanAllRUV task was not aborted')
        assert False

    # Now restart supplier 1, and make sure the abort process completes
    topology_m4.ms["supplier1"].restart()
    if topology_m4.ms["supplier1"].detectDisorderlyShutdown():
        log.fatal('test_abort_restart: Supplier 1 previously crashed!')
        assert False

    # Start supplier 3
    topology_m4.ms["supplier3"].start()

    # Need to wait 5 seconds before server processes any leftover tasks
    time.sleep(6)

    # Check supplier 1 tried to run abort task.  We expect the abort task to be aborted.
    if not topology_m4.ms["supplier1"].searchErrorsLog('Aborting abort task'):
        log.fatal('test_abort_restart: Abort task did not restart')
        assert False

    log.info('test_abort_restart PASSED')
    

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

