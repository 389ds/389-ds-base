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
from ldap.extop import ExtendedRequest
from lib389._constants import *
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.utils import get_default_db_lib
from test389.topologies import topology_st as topo
from . import (_end_payload, _response_code, _send_entry, _send_suffix,
               _start_payload, _successful_total_update, _supplier_connection,
               repl_consumer, MARKER_DN, MARKER_UUID, REPLICA_READY,
               START_OID, END_OID)

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(get_default_db_lib() != "bdb",
                                 reason="Exercises the BDB bulk import consumer's "
                                        "pre-import backup/restore on failure")]

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

DUP_UUID_1 = "66666666-66666666-66666666-66666666"
DUP_UUID_2 = "77777777-77777777-77777777-77777777"
DUP_DN = f"ou=duplicate-target,{DEFAULT_SUFFIX}"


def test_bdb_bulk_import_aborts_on_duplicate_dn(repl_consumer, request):
    """A duplicate DN in the stream must abort the import, and the backend
    must come back from its pre-import backup afterward

    :id: a2c60d10-605a-43cf-9456-91190871ce4b
    :setup: Standalone instance using the BDB backend, configured as a replica
    :steps:
        1. Run a normal, successful total update establishing a marker entry
        2. Start a new total update session and send the suffix entry
        3. Send two entries that share the same DN
        4. Finish the session
        5. Check the error log
        6. Confirm the marker entry survived and the duplicate did not
        7. Run another normal, successful total update
    :expectedresults:
        1. The consumer answers REPLICA_READY, then RELEASE_SUCCEEDED
        2. The consumer answers REPLICA_READY
        3. The second entry is rejected as a duplicate DN, which aborts
           the whole import (unlike BDB's lenient "no parent yet" skip)
        4. The session ends
        5. The duplicate DN is named in the error log and the import aborts
        6. The backup made before the aborted import was restored (the
           marker is back), not the half-built aborted import (no
           duplicate-target entry)
        7. Replication itself (not just the restored data) still works
           after the abort/restore: the consumer answers REPLICA_READY,
           then RELEASE_SUCCEEDED again
    """
    inst = repl_consumer

    def fin():
        # bdb_import_backup_create()/_restore() already put the backend
        # back online (see the assertions below); this is just a defensive
        # safety net in case that ever regresses.
        try:
            _successful_total_update(inst)
        except AssertionError as exc:
            log.warning("restore import failed: %s", exc)

    request.addfinalizer(fin)

    # Step 1: a normal, successful total update both proves replication
    # works before we deliberately break it, and establishes a marker
    # entry this test owns.
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
        _send_entry(supplier, DUP_UUID_1, DUP_DN, {
            'objectClass': ['top', 'organizationalUnit'],
            'ou': ['duplicate-target'],
        })
        # Same DN, different nsuniqueid: entryrdn_index_entry() reports
        # LDBM_ERROR_FOUND_DUPDN (bdb_foreman_do_entryrdn() in
        # bdb_import_threads.c); outside FLAG_UPGRADEDNFORMAT this is not
        # a skip like "entry has no parent yet" is -- it goes to the
        # foreman's error: label (info->state = ABORTED), which aborts
        # the whole import.
        _send_entry(supplier, DUP_UUID_2, DUP_DN, {
            'objectClass': ['top', 'organizationalUnit'],
            'ou': ['duplicate-target'],
        })
        # No response-code assert: the DONE path still reports success
        # after an abort (separate propagation issue, same as the LMDB
        # bulk_import_test.py case).
        supplier.extop_s(ExtendedRequest(END_OID, _end_payload()))
    finally:
        supplier.unbind_s()

    detail = inst.ds_error_log.match(
        '.*Duplicated DN detected.*duplicate-target.*')
    for line in detail:
        log.info("error log: %s", line.strip())
    assert len(detail) == 1

    # The abort wipes the backend before restoring it from the pre-import
    # backup -- confirm step 1's marker came back and the backend answers
    # searches again (bdb_import_backup_restore() +
    # import_reenable_backend_after_restore() both ran).
    assert inst.ds_error_log.match(
        ".*Online import failed; restored '.*' to its pre-import state.*")
    assert OrganizationalUnits(inst, DEFAULT_SUFFIX).get('pre-abort-marker') is not None
    # The duplicate belongs to the aborted import and must not have
    # survived the restore.
    assert not OrganizationalUnits(inst, DEFAULT_SUFFIX).filter('(ou=duplicate-target)')

    # Step 7: replication itself (not just the restored data) must still
    # work: import_reenable_backend_after_restore() re-enabled the mapping
    # tree node, so a fresh total update must succeed end to end again.
    _successful_total_update(inst)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
