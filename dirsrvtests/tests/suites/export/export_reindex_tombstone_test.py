# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import time
import pytest
import ldap
from lib389.idm.user import UserAccounts
from lib389.idm.domain import Domain
from lib389.tombstone import Tombstones
from lib389.topologies import topology_m2 as topo_m2
from lib389._constants import DEFAULT_SUFFIX, DEFAULT_BENAME, ErrorLog
from lib389.utils import *
from lib389.backend import Backends
from lib389.replica import Replicas, ReplicationManager

pytestmark = pytest.mark.tier1


def test_export_after_reindex_and_tombstone_purge(topo_m2):
    """Test that export with -s works after reindex and tombstone purge.

    :id: 8c5cb603-1f43-46ad-9935-5f518d6d7fe0
    :setup: Two supplier replication topology
    :steps:
        1. Add entries under ou=people on S1, wait for replication to S2
        2. Delete the entries on S1, wait for replication
        3. Perform additional modifications to advance the RUV
        4. Reindex S1
        5. Configure aggressive tombstone purging and wait for tombstones to be purged on S1
        6. Export ou=people,dc=example,dc=com with -s
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Export completes successfully
    """
    S1 = topo_m2.ms["supplier1"]
    S2 = topo_m2.ms["supplier2"]
    PEOPLE = f"ou=people,{DEFAULT_SUFFIX}"

    users = UserAccounts(S1, DEFAULT_SUFFIX, rdn="ou=people")
    test_users = []
    for i in range(5):
        user = users.create_test_user(uid=1234 + i)
        test_users.append(user)
    log.info("Added 5 test entries under ou=people")

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(S1, S2)

    for user in test_users:
        user.delete()
    log.info("Deleted 5 test entries")

    repl.wait_for_replication(S1, S2)

    domain = Domain(S1, DEFAULT_SUFFIX)
    for i in range(10):
        domain.replace("description", f"advancing RUV {i}")
    repl.wait_for_replication(S1, S2)

    domain2 = Domain(S2, DEFAULT_SUFFIX)
    for i in range(10):
        domain2.replace("description", f"advancing RUV from S2 {i}")
    repl.wait_for_replication(S2, S1)
    log.info("RUV advanced on both suppliers")

    S1.stop()
    S1.db2index(DEFAULT_BENAME)
    log.info("Reindex completed on S1")
    S1.start()

    replica = Replicas(S1).get(DEFAULT_SUFFIX)
    replica.replace("nsDS5ReplicaPurgeDelay", "1")
    replica.replace("nsDS5ReplicaTombstonePurgeInterval", "1")

    S1.config.loglevel((ErrorLog.REPLICA,), "error")

    log.info("Waiting for tombstone purge on S1...")
    tombstones = Tombstones(S1, PEOPLE)
    for attempt in range(60):
        time.sleep(2)
        ts_list = tombstones.list()
        if len(ts_list) == 0:
            log.info(f"All tombstones purged after {(attempt + 1) * 2}s")
            break
        log.info(f"Attempt {attempt + 1}: {len(ts_list)} tombstones remaining")
    else:
        pytest.fail("Tombstones not purged after 120s")

    S1.deleteErrorLogs()

    backends = Backends(S1)
    task = backends.export_ldif(be_names=[DEFAULT_BENAME], include_suffixes=[PEOPLE])
    task.wait()
    assert task.is_complete()
    assert task.get_exit_code() == 0

    log.info("Export after reindex + tombstone purge succeeded")
