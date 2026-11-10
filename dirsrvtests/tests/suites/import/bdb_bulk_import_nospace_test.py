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
import subprocess
import pytest
from ldap.extop import ExtendedRequest
from lib389._constants import *
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.replica import Replicas, BootstrapReplicationManager
from lib389.utils import get_default_db_lib
from test389.topologies import topology_st as topo
from . import (_end_payload, _response_code, _send_entry, _send_suffix,
               _start_payload, _successful_total_update, _supplier_connection,
               MARKER_DN, MARKER_UUID, REPL_MANAGER_PW, REPLICA_READY,
               START_OID, END_OID)

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(get_default_db_lib() != "bdb",
                                 reason="Exercises the BDB bulk import consumer's "
                                        "pre-import backup/restore on failure"),
              pytest.mark.skipif(not os.environ.get('DISK_MONITORING_ACK', False),
                                 reason="Mounts a tmpfs over the backend directory to "
                                        "simulate a full disk; opt in with DISK_MONITORING_ACK=1, "
                                        "same acknowledgement as suites/disk_monitoring")]

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

# Small enough to fill quickly with a modest amount of padded content;
# big enough that the instance can still start and run a tiny baseline
# import before the deliberately oversized one below.
TMPFS_SIZE_MB = 8

# A large attribute value, comfortably under nsslapd-maxbersize's 2MB
# default so it is never rejected at the protocol level -- the tmpfs is
# meant to run out on the server side, not the client side.
PADDING_VALUE = 'X' * 100_000


def _mount_tiny_tmpfs(inst, size_mb):
    """Mount a small tmpfs directly over the instance's db directory, so
    every BDB backend sharing it (just userRoot here) starts fresh and is
    capped at a real, small amount of space. The backup rename in
    bdb_import_backup_create()/_restore() stays on this same filesystem
    (sibling path, same mount), so only the actual data volume -- not the
    cheap rename itself -- is expected to hit ENOSPC."""
    inst.stop()
    db_dir = inst.dbdir
    if not os.path.exists(db_dir):
        os.makedirs(db_dir)
    rc = subprocess.call(['mount', '-t', 'tmpfs', '-o', f'size={size_mb}M', 'tmpfs', db_dir])
    if rc != 0:
        pytest.skip(f"could not mount a tmpfs on {db_dir} (rc={rc}); "
                   f"this test needs mount/umount privileges")
    subprocess.call(f'chown {DEFAULT_USER}: -R {db_dir}', shell=True)
    subprocess.call(f'restorecon -FvvR {db_dir}', shell=True)
    inst.start()


def _unmount_tmpfs(inst):
    inst.stop()
    subprocess.call(['umount', inst.dbdir])


def test_bdb_bulk_import_aborts_on_disk_full(topo, request):
    """A bulk import that genuinely runs out of disk space must abort
    cleanly, and the backend must come back from its pre-import backup
    afterward

    :id: 74ef90ba-72ae-4c97-b4f0-10ba4f9dd83b
    :setup: Standalone instance using the BDB backend, with its db
        directory mounted on a small (8MB) tmpfs, configured as a replica
    :steps:
        1. Mount a small tmpfs over the instance's db directory and restart
        2. Run a normal, successful total update establishing a marker entry
        3. Start a new total update session and send the suffix entry
        4. Send far more padded entries than the tmpfs can hold
        5. Finish the session
        6. Check the error log
        7. Confirm the marker entry survived and none of the filler did
        8. Run another normal, successful total update
    :expectedresults:
        1. Success
        2. The consumer answers REPLICA_READY, then RELEASE_SUCCEEDED
        3. The consumer answers REPLICA_READY
        4. A write eventually fails with ENOSPC
        5. The session ends
        6. The import is reported as aborted/failed
        7. The backup made before the aborted import was restored (the
           marker is back), not the half-built aborted import (no filler)
        8. Replication itself (not just the restored data) still works
           after the abort/restore: the consumer answers REPLICA_READY,
           then RELEASE_SUCCEEDED again
    """
    inst = topo.standalone

    def fin():
        try:
            _unmount_tmpfs(inst)
        except Exception as exc:
            log.warning("could not unmount tmpfs during cleanup: %s", exc)
        inst.start()

    request.addfinalizer(fin)

    # Step 1: fresh, tmpfs-capped db directory.
    _mount_tiny_tmpfs(inst, TMPFS_SIZE_MB)

    mgr = BootstrapReplicationManager(inst)
    mgr.create(properties={'cn': 'replication manager',
                           'userPassword': REPL_MANAGER_PW})
    Replicas(inst).create(properties={
        'cn': 'replica',
        'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
        'nsDS5ReplicaId': '1',
        'nsDS5ReplicaType': '3',
        'nsDS5Flags': '1',
        'nsDS5ReplicaBindDN': mgr.dn,
    })

    # Step 2: a normal, successful total update both proves replication
    # works on the tmpfs-backed instance, and establishes a marker entry
    # this test owns. The backend is empty at this point, so the
    # pre-import backup this takes (a cheap directory rename) costs
    # almost nothing in tmpfs space.
    _successful_total_update(inst, extra=[(MARKER_UUID, MARKER_DN, {
        'objectClass': ['top', 'organizationalUnit'],
        'ou': ['pre-abort-marker'],
    })])
    assert OrganizationalUnits(inst, DEFAULT_SUFFIX).get('pre-abort-marker') is not None

    # Steps 3-5: feed far more padded content than the 8MB tmpfs can
    # possibly hold (roughly 15MB raw, before any per-record overhead)
    # in a single session, so the rebuild itself hits ENOSPC.
    supplier = _supplier_connection(inst)
    try:
        _, raw = supplier.extop_s(ExtendedRequest(START_OID, _start_payload()))
        code = _response_code(raw)
        assert code == REPLICA_READY, f"start response {code}"

        _send_suffix(supplier)
        for i in range(150):
            _send_entry(supplier, f"dddddddd-{i:08x}-dddddddd-dddddddd",
                        f"ou=filler-{i},{DEFAULT_SUFFIX}", {
                            'objectClass': ['top', 'organizationalUnit'],
                            'ou': [f"filler-{i}"],
                            'description': [PADDING_VALUE],
                        })
        # No response-code assert: the DONE path still reports success
        # after an abort (separate propagation issue, same as the LMDB
        # and duplicate-DN bulk import abort cases).
        supplier.extop_s(ExtendedRequest(END_OID, _end_payload()))
    finally:
        supplier.unbind_s()

    # Step 6: the abort wipes the backend before restoring it from the
    # pre-import backup -- the exact underlying BDB error is not matched
    # here (ENOSPC is reported at whichever index write first hits it,
    # with no single fixed message), but the generic restore-after-failure
    # notice is the same one already proven by the duplicate-DN case.
    assert inst.ds_error_log.match(
        ".*Online import failed; restored '.*' to its pre-import state.*")

    # Step 7
    assert OrganizationalUnits(inst, DEFAULT_SUFFIX).get('pre-abort-marker') is not None
    assert not OrganizationalUnits(inst, DEFAULT_SUFFIX).filter('(ou=filler-0)')

    # Step 8: replication itself (not just the restored data) must still
    # work: import_reenable_backend_after_restore() re-enabled the mapping
    # tree node, so a fresh total update must succeed end to end again.
    _successful_total_update(inst)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
