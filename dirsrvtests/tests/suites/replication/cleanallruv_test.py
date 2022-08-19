# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389 import DirSrv
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m4, topology_m2
from lib389._constants import DEFAULT_SUFFIX
from lib389.replica import ReplicationManager, Replicas
from lib389.tasks import CleanAllRUVTask


pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def remove_supplier4_agmts(msg, topology_m4):
    """Remove all the repl agmts to supplier4. """

    log.info('%s: remove all the agreements to supplier 4...' % msg)
    repl = ReplicationManager(DEFAULT_SUFFIX)
    # This will delete m4 from the topo *and* remove all incoming agreements
    # to m4.
    repl.remove_supplier(topology_m4.ms["supplier4"],
        [topology_m4.ms["supplier1"], topology_m4.ms["supplier2"], topology_m4.ms["supplier3"]])

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


def test_clean(topology_m4):
    """Check that cleanallruv task works properly

    :id: e9b3ce5c-e17c-409e-aafc-e97d630f2878
    :setup: Replication setup with four suppliers
    :steps:
        1. Check that replication works on all suppliers
        2. Disable replication on supplier 4
        3. Remove agreements to supplier 4 from other suppliers
        4. Run a cleanallruv task on supplier 1 with a 'force' option 'on'
        5. Check that everything was cleaned
    :expectedresults:
        1. Replication should work properly on all suppliers
        2. Operation should be successful
        3. Agreements to supplier 4 should be removed
        4. Cleanallruv task should be successfully executed
        5. Everything should be cleaned
    """

    log.info('Running test_clean...')
    # Disable supplier 4
    # Remove the agreements from the other suppliers that point to supplier 4
    log.info('test_clean: disable supplier 4...')
    repl = ReplicationManager(DEFAULT_SUFFIX)
    m4rid = repl.get_rid(topology_m4.ms["supplier4"])
    remove_supplier4_agmts("test_clean", topology_m4)

    # Run the task
    log.info('test_clean: run the cleanAllRUV task...')
    cruv_task = CleanAllRUVTask(topology_m4.ms["supplier1"])
    cruv_task.create(properties={
        'replica-id': m4rid,
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'no'
        })
    cruv_task.wait()

    # Check the other supplier's RUV for 'replica 4'
    log.info('test_clean: check all the suppliers have been cleaned...')
    clean = check_ruvs("test_clean", topology_m4, m4rid)
    assert clean

    log.info('test_clean PASSED, restoring supplier 4...')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
