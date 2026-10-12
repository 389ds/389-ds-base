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
import socket
import time

import pytest

from lib389.rootdse import RootDSE
from test389.topologies import topology_st

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

CLOSING_ERR = ".*connection_acquire_nolock_ext.*Attempt to acquire connection in the closing state.*"


def _ber_len(n):
    if n < 0x80:
        return bytes([n])
    out = []
    while n:
        out.append(n & 0xFF)
        n >>= 8
    out.reverse()
    return bytes([0x80 | len(out)]) + bytes(out)


def _root_dse_search_request(msgid):
    """BER encoded base search of the root DSE for objectClass=*, all attributes"""
    body = (b"\x04\x00\x0a\x01\x00\x0a\x01\x00\x02\x01\x00\x02\x01\x00\x01\x01\x00"
            b"\x87\x0bobjectClass\x30\x00")
    op = b"\x63" + _ber_len(len(body)) + body
    msg = b"\x02\x01" + bytes([msgid]) + op
    return b"\x30" + _ber_len(len(msg)) + msg


def test_pipelined_requests_on_closed_connection(topology_st):
    """Check a client that pipelines requests and resets the connection
    does not make the server log errors

    :id: 5d0c8b71-2f6e-4a39-9c84-1e7b3a2f6d90
    :setup: Standalone instance
    :steps:
        1. Open 200 connections with a small receive buffer and send 32
           pipelined root DSE searches on each
        2. Reset every connection (SO_LINGER 0) without reading the replies, so
           the server fails to send a reply while requests are still buffered
        3. Repeat 10 times, then wait for the server to release the connections
        4. Check the errors log and that the server still answers
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. No "Attempt to acquire connection in the closing state" error is
           logged, and a search succeeds
    """
    inst = topology_st.standalone
    before = len(inst.ds_error_log.match(CLOSING_ERR))

    for _ in range(10):
        socks = []
        for _ in range(200):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            # A small receive window makes the server's replies back up, so its
            # writes fail while requests are still buffered
            s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            s.settimeout(10)
            s.connect((inst.host, inst.port))
            socks.append(s)
        for s in socks:
            s.sendall(b"".join(_root_dse_search_request(m) for m in range(1, 33)))
        for s in socks:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b"\x01\x00\x00\x00\x00\x00\x00\x00")
            s.close()
    time.sleep(5)

    after = len(inst.ds_error_log.match(CLOSING_ERR))
    log.info(f"closing-state errors before={before} after={after}")
    assert after == before

    assert RootDSE(inst).get_attr_val_utf8("vendorVersion")
