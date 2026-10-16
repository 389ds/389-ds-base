"""
   :Requirement: 389-ds-base: DataBase Import
"""

import time
import ldap
import pytest
from ldap.extop import ExtendedRequest
from lib389._constants import DEFAULT_SUFFIX
from lib389.replica import Replicas, BootstrapReplicationManager

# Shared by every bulk-import test below that drives the consumer side of a
# total update protocol session directly (see repl_extop.c's start/end
# decoders and repl5_total.c's entry decoder) rather than through a real
# supplier.
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

# A marker entry used by every "abort an online import, then confirm the
# pre-import backup was restored" test below, so a single well-known
# uuid/dn pair can be asserted on consistently across backends and test
# files.
MARKER_UUID = "55555555-55555555-55555555-55555555"
MARKER_DN = f"ou=pre-abort-marker,{DEFAULT_SUFFIX}"


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

    except ldap.SERVER_DOWN as exc:
        # Caller expects SERVER_DOWN in some cases
        raise exc
    except ldap.LDAPError as exc:
        # Other errors are unexpected
        raise AssertionError(f"consumer rejected entry {dn}: {exc}") from exc


def _send_suffix(conn):
    _send_entry(conn, SUFFIX_UUID, DEFAULT_SUFFIX, {
        'objectClass': ['top', 'domain'],
        'dc': ['example'],
    })


def _supplier_connection(inst):
    """Connection acting as the total update supplier.

    Only the entry extop lets the test choose nsuniqueid values (a plain
    add discards them), and exact uuid pairing matters to several of
    these tests. An explicit timeout keeps a synchronous call like
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


@pytest.fixture(scope="module")
def repl_consumer(topo):
    """Configure the instance as a replica the sessions below bind to.

    The replica configuration (like all of cn=config) lives in dse.ldif,
    not in any backend's own data, so it survives the backend
    wipes/restores the tests below perform and only needs to be created
    once per module."""
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
