# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.replica import Replicas, Replica
from lib389.tasks import *
from lib389.utils import *
from lib389.paths import Paths
from lib389.topologies import topology_m2

from lib389._constants import (DEFAULT_SUFFIX, DN_CONFIG)
from lib389.properties import (REPLICA_PURGE_DELAY, REPLICA_PURGE_INTERVAL)

from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

ds_paths = Paths()

@pytest.fixture(scope="module")
def topology_setup(topology_m2):
    """Configure the topology with purge parameters and enable audit logging

        - configure replica purge delay and interval on master1 and master2
        - enable audit log on master1 and master2
        - restart master1 and master2
    """
    m1 = topology_m2.ms["master1"]
    m2 = topology_m2.ms["master2"]

    replica1 = Replicas(m1).get(DEFAULT_SUFFIX)
    replica2 = Replicas(m2).get(DEFAULT_SUFFIX)

    replica1.set('nsDS5ReplicaPurgeDelay','5')
    replica2.set('nsDS5ReplicaPurgeDelay','5')
    assert replica1.present('nsDS5ReplicaPurgeDelay')
    assert replica2.present('nsDS5ReplicaPurgeDelay')
    replica1.display_attr('nsDS5ReplicaPurgeDelay')
    replica2.display_attr('nsDS5ReplicaPurgeDelay')

    replica1.set('nsDS5ReplicaTombstonePurgeInterval', '5')
    replica2.set('nsDS5ReplicaTombstonePurgeInterval', '5')
    assert replica1.present('nsDS5ReplicaTombstonePurgeInterval')
    assert replica2.present('nsDS5ReplicaTombstonePurgeInterval')
    replica1.display_attr('nsDS5ReplicaTombstonePurgeInterval')
    replica2.display_attr('nsDS5ReplicaTombstonePurgeInterval')


    m1.config.set('nsslapd-auditlog-logging-enabled', 'on')
    m2.config.set('nsslapd-auditlog-logging-enabled', 'on')
    m1.restart()
    m2.restart()


@pytest.mark.skipif(not ds_paths.asan_enabled, reason="Don't run if ASAN is not enabled")
@pytest.mark.ds48226
@pytest.mark.bz1243970
@pytest.mark.bz1262363
def test_MMR_double_free(topology_m2, topology_setup, timeout=5):
    """Reproduce conditions where a double free occurs and check it does not make
    the server crash

    :id: 91580b1c-ad10-49bc-8aed-402edac59f46 
    :setup: replicated topology - purge delay and purge interval are configured
    :steps:
        1. create an entry on master1
        2. modify the entry with description add
        3. check the entry is correctly replicated on master2
        4. stop master2
        5. delete the entry's description on master1
        6. stop master1
        7. start master2
        8. delete the entry's description on master2
        9. add an entry's description on master2
        10. wait the purge delay duration
        11. add again an entry's description on master2
    :expectedresults:
        1. entry exists on master1
        2. modification is effective 
        3. entry exists on master2 and modification is effective
        4. master2 is stopped
        5. description is removed from entry on master1
        6. master1 is stopped
        7. master2 is started - not synchronized with master1
        8. description is removed from entry on master2 (same op should be performed too by replication mecanism)
        9. description to entry is added on master2
        10. Purge delay has expired - changes are erased 
        11.  description to entry is added again on master2
    """
    name = 'test_entry'

    entry_m1 = UserAccounts(topology_m2.ms["master1"], DEFAULT_SUFFIX)
    entry = entry_m1.create(properties={
        'uid': name,
        'sn': name,
        'cn': name,
        'uidNumber': '1001',
        'gidNumber': '1001',
        'homeDirectory': '/home/test_entry',
        'userPassword': 'test_entry_pwd'
    })

    log.info('First do an update that is replicated')
    entry.add('description', '5')

    log.info('Check the update in the replicated entry')
    entry_m2 = UserAccounts(topology_m2.ms["master2"], DEFAULT_SUFFIX)

    success = 0
    for i in range(0, timeout):
        try:
            entry_repl = entry_m2.get(name)
            out = entry_repl.display_attr('description')
            if len(out) > 0:
                success = 1
                break
        except:
            time.sleep(1)

    assert success

    log.info('Stop M2 so that it will not receive the next update')
    topology_m2.ms["master2"].stop(10)

    log.info('Perform a del operation that is not replicated')
    entry.remove('description', '5')

    log.info("Stop M1 so that it will keep del '5' that is unknown from master2")
    topology_m2.ms["master1"].stop(10)

    log.info('start M2 to do the next updates')
    topology_m2.ms["master2"].start()

    log.info("del 'description' by '5'")
    entry_repl.remove('description', '5')

    log.info("add 'description' by '5'")
    entry_repl.add('description', '5')

    log.info('sleep of purge delay so that the next update will purge the CSN_7')
    time.sleep(6)

    log.info("add 'description' by '6' that purge the state info")
    entry_repl.add('description', '6')
     
    log.info('Restart master1')
    topology_m2.ms["master1"].start(30)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
