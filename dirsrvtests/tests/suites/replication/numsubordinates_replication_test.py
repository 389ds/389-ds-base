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
from lib389._constants import DEFAULT_SUFFIX
from lib389.replica import ReplicationManager
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_i2 as topo_i2


pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)