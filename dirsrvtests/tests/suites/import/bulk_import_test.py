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
import ldap
import pytest
from ldap.extop import ExtendedRequest
from lib389._constants import *
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.tombstone import Tombstones
from lib389.utils import get_default_db_lib
from test389.topologies import topology_st as topo
from . import (_end_payload, _response_code, _send_entry, _send_suffix,
               _start_payload, _successful_total_update, _supplier_connection,
               repl_consumer, MARKER_DN, MARKER_UUID, REPLICA_READY,
               RELEASE_SUCCEEDED, START_OID, END_OID)

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(get_default_db_lib() != "mdb",
                                 reason="Exercises the LMDB bulk import consumer")]

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

ANCESTOR_UUID = "bbbbbbbb-bbbbbbbb-bbbbbbbb-bbbbbbbb"
PARENT_UUID = "11111111-11111111-11111111-11111111"
CHILD_UUID = "22222222-22222222-22222222-22222222"

ANCESTOR_DN = f"ou=init-ancestor,{DEFAULT_SUFFIX}"
PARENT_DN = f"ou=init-parent,{ANCESTOR_DN}"
TOMBSTONE_DN = f"nsuniqueid={CHILD_UUID},uid=init-orphan,{PARENT_DN}"

MISSING_PARENT_UUID = "44444444-44444444-44444444-44444444"
ORPHAN_UUID = "33333333-33333333-33333333-33333333"
ORPHAN_DN = f"nsuniqueid={ORPHAN_UUID},uid=stranded-orphan,{DEFAULT_SUFFIX}"


def test_lmdb_bulk_import_tombstone_released_by_live_parent(repl_consumer):
    """A tombstone processed before its live parent must not be lost

    :id: 33dc80a2-28b7-4571-9cda-18c93f855c3a
    :setup: Standalone instance using the LMDB backend, configured as a replica
    :steps:
        1. Start a total update session and send the suffix entry
        2. Send a parent whose own parent has not been sent yet
        3. Send a tombstone whose nsparentuniqueid names that parent
        4. Send more entries than the bulk queue can hold
        5. Send the missing ancestor, then finish the session
        6. Search for the parent and for the tombstone
    :expectedresults:
        1. The consumer answers REPLICA_READY
        2. The parent parks on the waiting queue, unregistered
        3. The tombstone parks, keyed by the parent's nsuniqueid
        4. The tombstone's batch is drained before the ancestor arrives
        5. The ancestor releases the parent, the parent releases the
           tombstone, and the session ends successfully
        6. Both entries were imported
    """
    inst = repl_consumer
    supplier = _supplier_connection(inst)

    started = False
    try:
        _, raw = supplier.extop_s(ExtendedRequest(START_OID, _start_payload()))
        code = _response_code(raw)
        assert code == REPLICA_READY, f"start response {code}"
        started = True

        _send_suffix(supplier)
        # init-ancestor is sent last, so init-parent cannot register
        # before the tombstone is processed, whatever the batching.
        _send_entry(supplier, PARENT_UUID, PARENT_DN, {
            'objectClass': ['top', 'organizationalUnit'],
            'ou': ['init-parent'],
        })
        # Parks on waitingq keyed by PARENT_UUID (the comparison broken
        # by 4b82bf6d3).
        _send_entry(supplier, CHILD_UUID, TOMBSTONE_DN, {
            'objectClass': ['top', 'person', 'extensibleObject', 'nsTombstone'],
            'uid': ['init-orphan'],
            'cn': ['init-orphan'],
            'sn': ['init-orphan'],
            'nsparentuniqueid': [PARENT_UUID],
        })
        # 65 fillers exceed the 64-slot bulk queue, keeping the ancestor
        # out of the tombstone's LIFO-drained batch.
        for idx in range(65):
            _send_entry(supplier, f"eeeeeeee-{idx:08x}-eeeeeeee-eeeeeeee",
                        f"ou=filler-{idx},{DEFAULT_SUFFIX}", {
                            'objectClass': ['top', 'organizationalUnit'],
                            'ou': [f"filler-{idx}"],
                        })
        _send_entry(supplier, ANCESTOR_UUID, ANCESTOR_DN, {
            'objectClass': ['top', 'organizationalUnit'],
            'ou': ['init-ancestor'],
        })

        _, raw = supplier.extop_s(ExtendedRequest(END_OID, _end_payload()))
        code = _response_code(raw)
        assert code == RELEASE_SUCCEEDED, f"end response {code}"
        started = False
    finally:
        if started:
            # Do not leave a half-open session/import behind.
            try:
                supplier.extop_s(ExtendedRequest(END_OID, _end_payload()))
            except ldap.LDAPError:
                pass
        supplier.unbind_s()

    # Surface the producer's log lines so a failure names its cause.
    for pattern in ('.*was never imported.*', '.*Aborting bulk import.*'):
        for line in inst.ds_error_log.match(pattern):
            log.info("error log: %s", line.strip())

    assert OrganizationalUnits(inst, ANCESTOR_DN).get('init-parent') is not None
    found = Tombstones(inst, DEFAULT_SUFFIX).filter(f'(nsUniqueId={CHILD_UUID})')
    log.info("%d matching tombstones found after total update", len(found))
    assert len(found) == 1


