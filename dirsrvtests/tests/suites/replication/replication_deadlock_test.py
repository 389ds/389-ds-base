# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Test replication deadlock scenarios

This test covers deadlock scenarios that can occur after importing LDIF
with replication data when replication agreements are present.
"""

import logging
import os
import pytest

from lib389.topologies import topology_m2 as topo
from lib389.replica import Replicas, ReplicationManager
from lib389.agreement import Agreements
from lib389.tasks import ImportTask, ExportTask
from lib389.idm.user import UserAccounts
from lib389.tombstone import Tombstones
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_replication_deadlock_import_invalid_agreement(topo):
    """Test deadlock scenario during LDIF import with invalid replication agreement

    This test verifies that importing LDIF data while replication agreements
    are paused doesn't cause deadlocks.

    :id: b8c47d4a-b189-4e70-9070-f6281309cf92
    :setup: Two supplier replication setup
    :steps:
        1. Create test entries on supplier1
        2. Pause replication agreement
        3. Export LDIF with replication data
        4. Import LDIF during paused replication
        5. Resume replication agreement
        6. Verify entries exist
    :expectedresults:
        1. Test entries created successfully
        2. Replication agreement paused successfully
        3. LDIF export completes successfully
        4. LDIF import completes without deadlock
        5. Replication agreement resumed successfully
        6. Entries exist after import
    """

    log.info('Testing replication deadlock scenario during LDIF import')

    export_ldif = os.path.join(topo.ms["supplier1"].get_ldif_dir(), 'replication_export.ldif')

    log.info('Adding test entries to supplier1')
    users = UserAccounts(topo.ms["supplier1"], DEFAULT_SUFFIX)

    user1 = users.create(properties={
        'uid': 'testuser1',
        'cn': 'Test User 1',
        'sn': 'User',
        'userpassword': 'password',
        'uidNumber': '1001',
        'gidNumber': '2001',
        'homeDirectory': '/home/testuser1'
    })

    user2 = users.create(properties={
        'uid': 'testuser2',
        'cn': 'Test User 2',
        'sn': 'User',
        'userpassword': 'password',
        'uidNumber': '1002',
        'gidNumber': '2002',
        'homeDirectory': '/home/testuser2'
    })

    log.info('Waiting for replication to sync')
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(topo.ms["supplier1"], topo.ms["supplier2"])

    log.info('Pausing replication agreement')
    replicas = Replicas(topo.ms["supplier1"])
    replica = replicas.get(DEFAULT_SUFFIX)
    agreements = Agreements(topo.ms["supplier1"], basedn=replica.dn)
    agmt = agreements.list()[0]
    agmt.pause()

    log.info('Creating LDIF export')
    export_task = ExportTask(topo.ms["supplier1"])
    export_task.export_suffix_to_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)
    export_task.wait()

    log.info('Importing LDIF during paused replication')
    import_task = ImportTask(topo.ms["supplier1"])
    import_task.import_suffix_from_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait()

    log.info('Resuming replication agreement')
    agmt.resume()

    log.info('Verifying entries exist after import')
    assert user1.exists()
    assert user2.exists()

    log.info('Cleaning up test entries')
    user1.delete()
    user2.delete()

    if os.path.exists(export_ldif):
        os.remove(export_ldif)

    log.info('Test completed successfully - no deadlock detected')


def test_replication_deadlock_tombstone_search(topo):
    """Test deadlock scenario during tombstone search with replication

    This test verifies that searching for tombstones while replication
    agreements are paused doesn't cause deadlocks.

    :id: c9d58e5b-c298-4f81-a181-f7392410df03
    :setup: Two supplier replication setup
    :steps:
        1. Create and delete entries to generate tombstones
        2. Pause replication agreement
        3. Export LDIF with tombstone data
        4. Import LDIF during paused replication
        5. Search for tombstone entries
        6. Resume replication agreement
    :expectedresults:
        1. Tombstone entries created successfully
        2. Replication agreement paused successfully
        3. LDIF export completes successfully
        4. LDIF import completes without deadlock
        5. Tombstone search completes without deadlock
        6. Replication agreement resumed successfully
    """


    export_ldif = os.path.join(topo.ms["supplier1"].get_ldif_dir(), 'tombstone_export.ldif')

    log.info('Creating and deleting entries to generate tombstones')
    users = UserAccounts(topo.ms["supplier1"], DEFAULT_SUFFIX)

    user = users.create(properties={
        'uid': 'testuser_tombstone',
        'cn': 'Test User Tombstone',
        'sn': 'User',
        'userpassword': 'password',
        'uidNumber': '1003',
        'gidNumber': '2003',
        'homeDirectory': '/home/testuser_tombstone'
    })

    user.delete()

    log.info('Pausing replication agreement')
    replicas = Replicas(topo.ms["supplier1"])
    replica = replicas.get(DEFAULT_SUFFIX)
    agreements = Agreements(topo.ms["supplier1"], basedn=replica.dn)
    agmt = agreements.list()[0]
    agmt.pause()

    log.info('Creating LDIF export with tombstones')
    export_task = ExportTask(topo.ms["supplier1"])
    export_task.export_suffix_to_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)
    export_task.wait()

    log.info('Importing LDIF during paused replication')
    import_task = ImportTask(topo.ms["supplier1"])
    import_task.import_suffix_from_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait()

    log.info('Searching for tombstone entries')
    try:
        tombstones = Tombstones(topo.ms["supplier1"], DEFAULT_SUFFIX)
        tombstone_entries = tombstones.list()
        log.info(f'Found {len(tombstone_entries)} tombstone entries')
        assert isinstance(tombstone_entries, list), "Search should return a list"

    except Exception as e:
        log.error(f"Search failed with error: {e}")
        pytest.fail(f"Search failed: {e}")

    log.info('Resuming replication agreement')
    agmt.resume()

    if os.path.exists(export_ldif):
        os.remove(export_ldif)


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
