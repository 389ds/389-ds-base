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
import re
import time
import socket
import pytest
from lib389._constants import *
from test389.topologies import topology_st

pytestmark = pytest.mark.tier1
DEBUGGING = os.getenv("DEBUGGING", default=False)
logging.getLogger(__name__).setLevel(logging.DEBUG if DEBUGGING else logging.INFO)
log = logging.getLogger(__name__)


def _recv_exact(sock, count):
    data = bytearray()
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        assert chunk, "LDAP connection closed while reading a response"
        data.extend(chunk)
    return bytes(data)


def _recv_ldap_message(sock):
    header = _recv_exact(sock, 2)
    assert header[0] == 0x30
    length = header[1]
    if length & 0x80:
        length = int.from_bytes(_recv_exact(sock, length & 0x7f), "big")
    body = _recv_exact(sock, length)
    assert body[0] == 0x02
    id_length = body[1]
    msgid = int.from_bytes(body[2:2 + id_length], "big")
    op_offset = 2 + id_length
    tag = body[op_offset]
    op_length = body[op_offset + 1]
    op_offset += 2
    if op_length & 0x80:
        length_bytes = op_length & 0x7f
        op_length = int.from_bytes(body[op_offset:op_offset + length_bytes], "big")
        op_offset += length_bytes
    return msgid, tag, body[op_offset:op_offset + op_length]


def _recv_search_done(sock, expected_msgid):
    while True:
        msgid, tag, payload = _recv_ldap_message(sock)
        assert msgid == expected_msgid
        assert tag in (0x64, 0x65), "unexpected LDAP response tag: {}".format(tag)
        if tag == 0x65:
            assert payload.startswith(b"\x0a\x01\x00"), "Search result was not LDAP success"
            return


