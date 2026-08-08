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
from lib389.tombstone import Tombstones
from lib389.utils import get_default_db_lib
from test389.topologies import topology_st as topo

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(get_default_db_lib() != "mdb",
                                 reason="Exercises the LMDB bulk import consumer")]

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

REPL_MANAGER_PW = 'replmanagerpw'
FAKE_SUPPLIER_RID = 400

SUFFIX_UUID = "aaaaaaaa-aaaaaaaa-aaaaaaaa-aaaaaaaa"
ANCESTOR_UUID = "bbbbbbbb-bbbbbbbb-bbbbbbbb-bbbbbbbb"
PARENT_UUID = "11111111-11111111-11111111-11111111"
CHILD_UUID = "22222222-22222222-22222222-22222222"

ANCESTOR_DN = f"ou=init-ancestor,{DEFAULT_SUFFIX}"
PARENT_DN = f"ou=init-parent,{ANCESTOR_DN}"
TOMBSTONE_DN = f"nsuniqueid={CHILD_UUID},uid=init-orphan,{PARENT_DN}"


# Minimal DER encoders for the total update payloads.  The grammar mirrors
# the consumer's decoders: decode_startrepl_extop()/decode_endrepl_extop()
# in repl_extop.c and decode_total_update_extop()/my_ber_scanf_attr()/
# my_ber_scanf_value() in repl5_total.c.
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


def test_lmdb_bulk_import_tombstone_released_by_live_parent(topo):
    """A tombstone processed before its live parent must not be lost

    :id: 33dc80a2-28b7-4571-9cda-18c93f855c3a
    :setup: Standalone instance using the LMDB backend
    :steps:
        1. Configure a replica and a replication manager on the instance
        2. Start a total update session and send the suffix entry
        3. Send a parent whose own parent has not been sent yet
        4. Send a tombstone whose nsparentuniqueid names that parent
        5. Send more entries than the bulk queue can hold
        6. Send the missing ancestor, then finish the session
        7. Search for the parent and for the tombstone
    :expectedresults:
        1. Success
        2. The consumer answers REPLICA_READY
        3. The parent parks on the waiting queue, unregistered
        4. The tombstone parks, keyed by the parent's nsuniqueid
        5. The tombstone's batch is drained before the ancestor arrives
        6. The ancestor releases the parent, the parent releases the
           tombstone, and the session ends successfully
        7. Both entries were imported
    """
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

    # Hand-driven supplier side of a total update.  Only this path can
    # exercise the bug: the entry extop carries the uniqueid in its
    # payload, while a plain add discards client-supplied nsuniqueid
    # (NO-USER-MODIFICATION), so no add-based test can pair the
    # tombstone's nsparentuniqueid with its parent's uuid.
    supplier = ldap.initialize(f"ldap://{inst.host}:{inst.port}")
    supplier.simple_bind_s(mgr.dn, REPL_MANAGER_PW)

    started = False
    try:
        _, raw = supplier.extop_s(ExtendedRequest(START_OID, _start_payload()))
        code = _response_code(raw)
        assert code == REPLICA_READY, f"start response {code}"
        started = True

        _send_entry(supplier, SUFFIX_UUID, DEFAULT_SUFFIX, {
            'objectClass': ['top', 'domain'],
            'dc': ['example'],
        })
        # The parent chain: init-parent waits for init-ancestor, which is
        # sent last, so init-parent stays unregistered while the tombstone
        # below is processed -- deterministically, whatever the consumer's
        # batching does.
        _send_entry(supplier, PARENT_UUID, PARENT_DN, {
            'objectClass': ['top', 'organizationalUnit'],
            'ou': ['init-parent'],
        })
        # Parks keyed by PARENT_UUID -- the comparison that regressed
        # in 4b82bf6d3.
        _send_entry(supplier, CHILD_UUID, TOMBSTONE_DN, {
            'objectClass': ['top', 'person', 'extensibleObject', 'nsTombstone'],
            'uid': ['init-orphan'],
            'cn': ['init-orphan'],
            'sn': ['init-orphan'],
            'nsparentuniqueid': [PARENT_UUID],
        })
        # The bulk queue caps at 64 entries, so 65 fillers force the
        # tombstone's batch to be drained before the ancestor can share
        # a (LIFO-processed) batch with it.
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

    # A stranded tombstone is freed silently on the unfixed server; name
    # the cause before the assertions.
    for pattern in ('.*was never imported.*', '.*Aborting bulk import.*'):
        for line in inst.ds_error_log.match(pattern):
            log.info("error log: %s", line.strip())

    assert OrganizationalUnits(inst, ANCESTOR_DN).get('init-parent') is not None
    found = Tombstones(inst, DEFAULT_SUFFIX).filter(f'(nsUniqueId={CHILD_UUID})')
    log.info("%d matching tombstones found after total update", len(found))
    assert len(found) == 1


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
