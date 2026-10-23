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
from lib389.idm.domain import Domain
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.config import LMDB_LDBMConfig
from lib389.replica import Replicas, BootstrapReplicationManager
from lib389.utils import get_default_db_lib
from test389.topologies import topology_st as topo

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(get_default_db_lib() != "mdb",
                                 reason="Exercises the LMDB mapsize/backup capacity checks")]

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

# A small, deliberately undersized LMDB map: big enough for the server to
# start and do a handful of small operations, far too small to hold a
# bulk import of hundreds of sizeable entries. Matches the TINY_MAP_SIZE
# already used for the equivalent offline-import test in import_test.py
# (test_crash_on_ldif2db_with_lmdb).
TINY_MAP_SIZE = 16 * 1024 * 1024

# A large attribute value, comfortably under nsslapd-maxbersize's 2MB
# default so it is never rejected at the protocol level -- the map is
# meant to run out on the server side, not the client side.
PADDING_VALUE = 'X' * 100_000


# Minimal DER encoders for the total update payloads. The grammar matches
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
    except ldap.LDAPError as e:
        log.error(f"consumer rejected entry {dn}: {e}")
        raise e


def _send_suffix(conn):
    _send_entry(conn, SUFFIX_UUID, DEFAULT_SUFFIX, {
        'objectClass': ['top', 'domain'],
        'dc': ['example'],
    })


@pytest.fixture(scope="module")
def repl_consumer(topo):
    """Configure the instance as a replica the sessions below bind to.

    The replica configuration (like all of cn=config) lives in dse.ldif,
    not in the LMDB-backed backend, so it survives the data.mdb wipes the
    tests below perform and only needs to be created once per module."""
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
    """Connection acting as the total update supplier."""
    conn = ldap.initialize(f"ldap://{inst.host}:{inst.port}")
    conn.simple_bind_s(REPL_MANAGER_DN, REPL_MANAGER_PW)
    return conn


def _successful_total_update(inst, extra=()):
    """Drive one full, successful total update session: start, suffix,
    any extra entries, end."""
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


def _shrink_mapsize_and_restart(inst, mapsize):
    """Reconfigure nsslapd-mdb-max-size and recreate the LMDB environment
    from scratch at the new (smaller) size.

    LMDB cannot shrink an already-open environment below its previously
    recorded mapsize, so -- exactly like the equivalent offline-import
    test (test_crash_on_ldif2db_with_lmdb in import_test.py) -- the
    existing data.mdb/INFO.mdb/lock.mdb must be removed before restarting
    with the smaller value. This wipes every backend sharing the
    environment; cn=config (including the replica configured by
    repl_consumer) is unaffected since it lives in dse.ldif."""
    handler = LMDB_LDBMConfig(inst)
    handler.replace('nsslapd-mdb-max-size', str(mapsize))
    inst.stop()
    for dbfile in ('data.mdb', 'INFO.mdb', 'lock.mdb'):
        try:
            os.remove(f'{inst.dbdir}/{dbfile}')
        except FileNotFoundError:
            pass
    inst.start()


def _fill_backend_past_half_capacity(inst, max_attempts=75):
    """Add padded ou entries under DEFAULT_SUFFIX, past half of the
    configured mapsize but comfortably short of it.

    Filling the backend all the way to its real ceiling leaves no free
    pages for internal bookkeeping either, so the instance stops being
    usable at all -- even unrelated reads start failing. Stopping at
    roughly half avoids that: the backend itself stays small enough to
    remain fully usable, but a pre-import backup -- which has to clone
    this content into a second copy alongside the original -- no longer
    has room to fit, which is exactly the condition under test here.

    max_attempts=75 at PADDING_VALUE's 100KB each is ~7.5MB, a bit past
    half of TINY_MAP_SIZE's 16MB. The try/except below is just a safety
    net in case the server rejects an add before max_attempts is reached;
    that is not expected to happen at this size.

    Returns the number of filler entries added."""
    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    added = 0
    for i in range(max_attempts):
        try:
            ous.create(properties={'ou': f'filler-{i}', 'description': [PADDING_VALUE]})
            added += 1
        except ldap.LDAPError as exc:
            log.info("backend reached capacity after %d entries: %s", added, exc)
            break

    return added


def test_lmdb_bulk_import_skips_backup_when_map_is_full(repl_consumer, request):
    """When there is no free map space left to clone it, the pre-import
    backup is skipped -- not the import itself

    :id: 12355325-c387-41b9-8e84-8df5955c2295
    :setup: Standalone instance using the LMDB backend, configured as a
        replica, with nsslapd-mdb-max-size shrunk to 16MB
    :steps:
        1. Recreate the backend from scratch at the tiny map size
        2. Fill the backend with padded entries to just past half the map
        3. Run a trivial, otherwise-successful total update (suffix only)
        4. Check the error log
        5. Confirm the backend only holds the fresh (post-import) content
    :expectedresults:
        1. Success
        2. All filler entries are added successfully -- the backend is
           still comfortably usable, just too large to clone for a backup
        3. The consumer answers REPLICA_READY, then RELEASE_SUCCEEDED --
           the import proceeds even though no backup could be taken
        4. dbmdb_import_backup_check_room() logged that there was not
           enough free map space for a pre-import backup
        5. The old filler entries are gone and the suffix is back (the
           import wiped and rebuilt the backend as usual; skipping the
           backup does not block or change the import itself)
    """
    inst = repl_consumer

    def fin():
        try:
            _successful_total_update(inst)
        except AssertionError as exc:
            log.warning("restore import failed: %s", exc)

    request.addfinalizer(fin)

    # Step 1: fresh, tiny-mapsize environment. The suffix root and the
    # filler entries below are added as plain LDAP operations (not a
    # total update) -- only the actual capacity-check trigger in step 3
    # needs to go through the bulk-import consumer.
    _shrink_mapsize_and_restart(inst, TINY_MAP_SIZE)
    Domain(inst, DEFAULT_SUFFIX).create(properties={'dc': DEFAULT_SUFFIX.split('=')[1].split(',')[0]})

    # Step 2: fill the backend past half of TINY_MAP_SIZE. The backend
    # itself stays comfortably within capacity and fully usable -- only a
    # pre-import backup, which has to clone this content into a second
    # copy alongside the original, no longer has room to fit.
    added = _fill_backend_past_half_capacity(inst)
    assert added > 0

    # Step 3: a bulk import that only adds the (tiny) suffix entry would
    # normally succeed trivially. With the backend already past half the
    # map, dbmdb_import_backup_check_room() cannot find room to clone its
    # current footprint, so dbmdb_import_backup_create() fails -- but that
    # must not fail the import itself.
    _successful_total_update(inst)

    # Step 4
    assert inst.ds_error_log.match(
        '.*Not enough free map space for a pre-import backup.*')

    # Step 5: the import wipes and rebuilds the backend regardless of
    # whether a backup was taken; only the fresh suffix should remain.
    assert not OrganizationalUnits(inst, DEFAULT_SUFFIX).list()


