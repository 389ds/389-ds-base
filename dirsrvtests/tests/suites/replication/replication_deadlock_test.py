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
import ldap

from lib389._mapped_object import DSLdapObjects
from lib389.topologies import topology_m2 as topo
from lib389.topologies import topology_st
from lib389.replica import Replicas, ReplicationManager
from lib389.agreement import Agreements
from lib389.tasks import ImportTask, ExportTask
from lib389.idm.user import UserAccounts
from lib389.tombstone import Tombstones
from lib389.backend import Backends
from lib389._constants import (DEFAULT_SUFFIX, DEFAULT_BENAME, defaultProperties,
                               REPLICATION_BIND_DN, REPLICATION_BIND_PW,
                               REPLICATION_BIND_METHOD, REPLICATION_TRANSPORT)

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


def test_replication_deadlock_tombstone_invalid_agreement(topology_st):
    """Test deadlock scenario after importing LDIF with replication data and invalid agreement

    This test verifies that a deadlock does not occur while searching for tombstones after
    setting up an invalid replication agreement (pointing to a non-existent server)

    :id: a1b2c3d4-e5f6-4781-9abc-def012345678
    :setup: Standalone instance with replication enabled
    :steps:
        1. Create a supplier with an invalid replication agreement (port 5555, non-existent server)
        2. Add two test user entries
        3. Export LDIF with replication data
        4. Restart the server
        5. Import the LDIF back
        6. Search for tombstone entries with timeout (should not hang/deadlock)
        7. Cleanup: restore original timeout settings, delete invalid agreement, test entries, and export file
    :expectedresults:
        1. Invalid replication agreement is created successfully
        2. Test entries are created successfully
        3. LDIF export completes successfully
        4. Server restarts successfully
        5. LDIF import completes successfully
        6. Tombstone search completes without deadlock (returns at least one entry; no hang/timeout)
        7. Cleanup completes successfully
    """

    standalone = topology_st.standalone
    export_ldif = os.path.join(standalone.get_ldif_dir(), 'export.ldif')

    # Step 1: Create supplier with invalid replication agreement
    log.info('Creating supplier with invalid replication agreement (port 5555 - non-existent server)')
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.create_first_supplier(standalone)

    replicas = Replicas(standalone)
    replica = replicas.get(DEFAULT_SUFFIX)
    agreements = Agreements(standalone, basedn=replica.dn)

    # Create agreement with invalid port (non-existent server)
    invalid_agreement = agreements.create(properties={
        'cn': r'meTo_$host:$port',
        'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
        'nsDS5ReplicaHost': standalone.host,
        'nsDS5ReplicaPort': '5555',  # Invalid port - server does not exist
        'nsDS5ReplicaBindDN': defaultProperties[REPLICATION_BIND_DN],
        'nsDS5ReplicaBindMethod': defaultProperties[REPLICATION_BIND_METHOD],
        'nsDS5ReplicaTransportInfo': defaultProperties[REPLICATION_TRANSPORT],
        'nsDS5ReplicaCredentials': defaultProperties[REPLICATION_BIND_PW]
    })

    # Step 2: Add two test entries
    log.info('Adding two test entries')
    users = UserAccounts(standalone, DEFAULT_SUFFIX)

    entry1 = users.create(properties={
        'uid': 'entry1',
        'cn': 'entry1',
        'sn': 'user',
        'userpassword': 'password',
        'uidNumber': '1001',
        'gidNumber': '2001',
        'homeDirectory': '/home/entry1'
    })

    entry2 = users.create(properties={
        'uid': 'entry2',
        'cn': 'entry2',
        'sn': 'user',
        'userpassword': 'password',
        'uidNumber': '1002',
        'gidNumber': '2002',
        'homeDirectory': '/home/entry2'
    })

    # Step 3: Export LDIF with replication data
    log.info('Exporting LDIF with replication data')
    backends = Backends(standalone)
    export_task = backends.export_ldif(be_names=DEFAULT_BENAME, ldif=export_ldif, replication=True)
    export_task.wait()

    # Step 4: Restart the server
    log.info('Restarting server')
    standalone.restart()

    # Step 5: Import the LDIF
    log.info('Importing replication LDIF file')
    import_task = ImportTask(standalone)
    import_task.import_suffix_from_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait()

    # Step 6: Search for tombstones with timeout - should not hang/deadlock
    log.info('Searching for tombstone entries (should find entries and not hang)')
    # Save original timeout settings so we can restore them in cleanup
    orig_network_timeout = standalone.get_option(ldap.OPT_NETWORK_TIMEOUT)
    orig_timeout = standalone.get_option(ldap.OPT_TIMEOUT)
    # Set explicit timeouts to detect deadlocks
    standalone.set_option(ldap.OPT_NETWORK_TIMEOUT, 5)
    standalone.set_option(ldap.OPT_TIMEOUT, 5)

    # Verify at least one tombstone exists and the search does not hang/timeout
    try:
        results = DSLdapObjects(standalone, DEFAULT_SUFFIX).filter('(objectclass=nsTombstone)')
        log.info(f'Found {len(results)} tombstone entries')
        assert len(results) > 0, "Tombstone search should return at least one entry"
    except Exception as e:
        log.error(f"Tombstone search failed with error: {e}")
        pytest.fail(f"Tombstone search failed: {e}")
    finally:
        # Restore original timeout settings
        standalone.set_option(ldap.OPT_NETWORK_TIMEOUT, orig_network_timeout)
        standalone.set_option(ldap.OPT_TIMEOUT, orig_timeout)
        # Cleanup test entries and export file
        log.info('Cleaning up test entries')
        invalid_agreement.delete()
        entry1.delete()
        entry2.delete()
        if os.path.exists(export_ldif):
            os.remove(export_ldif)

    log.info('Test completed successfully - no deadlock detected')

if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
