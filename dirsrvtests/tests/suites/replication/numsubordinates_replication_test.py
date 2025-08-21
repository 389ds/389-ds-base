# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import logging
import pytest
import re
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts
from lib389.replica import ReplicationManager
from lib389.tasks import *
from lib389.tombstone import Tombstones
from lib389.topologies import topology_i2 as topo_i2, topology_m2 as topo_m2
from lib389.utils import get_default_db_lib

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def get_test_name():
    full_testname = os.getenv('PYTEST_CURRENT_TEST')
    res = re.match('.*::([^ ]+) .*', full_testname)
    assert res
    return res.group(1)


@pytest.fixture(scope="function")
def with_container(topo_m2, request):
    # Creates a organizational unit container with proper cleanup
    testname = get_test_name()
    ou = f'test_container_{testname}'
    S1 = topo_m2.ms["supplier1"]
    S2 = topo_m2.ms["supplier2"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info(f"Create container ou={ou},{DEFAULT_SUFFIX} on {S1.serverid}")
    ous1 = OrganizationalUnits(S1, DEFAULT_SUFFIX)
    container = ous1.create(properties={
         'ou': ou,
         'description': f'Test container for {testname} test'
    })

    def fin():
        container.delete(recursive=True)
        repl.wait_for_replication(S1, S2)
    
    if not DEBUGGING:
        request.addfinalizer(fin)
    repl.wait_for_replication(S1, S2)

    return container


def verify_value_against_entries(container, attr, entries, msg):
    # Check that container attr value match the number of entries
    num = container.get_attr_val_int(attr)
    num_entries = len(entries)
    dns = [ e.dn for e in entries ]
    log.debug(f"[{msg}] {attr}: entries: {entries}")
    log.info(f"[{msg}] container is {container}")
    log.info(f"[{msg}] {attr}: {num} (Expecting: {num_entries})")
    assert num == num_entries, (
        f"{attr} attribute has wrong value: {num} {msg}, was expecting: {num_entries}",
        f"entries are {dns}" )


def verify_subordinates(inst, container, msg):
    log.info(f"Verify numSubordinates and tombstoneNumSubordinates {msg}")
    tombstones = Tombstones(inst, container.dn).list()
    entries = container.search(scope='one')
    verify_value_against_entries(container, 'numSubordinates', entries, msg)
    verify_value_against_entries(container, 'tombstoneNumSubordinates', tombstones, msg)


def test_numsubordinates_tombstone_replication_mismatch(topo_i2):
    """Test that numSubordinates values match between replicas after tombstone creation

    :id: c43ecc7a-d706-42e8-9179-1ff7d0e7163a
    :setup: Two standalone instances
    :steps:
        1. Create a container (organizational unit) on the first instance
        2. Create a user object in that container
        3. Delete the user object (this creates a tombstone)
        4. Set up replication between the two instances
        5. Wait for replication to complete
        6. Check numSubordinates on both instances
        7. Check tombstoneNumSubordinates on both instances
        8. Verify that numSubordinates values match on both instances
    :expectedresults:
        1. Container should be created successfully
        2. User object should be created successfully
        3. User object should be deleted successfully
        4. Replication should be set up successfully
        5. Replication should complete successfully
        6. numSubordinates should be accessible on both instances
        7. tombstoneNumSubordinates should be accessible on both instances
        8. numSubordinates values should match on both instances
    """

    instance1 = topo_i2.ins["standalone1"]
    instance2 = topo_i2.ins["standalone2"]

    log.info("Create a container (organizational unit) on the first instance")
    ous1 = OrganizationalUnits(instance1, DEFAULT_SUFFIX)
    container = ous1.create(properties={
        'ou': 'test_container',
        'description': 'Test container for numSubordinates replication test'
    })
    container_rdn = container.rdn
    log.info(f"Created container: {container_rdn}")

    log.info("Create a user object in that container")
    users1 = UserAccounts(instance1, DEFAULT_SUFFIX, rdn=f"ou={container_rdn}")
    test_user = users1.create_test_user(uid=1001)
    log.info(f"Created user: {test_user.dn}")

    log.info("Checking initial numSubordinates on container")
    container_obj1 = OrganizationalUnits(instance1, DEFAULT_SUFFIX).get(container_rdn)
    initial_numsubordinates = container_obj1.get_attr_val_int('numSubordinates')
    log.info(f"Initial numSubordinates: {initial_numsubordinates}")
    assert initial_numsubordinates == 1

    log.info("Delete the user object (this creates a tombstone)")
    test_user.delete()

    log.info("Checking numSubordinates after deletion")
    after_delete_numsubordinates = container_obj1.get_attr_val_int('numSubordinates')
    log.info(f"numSubordinates after deletion: {after_delete_numsubordinates}")

    log.info("Checking tombstoneNumSubordinates after deletion")
    try:
        tombstone_numsubordinates = container_obj1.get_attr_val_int('tombstoneNumSubordinates')
        log.info(f"tombstoneNumSubordinates: {tombstone_numsubordinates}")
    except Exception as e:
        log.info(f"tombstoneNumSubordinates not found or error: {e}")
        tombstone_numsubordinates = 0

    log.info("Set up replication between the two instances")
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.create_first_supplier(instance1)
    repl.join_supplier(instance1, instance2)

    log.info("Wait for replication to complete")
    repl.wait_for_replication(instance1, instance2)

    log.info("Check numSubordinates on both instances")
    container_obj1 = OrganizationalUnits(instance1, DEFAULT_SUFFIX).get(container_rdn)
    numsubordinates_instance1 = container_obj1.get_attr_val_int('numSubordinates')
    log.info(f"numSubordinates on instance1: {numsubordinates_instance1}")

    container_obj2 = OrganizationalUnits(instance2, DEFAULT_SUFFIX).get(container_rdn)
    numsubordinates_instance2 = container_obj2.get_attr_val_int('numSubordinates')
    log.info(f"numSubordinates on instance2: {numsubordinates_instance2}")

    log.info("Check tombstoneNumSubordinates on both instances")
    try:
        tombstone_numsubordinates_instance1 = container_obj1.get_attr_val_int('tombstoneNumSubordinates')
        log.info(f"tombstoneNumSubordinates on instance1: {tombstone_numsubordinates_instance1}")
    except Exception as e:
        log.info(f"tombstoneNumSubordinates not found on instance1: {e}")
        tombstone_numsubordinates_instance1 = 0

    try:
        tombstone_numsubordinates_instance2 = container_obj2.get_attr_val_int('tombstoneNumSubordinates')
        log.info(f"tombstoneNumSubordinates on instance2: {tombstone_numsubordinates_instance2}")
    except Exception as e:
        log.info(f"tombstoneNumSubordinates not found on instance2: {e}")
        tombstone_numsubordinates_instance2 = 0

    log.info("Verify that numSubordinates values match on both instances")
    log.info(f"Comparison: instance1 numSubordinates={numsubordinates_instance1}, "
             f"instance2 numSubordinates={numsubordinates_instance2}")
    log.info(f"Comparison: instance1 tombstoneNumSubordinates={tombstone_numsubordinates_instance1}, "
             f"instance2 tombstoneNumSubordinates={tombstone_numsubordinates_instance2}")

    assert numsubordinates_instance1 == numsubordinates_instance2, (
        f"numSubordinates mismatch: instance1 has {numsubordinates_instance1}, "
        f"instance2 has {numsubordinates_instance2}. "
    )
    assert tombstone_numsubordinates_instance1 == tombstone_numsubordinates_instance2, (
        f"tombstoneNumSubordinates mismatch: instance1 has {tombstone_numsubordinates_instance1}, "
        f"instance2 has {tombstone_numsubordinates_instance2}. "
    )

def test_numsubordinates_tombstone_after_import(topo_m2, with_container):
    """Test that numSubordinates values are the expected one after an import

    :id: 67bec454-6bb3-11f0-b9ae-c85309d5c3e3
    :setup: Two suppliers instances with an ou container
    :steps:
        1. Create a container (organizational unit) on the first instance
        2. Create a user object in that container
        3. Delete the user object (this creates a tombstone)
        4. Set up replication between the two instances
        5. Wait for replication to complete
        6. Check numSubordinates on both instances
        7. Check tombstoneNumSubordinates on both instances
        8. Verify that numSubordinates values match on both instances
    :expectedresults:
        1. Container should be created successfully
        2. User object should be created successfully
        3. User object should be deleted successfully
        4. Replication should be set up successfully
        5. Replication should complete successfully
        6. numSubordinates should be accessible on both instances
        7. tombstoneNumSubordinates should be accessible on both instances
        8. numSubordinates values should match on both instances
    """

    S1 = topo_m2.ms["supplier1"]
    S2 = topo_m2.ms["supplier2"]
    container = with_container
    repl = ReplicationManager(DEFAULT_SUFFIX)
    tasks = Tasks(S1)

    log.info("Create some user objects in that container")
    users1 = UserAccounts(S1, DEFAULT_SUFFIX, rdn=f"ou={container.rdn}")
    users = {}
    for uid in range(1001,1010):
        users[uid] = users1.create_test_user(uid=uid)
        log.info(f"Created user: {users[uid].dn}")

    for uid in range(1002,1007,2):
        users[uid].delete()
        log.info(f"Removing user: {users[uid].dn}")
    repl.wait_for_replication(S1, S2)

    ldif_file = f"{S1.get_ldif_dir()}/export.ldif"
    log.info(f"Export into {ldif_file}")
    args = {EXPORT_REPL_INFO: True,
            TASK_WAIT: True}
    tasks.exportLDIF(DEFAULT_SUFFIX, None, ldif_file, args)

    verify_subordinates(S1, container, "before importing")

    # import the ldif file
    log.info(f"Import from {ldif_file}")
    args = {TASK_WAIT: True}
    tasks.importLDIF(DEFAULT_SUFFIX, None, ldif_file, args)

    verify_subordinates(S1, container, "after importing")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
