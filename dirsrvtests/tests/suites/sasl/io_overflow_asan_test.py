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

pytestmark = pytest.mark.tier1

PADDED_UNBIND_SIZE = 9000
STALL_PADDED_UNBIND_SIZE = 508
STALL_IOBLOCK_TIMEOUT_MS = 30000
STALL_CLIENT_TIMEOUT_SECONDS = 3
UNBIND_CONTROL_VALUE_SIZE = 512


def encode_ber_length(length):
    """Encode a non-negative BER definite length."""
    if length < 0x80:
        return bytes([length])

    encoded = length.to_bytes((length.bit_length() + 7) // 8, 'big')
    return bytes([0x80 | len(encoded)]) + encoded


def encode_ber_element(tag, value):
    """Encode a single-byte BER tag and its value."""
    return bytes([tag]) + encode_ber_length(len(value)) + value


def build_padded_unbind(padding_size=PADDED_UNBIND_SIZE):
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
    inner += b'A' * padding_size  # attacker-controlled padding

    # SEQUENCE (0x30) with 4-byte definite length encoding (0x84)
    length = len(inner)
    packet = b'\x30\x84' + struct.pack('>I', length) + inner
    return packet


def build_controlled_unbind():
    """Build a valid UNBIND with a noncritical control larger than 512 bytes."""
    control_oid = encode_ber_element(0x04, b'1.3.6.1.4.1.4203.666.11.999')
    control_value = encode_ber_element(0x04, b'V' * UNBIND_CONTROL_VALUE_SIZE)
    control = encode_ber_element(0x30, control_oid + control_value)
    controls = encode_ber_element(0xa0, control)

    message = b'\x02\x01\x01'  # msgid = 1
    message += b'\x42\x00'     # UNBIND
    message += controls
    return encode_ber_element(0x30, message)


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


def send_padded_unbind(fd, padding_size=PADDED_UNBIND_SIZE):
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
    packet = build_padded_unbind(padding_size)
    total_size = len(packet)

    log.info(f"[*] Sending padded UNBIND: {total_size} bytes total")
    log.info(f"[*]   Padding size: {padding_size} bytes of attacker-controlled data")
    log.info(f"[*]   Packet header: {packet[:10].hex()}")

    # Send directly on the raw FD, bypassing SASL wrapper
    with socket.fromfd(fd, socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.sendall(packet)
    log.info(f"[+] Sent {total_size} bytes on raw socket (bypassing SASL layer)")

    return total_size


def send_raw_packet(fd, packet):
    """Send a complete packet on the raw socket, bypassing the SASL layer."""
    with socket.fromfd(fd, socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.sendall(packet)


def wait_for_connection_close(fd):
    """Wait briefly for the server to close a raw client socket."""
    with socket.fromfd(fd, socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(STALL_CLIENT_TIMEOUT_SECONDS)
        try:
            data = sock.recv(1)
        except socket.timeout:
            pytest.fail(
                "Plaintext UNBIND stalled in sasl_io_recv instead of being "
                "drained or rejected"
            )

    assert data == b'', "Server did not close the connection after UNBIND"

def test_sasl_io_padded_unbind_does_not_stall(topo):
    """Verify a padded plaintext UNBIND does not stall the SASL I/O layer

    The LDAP connection buffer is 512 bytes. The padded UNBIND is 519 bytes,
    so sasl_io_recv must return it over multiple calls. The server must drain
    the already-buffered remainder or reject the PDU instead of waiting for
    more network data until nsslapd-ioblocktimeout expires.

    :id: ee28256e-5f97-4a19-8178-28d1e372695d
    :setup: Standalone Instance
    :steps:
        1. Configure a fixed 512-byte connection buffer and a 30-second I/O timeout
        2. Create a user and perform a SASL DIGEST-MD5 bind with SSF greater than zero
        3. Send a 519-byte plaintext UNBIND directly on the raw socket
        4. Wait up to 3 seconds for the server to close the connection
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. The connection closes without waiting for the server I/O timeout
    """

    inst = topo.standalone
    inst.config.set('passwordStorageScheme', 'CLEAR')
    inst.config.set('nsslapd-connection-buffer', '1')
    inst.config.set('nsslapd-ioblocktimeout', str(STALL_IOBLOCK_TIMEOUT_MS))

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create_test_user(uid=2)
    user.set('userPassword', PASSWORD)

    conn, fd = do_sasl_bind_and_get_fd(inst.host, inst.port, 'test_user_2')
    try:
        send_padded_unbind(fd, STALL_PADDED_UNBIND_SIZE)

        wait_for_connection_close(fd)
    finally:
        try:
            conn.unbind_s()
        except ldap.LDAPError:
            pass


def test_sasl_io_large_controlled_unbind_is_processed(topo):
    """Verify a valid plaintext UNBIND larger than the read buffer is processed

    LDAP permits controls on an UNBIND request. The SASL compatibility path must
    return such a request over multiple recv calls instead of rejecting it based
    on the size of the current connection read buffer.

    :id: a0496a4c-70e0-4d04-92d6-20f3977489fe
    :setup: Standalone Instance
    :steps:
        1. Configure a fixed 512-byte connection buffer and unbuffered access logging
        2. Create a user and perform a SASL DIGEST-MD5 bind with SSF greater than zero
        3. Send a valid plaintext UNBIND containing a large noncritical control
        4. Verify the connection closes and the server logs a processed UNBIND
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. The UNBIND is processed normally rather than rejected by the I/O layer
    """

    inst = topo.standalone
    inst.config.set('passwordStorageScheme', 'CLEAR')
    inst.config.set('nsslapd-connection-buffer', '1')
    inst.config.set('nsslapd-ioblocktimeout', str(STALL_IOBLOCK_TIMEOUT_MS))
    inst.config.set('nsslapd-accesslog-logbuffering', 'off')

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create_test_user(uid=3)
    user.set('userPassword', PASSWORD)

    conn, fd = do_sasl_bind_and_get_fd(inst.host, inst.port, 'test_user_3')
    unbind_count = len(inst.ds_access_log.match('.* UNBIND.*'))

    try:
        packet = build_controlled_unbind()
        assert len(packet) > 512
        send_raw_packet(fd, packet)
        wait_for_connection_close(fd)

        processed_unbinds = inst.ds_access_log.match('.* UNBIND.*')
        assert len(processed_unbinds) == unbind_count + 1, (
            "Large controlled UNBIND did not reach normal UNBIND processing"
        )
    finally:
        try:
            conn.unbind_s()
        except ldap.LDAPError:
            pass


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
    if not inst.has_asan():
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
    server_pid = inst.get_pid()

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

    try:
        wait_for_connection_close(fd)
    finally:
        try:
            conn.unbind_s()
        except ldap.LDAPError:
            pass

    assert inst.status(), "Server crashed while processing padded UNBIND"
    assert inst.get_pid() == server_pid, "Server restarted while processing padded UNBIND"

    # Check ASAN report
    log.info("[*] Checking ASAN report")
    overflow_detected = False
    try:
        overflow_detected = check_asan_report(inst, 'heap-buffer-overflow')
    except ValueError as e:
        log.info('No ASAN report found (expected when no overflow): %s', e)

    assert not overflow_detected, 'heap-buffer-overflow detected in ASAN report'

    log.info("[*] Test passed")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
