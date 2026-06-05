# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os

import pytest

from lib389._constants import DEFAULT_BENAME, DEFAULT_SUFFIX
from lib389.cli_ctl.dblib import get_mdb_dbis
from lib389.dbgen import dbgen_users
from lib389.dirsrv_log import DirsrvErrorLog
from lib389.properties import TASK_WAIT
from lib389.replica import Replicas
from lib389.tasks import Tasks
from lib389.utils import get_default_db_lib
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

NUM_USERS = 500000

REINDEX_ATTRS = [
    'uid', 'cn', 'sn', 'givenName', 'mail',
    'telephoneNumber', 'objectclass',
    'entryrdn', 'parentid', 'numsubordinates',
    'ancestorid',
]


def _get_mdb_entry_count(inst, bename, dbiname):
    """Get entry count for a specific DBI via dbscan.  Instance must be stopped."""
    dbhome = inst.ds_paths.db_dir
    dbis = get_mdb_dbis(dbhome)
    be = bename.lower()
    if be in dbis and dbiname in dbis[be]:
        return int(dbis[be][dbiname]['nbentries'])
    return -1


@pytest.fixture(scope="function")
def populated_instance(topo, request):
    """Populate the default backend with 500k users and enable replication."""
    inst = topo.standalone

    # Generate and import users
    import_ldif = os.path.join(inst.ds_paths.ldif_dir, 'reindex_abort_test.ldif')
    dbgen_users(inst, NUM_USERS, import_ldif, DEFAULT_SUFFIX,
                generic=True, parent=f'ou=people,{DEFAULT_SUFFIX}')

    tasks = Tasks(inst)
    assert tasks.importLDIF(
        suffix=DEFAULT_SUFFIX,
        input_file=import_ldif,
        args={TASK_WAIT: True}
    ) == 0

    # Enable replication
    replicas = Replicas(inst)
    replicas.create(properties={
        'cn': 'replica',
        'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
        'nsDS5ReplicaId': '1',
        'nsDS5Flags': '1',
        'nsDS5ReplicaType': '3',
    })

    # Record baseline id2entry count
    inst.stop()
    baseline_count = _get_mdb_entry_count(inst, DEFAULT_BENAME, 'id2entry.db')
    log.info(f"Baseline id2entry count: {baseline_count}")
    assert baseline_count > NUM_USERS, \
        f"Expected at least {NUM_USERS} entries in id2entry, got {baseline_count}"
    inst.start()

    def fin():
        if not inst.status():
            inst.start()
        try:
            replicas = Replicas(inst)
            replica = replicas.get(DEFAULT_SUFFIX)
            replica.delete()
        except Exception:
            pass

    request.addfinalizer(fin)
    return inst, baseline_count


@pytest.mark.skipif(get_default_db_lib() != "mdb",
                    reason="LMDB-specific data loss bug")
def test_reindex_abort_does_not_delete_db(populated_instance):
    """Test that stopping a server during reindex does not destroy data.

    :id: 5a7b3c2e-4f1d-11f0-9b8a-482ae39447e5
    :setup: Standalone instance with 500000 users in the default backend
            and replication enabled
    :steps:
        1. Submit an online reindex task for multiple attributes
        2. Stop the server while reindex is in progress
        3. Check id2entry count - it must match the baseline
        4. Start the server and check that CRIT message about
           incomplete indexes is logged
    :expectedresults:
        1. Success
        2. Success
        3. id2entry count matches the baseline (data preserved)
        4. Server starts successfully and the CRIT message is present
    """
    inst, baseline_count = populated_instance

    # Step 1: Submit reindex task (without TASK_WAIT - returns immediately
    # while the server-side reindex runs in a background thread)
    tasks = Tasks(inst)
    tasks.reindex(suffix=DEFAULT_SUFFIX, attrname=REINDEX_ATTRS)
    log.info("Reindex task submitted")

    # Step 2: Stop the server while reindex is in progress
    log.info("Stopping the server while reindex is in progress")
    inst.stop()

    # Step 3: Verify id2entry count matches baseline
    post_count = _get_mdb_entry_count(inst, DEFAULT_BENAME, 'id2entry.db')
    log.info(f"Post-abort id2entry count: {post_count} "
             f"(baseline: {baseline_count})")
    assert post_count == baseline_count, \
        (f"DATA LOSS: id2entry went from {baseline_count} to "
         f"{post_count} entries after reindex abort. "
         f"The instance directory was incorrectly deleted "
         f"during reindex failure.")

    # Step 4: Verify server starts and CRIT message is logged
    inst.start()
    assert inst.status(), "Server failed to start after reindex abort"

    errlog = DirsrvErrorLog(inst)
    crit_msgs = errlog.match(".*CRIT.*Reindex failed.*backend is unavailable.*")
    assert len(crit_msgs) > 0, \
        "Expected CRIT log message about failed reindex"
    log.info("CRIT message found: %s", crit_msgs[0].strip())
    log.info("Server started successfully - data survived reindex abort")


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
