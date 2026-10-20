# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Connection handling tests for the listener (ct-list) threads.

These cover behaviour that the optional epoll connection handling
(--enable-epoll) must share with the default poll build: closed connections
are released, idle connections time out, one client sending a partial request
does not stall other connections, and pipelined requests, also over LDAPS and
StartTLS and at max threads per connection, all get answered.
"""
import logging
import os
import select
import socket
import ssl
import time

import ldap
import pytest

from lib389 import pid_from_file
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

LDAP_TAG_SEARCH_RESULT_DONE = 0x65


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


def _root_dse_search_request(msgid):
    """BER encoded base search of the root DSE for objectClass=*"""
    body = (b"\x04\x00"                    # baseObject ""
            b"\x0a\x01\x00"                # scope baseObject
            b"\x0a\x01\x00"                # derefAliases never
            b"\x02\x01\x00"                # sizeLimit 0
            b"\x02\x01\x00"                # timeLimit 0
            b"\x01\x01\x00"                # typesOnly FALSE
            b"\x87\x0bobjectClass"         # filter (objectClass=*)
            b"\x30\x00")                   # attributes
    op = b"\x63" + _ber_len(len(body)) + body
    msg = b"\x02\x01" + bytes([msgid]) + op
    return b"\x30" + _ber_len(len(msg)) + msg


def _root_dse_search_request_64(msgid):
    """A 64 byte root DSE search: the server's read buffer (512 bytes, growing
    to 8192) always ends on a request boundary, so a read never leaves part
    of a request that would make the worker read again
    """
    attr = b"supportedSASLMechanisms"
    body = (b"\x04\x00\x0a\x01\x00\x0a\x01\x00\x02\x01\x00\x02\x01\x00\x01\x01\x00"
            b"\x87\x0bobjectClass"
            b"\x30" + _ber_len(len(attr) + 2) + b"\x04" + _ber_len(len(attr)) + attr)
    op = b"\x63" + _ber_len(len(body)) + body
    msg = b"\x02\x01" + bytes([msgid]) + op
    req = b"\x30" + _ber_len(len(msg)) + msg
    assert len(req) == 64
    return req


def _read_message(sock):
    """Read one LDAPMessage, return (msgid, protocol op tag)"""
    hdr = b""
    while len(hdr) < 2:
        chunk = sock.recv(2 - len(hdr))
        if not chunk:
            raise EOFError("connection closed")
        hdr += chunk
    length = hdr[1]
    if length & 0x80:
        nbytes = length & 0x7F
        raw = b""
        while len(raw) < nbytes:
            chunk = sock.recv(nbytes - len(raw))
            if not chunk:
                raise EOFError("connection closed")
            raw += chunk
        length = int.from_bytes(raw, "big")
    body = b""
    while len(body) < length:
        chunk = sock.recv(length - len(body))
        if not chunk:
            raise EOFError("connection closed")
        body += chunk
    # body: 02 01 <msgid> <op tag> ...
    return body[2], body[3]


def _current_connections(inst):
    return int(inst.monitor.get_attr_val_utf8("currentconnections"))


def _socket_count(inst):
    pid = pid_from_file(inst.ds_paths.pid_file)
    fd_dir = f"/proc/{pid}/fd"
    if not os.path.isdir(fd_dir):
        pytest.skip(f"{fd_dir} not available")
    count = 0
    for fd in os.listdir(fd_dir):
        try:
            if os.readlink(os.path.join(fd_dir, fd)).startswith("socket:"):
                count += 1
        except OSError:
            pass
    return count


def _ldap_conn(url, starttls=False):
    conn = ldap.initialize(url)
    conn.set_option(ldap.OPT_X_TLS_REQUIRE_CERT, ldap.OPT_X_TLS_NEVER)
    conn.set_option(ldap.OPT_X_TLS_NEWCTX, 0)
    if starttls:
        conn.start_tls_s()
    conn.simple_bind_s("", "")
    return conn


def _pipelined_root_dse_searches(conn, count):
    msgids = [conn.search("", ldap.SCOPE_BASE, "(objectClass=*)", ["dn"]) for _ in range(count)]
    for msgid in msgids:
        conn.result(msgid, all=1, timeout=30)


@pytest.fixture(scope="function")
def max_threads_per_conn_one(topo, request):
    """Set nsslapd-maxthreadsperconn to 1 for the duration of a test"""
    inst = topo.standalone
    orig = inst.config.get_attr_val_utf8("nsslapd-maxthreadsperconn")
    inst.config.replace("nsslapd-maxthreadsperconn", "1")
    inst.restart()

    def fin():
        inst.config.replace("nsslapd-maxthreadsperconn", orig)
        inst.restart()

    request.addfinalizer(fin)
    return inst


def test_closed_connections_released(topo):
    """Check that the server releases connections closed by the client

    :id: b45230d7-4855-48f1-8ec0-fa1453c3cbee
    :setup: Standalone instance
    :steps:
        1. Record the current connection count and the server's open sockets
        2. Open, use and close 300 connections
        3. Wait for the listener to release them
        4. Check the connection count and the open sockets again
    :expectedresults:
        1. Success
        2. Every search succeeds
        3. Success
        4. Both are back to their starting values
    """
    inst = topo.standalone
    url = f"ldap://{inst.host}:{inst.port}"
    conns_before = _current_connections(inst)
    sockets_before = _socket_count(inst)
    log.info(f"Before: currentconnections={conns_before} sockets={sockets_before}")

    for _ in range(300):
        conn = ldap.initialize(url)
        conn.search_s("", ldap.SCOPE_BASE, "(objectClass=*)", ["dn"])
        conn.unbind_s()

    deadline = time.time() + 10
    while time.time() < deadline:
        if _current_connections(inst) <= conns_before and _socket_count(inst) <= sockets_before:
            break
        time.sleep(0.5)
    conns_after = _current_connections(inst)
    sockets_after = _socket_count(inst)
    log.info(f"After: currentconnections={conns_after} sockets={sockets_after}")
    assert conns_after <= conns_before
    assert sockets_after <= sockets_before


def _unbind_request(msgid):
    return b"\x30\x05\x02\x01" + bytes([msgid]) + b"\x42\x00"


def _abandon_request(msgid, target):
    return b"\x30\x06\x02\x01" + bytes([msgid]) + b"\x50\x01" + bytes([target])


@pytest.mark.parametrize("close", ["unbind", "fin", "rst", "abandon", "bad_ber"])
def test_busy_connections_closed_released(topo, close):
    """Check that connections closed while the server is busy with them are released

    :id: 4ace8282-4bf1-4859-bb6d-9ea29d10b3fb
    :parametrized: yes
    :setup: Standalone instance
    :steps:
        1. Record the current connection count and the server's open sockets
        2. Open 500 connections and send 8 pipelined searches on each, so
           workers are busy with them when the client goes away
        3. Without reading the replies, close every connection: unbind then
           close, half close (FIN), reset (RST), abandon then unbind, or send
           a bad BER tag so the server closes it
        4. Repeat steps 2 and 3 three times
        5. Wait for the server to release the connections
        6. Check the connection count and open sockets, and that the server
           still answers
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Both are back to their starting values and a search succeeds
    """
    inst = topo.standalone
    conns_before = _current_connections(inst)
    sockets_before = _socket_count(inst)
    log.info(f"{close}: before currentconnections={conns_before} sockets={sockets_before}")

    for _ in range(3):
        socks = []
        for _ in range(500):
            s = socket.create_connection((inst.host, inst.port), timeout=10)
            socks.append(s)
        for s in socks:
            s.sendall(b"".join(_root_dse_search_request(m) for m in range(1, 9)))
        for s in socks:
            if close == "unbind":
                s.sendall(_unbind_request(9))
                s.shutdown(socket.SHUT_WR)
            elif close == "fin":
                s.shutdown(socket.SHUT_WR)
            elif close == "rst":
                s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b"\x01\x00\x00\x00\x00\x00\x00\x00")
            elif close == "abandon":
                s.sendall(_abandon_request(9, 8) + _unbind_request(10))
                s.shutdown(socket.SHUT_WR)
            elif close == "bad_ber":
                s.sendall(b"\x31\x03\x02\x01\x09")
        if close == "bad_ber":
            # The server must close these itself: read until EOF, then close
            open_socks = list(socks)
            deadline = time.time() + 30
            while open_socks and time.time() < deadline:
                readable, _, _ = select.select(open_socks, [], [], 1)
                for s in readable:
                    try:
                        data = s.recv(65536)
                    except ConnectionResetError:
                        data = b""
                    if not data:
                        open_socks.remove(s)
            assert not open_socks, f"server left {len(open_socks)} bad BER connections open"
        for s in socks:
            s.close()

    deadline = time.time() + 30
    while time.time() < deadline:
        if _current_connections(inst) <= conns_before and _socket_count(inst) <= sockets_before:
            break
        time.sleep(0.5)
    conns_after = _current_connections(inst)
    sockets_after = _socket_count(inst)
    log.info(f"{close}: after currentconnections={conns_after} sockets={sockets_after}")
    assert conns_after <= conns_before
    assert sockets_after <= sockets_before

    conn = ldap.initialize(f"ldap://{inst.host}:{inst.port}")
    conn.search_s("", ldap.SCOPE_BASE, "(objectClass=*)", ["dn"])
    conn.unbind_s()


def test_idle_timeout(topo):
    """Check that an idle connection is closed after nsslapd-idletimeout

    :id: 533a6eca-8b76-4e52-86f4-e48d03b6b34d
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-idletimeout to 5 seconds
        2. Open a connection and send nothing
        3. Wait for the server to close it
        4. Restore nsslapd-idletimeout
    :expectedresults:
        1. Success
        2. Success
        3. The server closes the connection within a few seconds of the timeout
        4. Success
    """
    inst = topo.standalone
    orig = inst.config.get_attr_val_utf8("nsslapd-idletimeout")
    inst.config.replace("nsslapd-idletimeout", "5")
    try:
        sock = socket.create_connection((inst.host, int(inst.port)), timeout=30)
        start = time.time()
        try:
            data = sock.recv(1)
        finally:
            sock.close()
        elapsed = time.time() - start
        log.info(f"Idle connection closed after {elapsed:.1f}s, recv={data!r}")
        assert data == b""
        assert 4 <= elapsed <= 15
    finally:
        inst.config.replace("nsslapd-idletimeout", orig)


def test_partial_request_does_not_stall_listener(topo, max_threads_per_conn_one):
    """Check that a client holding a partial request does not delay other connections

    :id: 63e4a9b1-5d08-4ed1-bdeb-99f7aa38e50b
    :setup: Standalone instance, nsslapd-maxthreadsperconn set to 1
    :steps:
        1. On one connection send a complete search followed by the first bytes of a second one
        2. For 6 seconds run searches on other connections
        3. Send the rest of the second search
        4. Read the responses on the first connection
    :expectedresults:
        1. Success
        2. Every search completes in well under the I/O block timeout
        3. Success
        4. Both searches are answered
    """
    inst = max_threads_per_conn_one
    url = f"ldap://{inst.host}:{inst.port}"
    second = _root_dse_search_request(2)

    sock = socket.create_connection((inst.host, int(inst.port)), timeout=30)
    try:
        sock.sendall(_root_dse_search_request(1) + second[:10])

        slowest = 0.0
        end = time.time() + 6
        while time.time() < end:
            start = time.time()
            conn = ldap.initialize(url)
            conn.search_s("", ldap.SCOPE_BASE, "(objectClass=*)", ["dn"])
            conn.unbind_s()
            slowest = max(slowest, time.time() - start)
            time.sleep(0.2)
        log.info(f"Slowest search on other connections: {slowest:.3f}s")
        assert slowest < 2.0

        sock.sendall(second[10:])
        done = set()
        while len(done) < 2:
            msgid, tag = _read_message(sock)
            if tag == LDAP_TAG_SEARCH_RESULT_DONE:
                done.add(msgid)
        assert done == {1, 2}
    finally:
        sock.close()


def test_pipelined_requests(topo):
    """Check that many outstanding requests on one connection are all answered

    :id: 64f4b3cc-2d17-4d34-93b3-4ce88be635cd
    :setup: Standalone instance
    :steps:
        1. Send 200 searches on one connection without waiting, then read all results
        2. Repeat 20 bursts of 25 pipelined searches
    :expectedresults:
        1. All 200 searches are answered
        2. All searches are answered
    """
    inst = topo.standalone
    conn = _ldap_conn(f"ldap://{inst.host}:{inst.port}")
    try:
        _pipelined_root_dse_searches(conn, 200)
        for _ in range(20):
            _pipelined_root_dse_searches(conn, 25)
    finally:
        conn.unbind_s()


def test_pipelined_requests_max_threads(topo, max_threads_per_conn_one):
    """Check pipelined requests are answered when the connection is at max threads

    :id: 3d454e99-f7f8-47c0-b616-9fa043f8fbb9
    :setup: Standalone instance, nsslapd-maxthreadsperconn set to 1
    :steps:
        1. Send 200 searches on one connection without waiting, then read all results
        2. Repeat 20 bursts of 25 pipelined searches
    :expectedresults:
        1. All 200 searches are answered
        2. All searches are answered
    """
    inst = max_threads_per_conn_one
    conn = _ldap_conn(f"ldap://{inst.host}:{inst.port}")
    try:
        _pipelined_root_dse_searches(conn, 200)
        for _ in range(20):
            _pipelined_root_dse_searches(conn, 25)
    finally:
        conn.unbind_s()


def _start_tls_request(msgid):
    oid = b"1.3.6.1.4.1.1466.20037"
    op = b"\x77" + _ber_len(len(oid) + 2) + b"\x80" + _ber_len(len(oid)) + oid
    msg = b"\x02\x01" + bytes([msgid]) + op
    return b"\x30" + _ber_len(len(msg)) + msg


def _tls_socket(inst, mode):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    if mode == "ldaps":
        raw = socket.create_connection((inst.host, inst.sslport), timeout=30)
    else:
        raw = socket.create_connection((inst.host, inst.port), timeout=30)
        raw.sendall(_start_tls_request(1))
        msgid, tag = _read_message(raw)
        assert (msgid, tag) == (1, 0x78), "StartTLS was not answered"
    return ctx.wrap_socket(raw, server_hostname=inst.host)


@pytest.mark.parametrize("max_threads", ["default", "one"])
@pytest.mark.parametrize("mode", ["ldaps", "starttls"])
def test_tls_requests_buffered_in_one_record(topo, request, mode, max_threads):
    """Check requests that arrive in one TLS record are all answered

    :id: 7c1f0a52-3e8b-4d6a-9f27-5b0e8d4c2a61
    :parametrized: yes
    :setup: Standalone instance with TLS enabled
    :steps:
        1. Connect with LDAPS or StartTLS, optionally with
           nsslapd-maxthreadsperconn set to 1
        2. Turn turbo mode off: a turbo worker polls through NSS, which
           reports the buffered data, so it would hide the problem
        3. Send 250 searches of 64 bytes in a single TLS write: one record,
           larger than the server's connection read buffer, and every read
           ends on a request boundary, so the rest waits in the TLS layer
           while the socket has nothing to read
        4. Send nothing more and read the results
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. All 250 searches are answered
    """
    inst = topo.standalone
    if not inst.config.get_attr_val_utf8("nsslapd-securePort") or \
       inst.config.get_attr_val_utf8("nsslapd-security") != "on":
        inst.enable_tls()
    if max_threads == "one":
        request.getfixturevalue("max_threads_per_conn_one")
    turbo = inst.config.get_attr_val_utf8("nsslapd-enable-turbo-mode")
    inst.config.replace("nsslapd-enable-turbo-mode", "off")
    request.addfinalizer(lambda: inst.config.replace("nsslapd-enable-turbo-mode", turbo))
    sock = _tls_socket(inst, mode)
    try:
        # One-byte message ids, reused: 1..127 then again (StartTLS used 1)
        msgids = range(1, 251)
        data = b"".join(_root_dse_search_request_64(m % 127 + 1) for m in msgids)
        assert 8192 < len(data) <= 16384
        sock.sendall(data)
        done = 0
        while done < len(msgids):
            _, tag = _read_message(sock)
            if tag == LDAP_TAG_SEARCH_RESULT_DONE:
                done += 1
        assert done == 250
    finally:
        sock.close()


@pytest.mark.parametrize("mode", ["ldaps", "starttls"])
def test_pipelined_requests_tls(topo, mode):
    """Check that pipelined requests over TLS are all answered

    :id: 2b5f6f66-f664-46f9-b69d-6a6a5c3b092c
    :parametrized: yes
    :setup: Standalone instance with TLS enabled
    :steps:
        1. Connect with LDAPS or StartTLS
        2. Send 200 searches without waiting, then read all results
        3. Repeat 20 bursts of 25 pipelined searches
    :expectedresults:
        1. Success
        2. All 200 searches are answered
        3. All searches are answered
    """
    inst = topo.standalone
    if not inst.config.get_attr_val_utf8("nsslapd-securePort") or \
       inst.config.get_attr_val_utf8("nsslapd-security") != "on":
        inst.enable_tls()
    if mode == "ldaps":
        conn = _ldap_conn(f"ldaps://{inst.host}:{inst.sslport}")
    else:
        conn = _ldap_conn(f"ldap://{inst.host}:{inst.port}", starttls=True)
    try:
        _pipelined_root_dse_searches(conn, 200)
        for _ in range(20):
            _pipelined_root_dse_searches(conn, 25)
    finally:
        conn.unbind_s()
