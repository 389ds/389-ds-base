# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import ldap
import pytest
from lib389.topologies import topology_m2
from lib389.idm.user import UserAccounts
from lib389._constants import DEFAULT_SUFFIX
from lib389.replica import Replicas
from lib389.tombstone import Tombstones

# Constants for user names
USER1_UID = "1"
USER2_UID = "2"
NEW_USER2_UID = "new_user2"

pytestmark = pytest.mark.tier2


def test_replication_with_mod_delete_and_modrdn_operations(topology_m2):
    """ Test replication with modifications

    :id: d7798eb7-8b04-486a-95ea-4cd1a5031fdb
    :setup: Two supplier instances (S1 and S2) with initial users
    :steps:
        1. Pause all replication agreements
        2. Perform a delete operation on S1 (e.g., delete a user)
        3. Perform a modify operation on S1 (e.g., change the description of a test user)
        4. Sleep for 1 second to ensure CSNs are different
        5. Perform a modrdn operation on S2 (e.g., rename a user)
        6. Perform a modify operation on S2 (e.g., change the description of a test user)
        7. Resume all replication agreements
        8. Sleep for 5 seconds to allow replication to propagate
        9. Validate that replication is working
    :expectedresults:
        1. All replication agreements should be paused successfully
        2. User should be deleted on S1
        3. Description should be modified for a test user on S1
        4. 1 second should elapse
        5. User should be renamed on S2
        6. Description should be modified for a test user on S2
        7. All replication agreements should be resumed
        8. Sufficient time should pass to allow replication to propagate
        9. Entries should be in the expected state on both servers
    """
    
    S1 = topology_m2.ms["supplier1"]
    S2 = topology_m2.ms["supplier2"]

    # Add entries for the test
    users_s1 = UserAccounts(S1, DEFAULT_SUFFIX)
    user1 = users_s1.create_test_user(uid=USER1_UID)
    test1 = users_s1.create_test_user(uid=USER2_UID)
    time.sleep(5)

    topology_m2.pause_all_replicas()

    users_s2 = UserAccounts(S2, DEFAULT_SUFFIX)
    user2 = users_s2.get(f"test_user_{USER1_UID}")
    test2 = users_s2.get(f"test_user_{USER2_UID}")

    # Delete operation on S1
    user1.delete()

    # Modify operation on S1
    test1.replace("description", "modified on S1")

    # Ensure CSN different
    time.sleep(1)

    # modrdn operation on S2
    user2.rename(f"uid={NEW_USER2_UID}")

    # Modify operation on S2
    test2.replace("description", "modified on S2")

    # Resume all replication
    topology_m2.resume_all_replicas()

    # Check if replication is working
    time.sleep(5)

    assert not users_s1.exists(f"test_user_{USER1_UID}")
    assert not users_s2.exists(f"test_user_{USER1_UID}")
    assert not users_s1.exists(NEW_USER2_UID)
    assert not users_s2.exists(NEW_USER2_UID)

    tombstones = Tombstones(S1, DEFAULT_SUFFIX)
    assert tombstones.filter(f"(&(objectClass=nstombstone)(uid=test_user_{USER1_UID}))")
    tombstones = Tombstones(S2, DEFAULT_SUFFIX)
    assert tombstones.filter(f"(&(objectClass=nstombstone)(uid={NEW_USER2_UID}))")

    assert test1.get_attr_val_utf8("description") == "modified on S2"
    assert test2.get_attr_val_utf8("description") == "modified on S2"