def test_lmdb_bulk_import_aborts_on_map_full(repl_consumer, request):
    """A bulk import that genuinely outgrows the configured map size must
    abort cleanly, and the backend must come back from its pre-import
    backup afterward

    :id: 11660201-344d-4907-968f-a10b4d3d536a
    :setup: Standalone instance using the LMDB backend, configured as a
        replica, with nsslapd-mdb-max-size shrunk to 16MB
    :steps:
        1. Recreate the backend from scratch at the tiny map size
        2. Run a normal, successful total update establishing a marker entry
        3. Start a new total update session and send the suffix entry
        4. Send far more padded entries than the 16MB map can hold
        5. Finish the session
        6. Check the error log
        7. Confirm the marker entry survived and none of the filler did
        8. Run another normal, successful total update
    :expectedresults:
        1. Success
        2. The consumer answers REPLICA_READY, then RELEASE_SUCCEEDED
        3. The consumer answers REPLICA_READY
        4. A write eventually fails with MDB_MAP_FULL
        5. The session ends
        6. The write failure is named and the import aborts
        7. The backup made before the aborted import was restored (the
           marker is back), not the half-built aborted import (no filler)
        8. Replication itself (not just the restored data) still works
           after the abort/restore: the consumer answers REPLICA_READY,
           then RELEASE_SUCCEEDED again
    """
    inst = repl_consumer

    def fin():
        try:
            _successful_total_update(inst)
        except AssertionError as exc:
            log.warning("restore import failed: %s", exc)

    request.addfinalizer(fin)

    # Step 1: fresh, tiny-mapsize environment.
    _shrink_mapsize_and_restart(inst, TINY_MAP_SIZE)

    # Step 2: a normal, successful total update both proves replication
    # works at this tiny map size, and establishes a marker entry this
    # test owns. The backend is empty at this point, so the pre-import
    # backup this takes is trivially small.
    _successful_total_update(inst, extra=[(MARKER_UUID, MARKER_DN, {
        'objectClass': ['top', 'organizationalUnit'],
        'ou': ['pre-abort-marker'],
    })])
    assert OrganizationalUnits(inst, DEFAULT_SUFFIX).get('pre-abort-marker') is not None

    # Steps 3-5: feed far more padded content than 16MB can possibly
    # hold (roughly 30MB raw, before any per-record overhead) in a
    # single session, so the rebuild itself outgrows the map.
    supplier = _supplier_connection(inst)
    try:
        _, raw = supplier.extop_s(ExtendedRequest(START_OID, _start_payload()))
        code = _response_code(raw)
        assert code == REPLICA_READY, f"start response {code}"

        try:
            _send_suffix(supplier)
            for i in range(300):
                _send_entry(supplier, f"dddddddd-{i:08x}-dddddddd-dddddddd",
                            f"ou=filler-{i},{DEFAULT_SUFFIX}", {
                                'objectClass': ['top', 'organizationalUnit'],
                                'ou': [f"filler-{i}"],
                                'description': [PADDING_VALUE],
                            })
            # No response-code assert: the DONE path still reports success
            # after an abort (separate propagation issue, same as
            # bulk_import_test.py's stranded-tombstone case).
        except ldap.SERVER_DOWN:
            # Conn is closed once things go wrong (expected), so reconnect
            supplier = _supplier_connection(inst)
        supplier.extop_s(ExtendedRequest(END_OID, _end_payload()))
    finally:
        supplier.unbind_s()

    # Step 6
    detail = inst.ds_error_log.match('.*Failed to write record in dbi.*')
    for line in detail:
        log.info("error log: %s", line.strip())
    assert detail
    assert inst.ds_error_log.match('.*Aborting import after failure.*')
    assert inst.ds_error_log.match(
        ".*Online import failed; restored '.*' to its pre-import state.*")

    # Step 7: the abort wipes the backend before restoring it from the
    # pre-import backup -- confirm step 2's marker came back and none of
    # the aborted import's filler data survived.
    assert OrganizationalUnits(inst, DEFAULT_SUFFIX).get('pre-abort-marker') is not None
    assert not OrganizationalUnits(inst, DEFAULT_SUFFIX).filter('(ou=filler-0)')

    # Step 8: replication itself (not just the restored data) must still
    # work: dbmdb_reenable_backend_after_restore() re-enabled the mapping
    # tree node, so a fresh total update must succeed end to end again.
    _successful_total_update(inst)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
