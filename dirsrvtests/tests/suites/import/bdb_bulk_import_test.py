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
import time
import ldap
import pytest
from ldap.extop import ExtendedRequest
from lib389._constants import *
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.replica import Replicas, BootstrapReplicationManager
from lib389.utils import get_default_db_lib
from test389.topologies import topology_st as topo

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

START_OID = "2.16.840.1.113730.3.5.3"   # StartNSDS50ReplicationRequest
ENTRY_OID = "2.16.840.1.113730.3.5.6"   # NSDS50ReplicationEntry
END_OID = "2.16.840.1.113730.3.5.5"     # EndNSDS50ReplicationRequest
TOTAL_PROTOCOL_OID = "2.16.840.1.113730.3.6.2"

REPLICA_READY = 0x00
RELEASE_SUCCEEDED = 0x09

REPL_MANAGER_DN = 'cn=replication manager,cn=config'
REPL_MANAGER_PW = 'replmanagerpw'
FAKE_SUPPLIER_RID = 400

SUFFIX_UUID = "aaaaaaaa-aaaaaaaa-aaaaaaaa-aaaaaaaa"
MARKER_UUID = "55555555-55555555-55555555-55555555"
MARKER_DN = f"ou=pre-abort-marker,{DEFAULT_SUFFIX}"

DUP_UUID_1 = "66666666-66666666-66666666-66666666"
DUP_UUID_2 = "77777777-77777777-77777777-77777777"
DUP_DN = f"ou=duplicate-target,{DEFAULT_SUFFIX}"


# Minimal DER encoders for the total update payloads.  The grammar matches
# the consumer's decoders in repl_extop.c (start/end) and repl5_total.c (entry).
def _ber(tag, payload):
    n = len(payload)
    if n < 0x80:
        lenb = bytes([n])
    else:
        nb = n.to_bytes((n.bit_length() + 7) // 8, 'big')
        lenb = bytes([0x80 | len(nb)]) + nb
    return bytes([tag]) + lenb + payload


def _octet(value):
    return _ber(0x04, value if isinstance(value, bytes) else value.encode())


def _seq(*parts):
    return _ber(0x30, b''.join(parts))


def _set(*parts):
    return _ber(0x31, b''.join(parts))


def _entry_payload(uniqueid, dn, attrs):
    """SEQ { uniqueid, dn, SET OF SEQ { type, SET OF SEQ { value, SEQ{} } } }
    The trailing empty SEQ is the (empty) per-value CSN list."""
    encoded = []
    for atype, values in attrs.items():
        vseqs = [_seq(_octet(v), _seq()) for v in values]
        encoded.append(_seq(_octet(atype), _set(*vseqs)))
    return _seq(_octet(uniqueid), _octet(dn), _set(*encoded))


def _start_payload():
    """SEQ { protocol oid, repl root, SET OF ruv element, csn }"""
    csn = f"{int(time.time()):08x}0000{FAKE_SUPPLIER_RID:04x}0000"
    ruv = [f"{{replicageneration}} {csn}",
           f"{{replica {FAKE_SUPPLIER_RID} ldap://supplier.example.com:389}} {csn} {csn}"]
    return _seq(_octet(TOTAL_PROTOCOL_OID), _octet(DEFAULT_SUFFIX),
                _set(*[_octet(elem) for elem in ruv]), _octet(csn))


def _end_payload():
    return _seq(_octet(DEFAULT_SUFFIX))


def _response_code(raw):
    """The consumer answers SEQ { ENUMERATED code, ... }."""
    assert raw and raw[0] == 0x30, raw.hex()
    pos = 2 + (raw[1] & 0x7f if raw[1] >= 0x80 else 0)
    assert raw[pos] == 0x0a, raw.hex()
    return int.from_bytes(raw[pos + 2:pos + 2 + raw[pos + 1]], 'big')


def _send_entry(conn, uniqueid, dn, attrs):
    try:
        conn.extop_s(ExtendedRequest(ENTRY_OID, _entry_payload(uniqueid, dn, attrs)))
    except ldap.LDAPError as exc:
        raise AssertionError(f"consumer rejected entry {dn}: {exc}") from exc


def _send_suffix(conn):
    _send_entry(conn, SUFFIX_UUID, DEFAULT_SUFFIX, {
        'objectClass': ['top', 'domain'],
        'dc': ['example'],
    })


@pytest.fixture(scope="module")
def repl_consumer(topo):
    """Configure the instance as a replica the sessions below bind to."""
    inst = topo.standalone
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
    return inst


def _supplier_connection(inst):
    """Connection acting as the total update supplier.

    Only the entry extop lets the test choose nsuniqueid values (a plain
    add discards them), and this test needs exact uuid pairing to force
    a duplicate DN. An explicit timeout keeps a synchronous call like
    simple_bind_s()/extop_s() from blocking forever if the server ever
    dies or wedges mid-test."""
    conn = ldap.initialize(f"ldap://{inst.host}:{inst.port}")
    conn.set_option(ldap.OPT_NETWORK_TIMEOUT, 10)
    conn.set_option(ldap.OPT_TIMEOUT, 60)
    conn.simple_bind_s(REPL_MANAGER_DN, REPL_MANAGER_PW)
    return conn


def _successful_total_update(inst, extra=()):
    """Drive one full, successful total update session: start, suffix,
    any extra entries, end. This is the one thing that fails outright if
    the backend/replica is not actually online -- and, unlike a plain
    write, it is something the replication manager bind DN is actually
    entitled to do on a fresh connection: SLAPI_CONN_IS_REPLICATION_SESSION
    (the ACI bypass) is only granted for the duration of a session that
    itself began with a successful StartNSDS50ReplicationRequest on that
    connection (repl_extop.c), not merely by virtue of the bind DN."""
    conn = _supplier_connection(inst)
    try:
        _, raw = conn.extop_s(ExtendedRequest(START_OID, _start_payload()))
        code = _response_code(raw)
        assert code == REPLICA_READY, f"start response {code}"

        _send_suffix(conn)
        for uniqueid, dn, attrs in extra:
            _send_entry(conn, uniqueid, dn, attrs)

        _, raw = conn.extop_s(ExtendedRequest(END_OID, _end_payload()))
        code = _response_code(raw)
        assert code == RELEASE_SUCCEEDED, f"end response {code}"
    except ldap.LDAPError as exc:
        raise AssertionError(f"replication is not working: {exc}") from exc
    finally:
        conn.unbind_s()


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
    # bdb_reenable_backend_after_restore() both ran).
    assert inst.ds_error_log.match(
        ".*Online import failed; restored '.*' to its pre-import state.*")
    assert OrganizationalUnits(inst, DEFAULT_SUFFIX).get('pre-abort-marker') is not None
    # The duplicate belongs to the aborted import and must not have
    # survived the restore.
    assert not OrganizationalUnits(inst, DEFAULT_SUFFIX).filter('(ou=duplicate-target)')

    # Step 7: replication itself (not just the restored data) must still
    # work: bdb_reenable_backend_after_restore() re-enabled the mapping
    # tree node, so a fresh total update must succeed end to end again.
    _successful_total_update(inst)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