def _wait_for_error_log(inst, offset, pattern, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with open(inst.errlog, "r", errors="replace") as errlog:
            errlog.seek(offset)
            match = re.search(pattern, errlog.read())
        if match:
            return match
        time.sleep(0.02)
    return None


def test_dos_partial_message(topology_st):
    """Verify that a partial LDAP message does not block previous operations on the same connection.

    This exercises the race deterministically instead of racing a wall-clock
    timeout: nsslapd-ioblocktimeout is pinned to a known, short value, and we
    wait on the error log for proof that the server actually queued the
    buffered second request ("more_data") before measuring how fast the
    first Bind response arrives. This avoids CI-load-dependent flakiness
    while still failing clearly (and quickly) if the connection mutex is
    ever held across the second worker's blocking read again.

    :id: 3c59bb2a-0f77-477e-aa1c-de3aa8a0fa80
    :setup: Standalone instance with connection logging and a short, pinned
        ioblocktimeout
    :steps:
        1. Connect to the server.
        2. Send a complete Anonymous Bind request followed by a partial second message.
        3. Wait for the error log to confirm the second (partial) message was buffered/queued.
        4. Verify that the Bind response for the first message is received promptly.
        5. Verify that other connections can still be established and used.
    :expectedresults:
        1. Connection established.
        2. Data sent.
        3. The server logs that it queued the connection due to buffered data.
        4. Bind response received well within ioblocktimeout.
        5. Server remains responsive.
    """
    inst = topology_st.standalone
    original_loglevel = inst.config.get_attr_val_utf8("nsslapd-errorlog-level")
    original_ioblocktimeout = inst.config.get_attr_val_utf8("nsslapd-ioblocktimeout")
    # Short enough that a regression (worker blocked for the full timeout)
    # fails fast, long enough that CI scheduling jitter can't trip it.
    test_ioblocktimeout_ms = 5000
    s = None
    try:
        inst.config.loglevel([ErrorLog.CONNECT, ErrorLog.DEFAULT])
        inst.config.replace("nsslapd-ioblocktimeout", str(test_ioblocktimeout_ms))
        inst.restart()

        # Anonymous Bind (MsgID=1): 30 0c 02 01 01 60 07 02 01 03 04 00 80 00
        bind_req = b'\x30\x0c\x02\x01\x01\x60\x07\x02\x01\x03\x04\x00\x80\x00'
        # Partial Bind (MsgID=2): 30 0c 02 01 02 ... (stops here)
        partial_req = b'\x30\x0c\x02\x01\x02'

        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((inst.host, inst.port))
        # Generous safety-net timeout: a true hang (bug reintroduced) should
        # approach the full ioblocktimeout, so this only needs enough margin
        # over that worst case to avoid masking a real failure as an error.
        s.settimeout((test_ioblocktimeout_ms / 1000.0) + 5)

        log_offset = os.path.getsize(inst.errlog)
        log.info("Sending full bind request + partial second message")
        s.sendall(bind_req + partial_req)

        # Confirm the race window is actually open: the server must have
        # buffered the partial second message and queued the connection
        # before we start timing the response.
        queued = _wait_for_error_log(
            inst, log_offset, r"conn \d+ queued because more_data", 5)
        assert queued is not None, "the partial second message was not buffered/queued"

        start = time.monotonic()
        log.info("Waiting for bind response...")
        try:
            data = s.recv(1024)
        except socket.timeout:
            pytest.fail("Timed out waiting for bind response. The server might be deadlocked.")
        elapsed = time.monotonic() - start
        log.info(f"Bind response received after {elapsed:.2f}s")
        log.debug(f"Received data: {data.hex()}")

        # Bind response for MsgID=1: 30 0c 02 01 01 61 07 0a 01 00 04 00 04 00
        assert b'\x02\x01\x01\x61' in data
        # The real regression signature is the response taking close to the
        # full ioblocktimeout (because a second worker held c_mutex while
        # blocked reading the fragment). A healthy server replies almost
        # immediately, so a generous fraction of the timeout still leaves
        # plenty of margin above normal scheduling noise.
        assert elapsed < (test_ioblocktimeout_ms / 1000.0) / 2, (
            f"Bind response took {elapsed:.2f}s - the server may be blocked "
            f"behind the partial second LDAP message"
        )
        log.info("Successfully received a prompt bind response for the first operation")
    finally:
        if s is not None:
            s.close()
        inst.config.replace("nsslapd-errorlog-level", original_loglevel)
        inst.config.replace("nsslapd-ioblocktimeout", original_ioblocktimeout)
        inst.restart()

    # Verify server is still responsive
    log.info("Verifying server responsiveness")
    inst.open()
    assert "dn: cn=directory manager" == inst.whoami_s()


def test_pipelined_partial_request_has_one_reader(topology_st):
    """A queued worker must not start reading a PDU already owned by another worker.

    :id: 357d2ba4-ae82-4da4-9b8b-f97de9653c26
    :setup: Standalone instance with two worker threads and connection logging
    :steps:
        1. Park one worker on an incomplete request on a separate connection.
        2. Send a complete Search request and the first five bytes of a second Search on one connection.
        3. Receive the first Search result and release the parked worker while the second request is incomplete.
        4. Finish the second Search request and receive its result.
    :expectedresults:
        1. The parked worker waits for the remaining bytes.
        2. The server queues the buffered second request.
        3. The queued worker does not try to read the incomplete PDU concurrently.
        4. The second Search succeeds without disconnecting the connection.
    """
    inst = topology_st.standalone
    original_threads = inst.config.get_attr_val_utf8("nsslapd-threadnumber")
    original_loglevel = inst.config.get_attr_val_utf8("nsslapd-errorlog-level")
    original_ioblocktimeout = inst.config.get_attr_val_utf8("nsslapd-ioblocktimeout")
    target = None
    parked = None
    try:
        inst.config.replace("nsslapd-threadnumber", "2")
        inst.config.replace("nsslapd-ioblocktimeout", "30000")
        inst.config.loglevel([ErrorLog.CONNECT, ErrorLog.DEFAULT])
        inst.restart()

        target = socket.create_connection((inst.host, inst.port), timeout=5)
        target.settimeout(5)
        # Complete an initial Bind before sending the pipelined Search requests.
        target.sendall(bytes.fromhex("30 0c 02 01 01 60 07 02 01 03 04 00 80 00"))
        assert _recv_ldap_message(target)[:2] == (1, 0x61)

        parked = socket.create_connection((inst.host, inst.port), timeout=5)
        parked.settimeout(5)
        # This valid LDAPMessage prefix keeps one worker waiting for its body.
        parked_offset = os.path.getsize(inst.errlog)
        parked.sendall(bytes.fromhex("30 0c 02 01 01"))
        parked_read = _wait_for_error_log(
            inst, parked_offset, r"connection \d+ read 5 bytes", 5)
        assert parked_read is not None, "the parked worker did not read its partial request"
        time.sleep(0.25)

        search = (bytes.fromhex("30 25 02 01 02 63 20 04 00 0a 01 00 0a 01 00 "
                                "02 01 00 02 01 00 01 01 00 87 0b") +
                  b"objectClass" + bytes.fromhex("30 00"))
        search2 = search[:4] + b"\x03" + search[5:]
        log_offset = os.path.getsize(inst.errlog)
        target.sendall(search + search2[:5])

        queued = _wait_for_error_log(inst, log_offset,
                                     r"conn (\d+) queued because more_data", 5)
        assert queued is not None, "the second Search was not queued from buffered data"
        conn_id = queued.group(1)
        _recv_search_done(target, 2)
        # Allow the first worker to enter the poll for the second Search body.
        time.sleep(0.25)

        parked.close()
        parked = None
        duplicate_read = _wait_for_error_log(
            inst, log_offset,
            r"conn {} read not ready due to 4\b".format(conn_id), 2)

        target.sendall(search2[5:])
        _recv_search_done(target, 3)
        assert duplicate_read is None, (
            "a second worker read the connection while the first was assembling "
            "the fragmented Search request: {}".format(duplicate_read.group(0)))
    finally:
        if parked is not None:
            parked.close()
        if target is not None:
            target.close()
        inst.config.replace("nsslapd-threadnumber", original_threads)
        inst.config.replace("nsslapd-errorlog-level", original_loglevel)
        inst.config.replace("nsslapd-ioblocktimeout", original_ioblocktimeout)
        inst.restart()