def test_lmdb_bulk_import_aborts_on_stranded_tombstone(repl_consumer, request):
    """A tombstone whose parent never arrives must be named and abort the import

    :id: b7f2a9c1-4d3e-4f5a-9b8c-2e6d7a1f0c43
    :setup: Standalone instance using the LMDB backend, configured as a replica
    :steps:
        1. Run a normal, successful total update establishing a marker entry
        2. Start a new total update session and send the suffix entry
        3. Send a tombstone whose nsparentuniqueid never appears in the stream
        4. Finish the session
        5. Check the error log
        6. Confirm the marker entry survived and the stranded orphan did not
        7. Run another normal, successful total update
    :expectedresults:
        1. The consumer answers REPLICA_READY, then RELEASE_SUCCEEDED
        2. The consumer answers REPLICA_READY
        3. The tombstone parks on the waiting queue
        4. The session ends
        5. The stranded entry is named in the error log and the import aborts
        6. The backup made before the aborted import was restored (the
           marker is back), not the half-built aborted import (no orphan)
        7. Replication itself (not just the restored data) still works
           after the abort/restore: the consumer answers REPLICA_READY,
           then RELEASE_SUCCEEDED again
    """
    inst = repl_consumer

    def fin():
        # dbmdb_import_backup_create()/_restore() already put the backend
        # back online (see the assertions below); this is just a defensive
        # safety net in case that ever regresses.
        try:
            _successful_total_update(inst)
        except AssertionError as exc:
            log.warning("restore import failed: %s", exc)

    request.addfinalizer(fin)

    # Step 1: a normal, successful total update both proves replication
    # works before we deliberately break it, and establishes a marker
    # entry this test owns (decoupled from the previous test's data).
    _successful_total_update(inst, extra=[(MARKER_UUID, MARKER_DN, {
        'objectClass': ['top', 'organizationalUnit'],
        'ou': ['pre-abort-marker'],
    })])
    assert OrganizationalUnits(inst, DEFAULT_SUFFIX).get('pre-abort-marker') is not None

    supplier = _supplier_connection(inst)
    try:
        _, raw = supplier.extop_s(ExtendedRequest(START_OID, _start_payload()))
        code = _response_code(raw)
        assert code == REPLICA_READY, f"start response {code}"

        _send_suffix(supplier)
        # Parks on a nsuniqueid that no entry in the stream will register.
        _send_entry(supplier, ORPHAN_UUID, ORPHAN_DN, {
            'objectClass': ['top', 'person', 'extensibleObject', 'nsTombstone'],
            'uid': ['stranded-orphan'],
            'cn': ['stranded-orphan'],
            'sn': ['stranded-orphan'],
            'nsparentuniqueid': [MISSING_PARENT_UUID],
        })
        # No response-code assert: the DONE path still reports success
        # after an abort (separate propagation issue).
        supplier.extop_s(ExtendedRequest(END_OID, _end_payload()))
    finally:
        supplier.unbind_s()

    detail = inst.ds_error_log.match(
        '.*dbmdb_bulk_producer.*stranded-orphan.*was never imported.*')
    for line in detail:
        log.info("error log: %s", line.strip())
    assert len(detail) == 1
    assert inst.ds_error_log.match(
        '.*Aborting bulk import: 1 entries had no parent in the import stream.*')

    # The abort wipes the backend before restoring it from the pre-import
    # backup -- confirm step 1's marker came back and the backend answers
    # searches again (dbmdb_import_backup_restore() +
    # import_reenable_backend_after_restore() both ran).
    assert inst.ds_error_log.match(
        ".*Online import failed; restored '.*' to its pre-import state.*")
    assert OrganizationalUnits(inst, DEFAULT_SUFFIX).get('pre-abort-marker') is not None
    # The stranded orphan belongs to the aborted import and must not
    # have survived the restore.
    assert not Tombstones(inst, DEFAULT_SUFFIX).filter(f'(nsUniqueId={ORPHAN_UUID})')

    # Step 7: replication itself (not just the restored data) must still
    # work: import_reenable_backend_after_restore() re-enabled the mapping
    # tree node, so a fresh total update must succeed end to end again.
    _successful_total_update(inst)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
