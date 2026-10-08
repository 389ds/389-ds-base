# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Regression tests for StartTLS read-buffer handling (CVE-2026-86345)."""

import logging
import os
import socket

import pytest
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

START_TLS_OID = b"1.3.6.1.4.1.1466.20037"
LDAP_SUCCESS = 0
LDAP_TAG_BIND_RESPONSE = 0x61


def _ber_len(n):
    if n < 0x80:
        return bytes([n])
    out = []
    v = n
    while v:
        out.append(v & 0xFF)
        v >>= 8
    out.reverse()
    return bytes([0x80 | len(out)]) + bytes(out)


def _ber_tlv(tag, val):
    if isinstance(val, int):
        if val == 0:
            raw = b"\x00"
        else:
            raw = b""
            v = val
            while v:
                raw = bytes([v & 0xFF]) + raw
                v >>= 8
            if raw[0] & 0x80:
                raw = b"\x00" + raw
        val = raw
    return bytes([tag]) + _ber_len(len(val)) + val


def _ldap_message(msgid, protocol_op):
    return _ber_tlv(0x30, _ber_tlv(0x02, msgid) + protocol_op)


def _start_tls_request(msgid=1):
    return _ldap_message(msgid, _ber_tlv(0x77, _ber_tlv(0x80, START_TLS_OID)))


def _simple_bind_request(msgid, bind_dn="", password=""):
    body = (
        _ber_tlv(0x02, 3)
        + _ber_tlv(0x04, bind_dn.encode("utf-8"))
        + _ber_tlv(0x80, password.encode("utf-8"))
    )
    return _ldap_message(msgid, _ber_tlv(0x60, body))


def _recv_exact(sock, n, timeout=5.0):
    sock.settimeout(timeout)
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("EOF reading %d bytes (got %d)" % (n, len(buf)))
        buf += chunk
    return buf


def _read_ber_element(sock, timeout=5.0):
    hdr = _recv_exact(sock, 2, timeout)
    tag = hdr[0]
    first = hdr[1]
    if first & 0x80:
        nlen = first & 0x7F
        if nlen == 0:
            raise ValueError("indefinite BER length")
        len_bytes = _recv_exact(sock, nlen, timeout)
        length = int.from_bytes(len_bytes, "big")
        rest_hdr = bytes([first]) + len_bytes
    else:
        length = first
        rest_hdr = bytes([first])
    value = _recv_exact(sock, length, timeout)
    return bytes([tag]) + rest_hdr + value


def _parse_ldap_message(raw):
    if raw[0] != 0x30:
        raise ValueError("not a SEQUENCE")
    i = 1
    first = raw[i]
    i += 1
    if first & 0x80:
        i += first & 0x7F
    if raw[i] != 0x02:
        raise ValueError("expected msgid INTEGER")
    i += 1
    mlen = raw[i]
    i += 1
    msgid = int.from_bytes(raw[i:i + mlen], "big")
    i += mlen
    return msgid, raw[i], raw[i:]


def _parse_result_code(protocol_op):
    i = 1
    first = protocol_op[i]
    i += 1
    if first & 0x80:
        i += first & 0x7F
    if protocol_op[i] != 0x0A:
        idx = protocol_op.find(b"\x0a\x01")
        if idx < 0:
            raise ValueError("no resultCode")
        return protocol_op[idx + 2]
    i += 1
    rlen = protocol_op[i]
    i += 1
    return int.from_bytes(protocol_op[i:i + rlen], "big")


def test_starttls_rejects_smuggled_bind(topo):
    """StartTLS must not execute a second LDAP PDU from the same TCP read

    :id: 8273e43a-0453-4518-892e-ae1fbc071f42
    :setup: Standalone instance with TLS enabled
    :steps:
        1. Enable TLS on the instance
        2. Open a plaintext LDAP socket and send StartTLS plus an anonymous
           BindRequest in a single TCP write
        3. Collect cleartext LDAP responses (if any) before the connection closes
        4. Check the errors log for the StartTLS leftover-buffer rejection
    :expectedresults:
        1. TLS is enabled
        2. The write completes
        3. No successful BindResponse is returned for the smuggled bind msgid
        4. The server logged rejection of unexpected data with StartTLS
    """
    inst = topo.standalone
    log.info("Enabling TLS")
    inst.enable_tls()

    host = inst.host
    port = int(inst.port)
    smuggle_msgid = 2
    payload = _start_tls_request(1) + _simple_bind_request(smuggle_msgid)

    log.info("Sending StartTLS + BindRequest (%d bytes) to %s:%d",
             len(payload), host, port)
    sock = socket.create_connection((host, port), timeout=10)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.sendall(payload)

    bind_success = False
    responses = []
    try:
        for _ in range(8):
            try:
                raw = _read_ber_element(sock, timeout=3.0)
            except (socket.timeout, TimeoutError, EOFError, OSError) as exc:
                log.info("Stopped reading cleartext responses: %s", exc)
                break
            msgid, tag, pop = _parse_ldap_message(raw)
            rc = _parse_result_code(pop)
            responses.append((msgid, tag, rc))
            log.info("Cleartext response msgid=%d tag=0x%02x result=%d",
                     msgid, tag, rc)
            if msgid == smuggle_msgid and tag == LDAP_TAG_BIND_RESPONSE and rc == LDAP_SUCCESS:
                bind_success = True
                break
    finally:
        try:
            sock.close()
        except OSError:
            pass

    assert not bind_success, (
        "Smuggled BindResponse success was delivered; "
        "responses=%r" % (responses,)
    )

    # Fixed servers reject StartTLS when leftover plaintext remains.
    assert inst.ds_error_log.match(
        ".*Rejecting StartTLS.*unexpected data.*"
    ), "Expected StartTLS leftover-buffer rejection in the errors log"


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
