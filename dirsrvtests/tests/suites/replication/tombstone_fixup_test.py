# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m1
from lib389.tombstone import Tombstones
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.replica import ReplicationManager
from lib389._constants import (defaultProperties, DEFAULT_SUFFIX, ReplicaRole,
                               REPLICAID_SUPPLIER_1, REPLICA_PRECISE_PURGING, REPLICA_PURGE_DELAY,
                               REPLICA_PURGE_INTERVAL)

pytestmark = pytest.mark.tier2


def test_precise_tombstone_purging(topology_m1):
    """ Test precise tombstone purging

    :id: adb86f50-ae76-4ed6-82b4-3cdc30ccab79
    :setup: supplier1 instance
    :steps:
        1. Create and Delete entry to create a tombstone
        2. export ldif, edit, and import ldif
        3. Check tombstones do not contain nsTombstoneCSN
        4. Run fixup task, and verify tombstones now have nsTombstone CSN
        5. Configure tombstone purging
        6. Verify tombstones are purged
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    
    m1 = topology_m1.ms['supplier1']
    m1_tasks = Tasks(m1)

    # Create tombstone entry
    users = UserAccounts(m1, DEFAULT_SUFFIX)
    user = users.create_test_user(uid=1001)
    user.delete()

    # Verify tombstone was created
    tombstones = Tombstones(m1, DEFAULT_SUFFIX)
    assert len(tombstones.list()) == 1

    # Export db, strip nsTombstoneCSN, and import it
    ldif_file = "{}/export.ldif".format(m1.get_ldif_dir())
    args = {EXPORT_REPL_INFO: True,
            TASK_WAIT: True}
    m1_tasks.exportLDIF(DEFAULT_SUFFIX, None, ldif_file, args)
    m1.restart()  # harden test case

    # Strip LDIF of nsTombstoneCSN, get the LDIF lines, then create new ldif
    ldif = open(ldif_file, "r")
    lines = ldif.readlines()
    ldif.close()
    time.sleep(.5)

    ldif = open(ldif_file, "w")
    for line in lines:
        if not line.lower().startswith('nstombstonecsn'):
            ldif.write(line)
    ldif.close()
    time.sleep(.5)

    # import the new ldif file
    log.info('Import replication LDIF file...')
    args = {TASK_WAIT: True}
    m1_tasks.importLDIF(DEFAULT_SUFFIX, None, ldif_file, args)
    time.sleep(.5)

    # Search for the tombstone again
    tombstones = Tombstones(m1, DEFAULT_SUFFIX)
    assert len(tombstones.list()) == 1

    #
    # Part 3 - test fixup task using the strip option.
    #
    args = {TASK_WAIT: True,
            TASK_TOMB_STRIP: True}
    m1_tasks.fixupTombstones(DEFAULT_BENAME, args)
    time.sleep(.5)

    # Search for tombstones with nsTombstoneCSN - better not find any
    for ts in tombstones.list():
        assert not ts.present("nsTombstoneCSN")
    
    # Now run the fixup task
    args = {TASK_WAIT: True}
    m1_tasks.fixupTombstones(DEFAULT_BENAME, args)
    time.sleep(.5)

    # Search for tombstones with nsTombstoneCSN - better find some
    tombstones = Tombstones(m1, DEFAULT_SUFFIX)
    assert len(tombstones.list()) == 1

    #
    # Part 4 - Test tombstone purging
    #
    args = {REPLICA_PRECISE_PURGING: b'on',
            REPLICA_PURGE_DELAY: b'5',
            REPLICA_PURGE_INTERVAL: b'5'}
    m1.replica.setProperties(DEFAULT_SUFFIX, None, None, args)

    # Wait for the interval to pass
    log.info('Wait for tombstone purge interval to pass...')
    time.sleep(6)

    # Add an entry to trigger replication
    users.create_test_user(uid=1002)

    # Wait for the interval to pass again
    log.info('Wait for tombstone purge interval to pass again...')
    time.sleep(6)

    # search for tombstones, there should be none
    tombstones = Tombstones(m1, DEFAULT_SUFFIX)
    assert len(tombstones.list()) == 0

