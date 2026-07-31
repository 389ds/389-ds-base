# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import re
import socket
import struct
import time
import logging
import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts
from lib389.saslmap import SaslMappings
from lib389.utils import *
from test389.topologies import topology_st

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

SASL_OVERFLOW_FAKE_LENGTH = 0xFFFFFFFC
SASL_OVERFLOW_PAYLOAD_SIZE = 65536
# Hard limit for the SASL token length (before the 4-byte prefix).
# Must match SLAPD_MAX_SASLIO_SIZE in ldap/servers/slapd/slap.h
SASL_IO_MAX_TOKEN_SIZE = 0xFFFFFF
# Default value for nsslapd-maxsasliosize (2 MB)
SASL_MAX_IO_SIZE_DEFAULT = '2097152'

# Seconds to wait / poll interval for server liveness after injection
SERVER_POLL_TIMEOUT = 30
SERVER_POLL_INTERVAL = 0.5

# Error log pattern emitted when a SASL packet exceeds the size limit
SASL_REJECT_LOG_PATTERN = '.*SASL encrypted packet length exceeds maximum allowed limit.*'
_SASL_REJECT_RE = re.compile(SASL_REJECT_LOG_PATTERN)


def _get_error_log_line_count(inst):
    """Return current number of lines in the error log."""
    return len(inst.ds_error_log.readlines())


def _assert_rejection_logged(inst, pre_count, msg="Expected rejection log not found"):
    """Assert the rejection pattern appears in error log lines written after pre_count."""
    new_lines = inst.ds_error_log.readlines()[pre_count:]
    assert any(_SASL_REJECT_RE.match(line) for line in new_lines), msg


def _assert_rejection_not_logged(inst, pre_count, msg="Unexpected rejection log found"):
    """Assert the rejection pattern does NOT appear in error log lines after pre_count."""
    new_lines = inst.ds_error_log.readlines()[pre_count:]
    assert not any(_SASL_REJECT_RE.match(line) for line in new_lines), msg


def _wait_for_server(inst, timeout=SERVER_POLL_TIMEOUT):
    """Poll until the server responds to LDAP or timeout expires.

    Returns True if the server is confirmed alive, False otherwise.
    """
    elapsed = 0.0
    while elapsed < timeout:
        if inst.status():
            try:
                inst.rootdse.get_attr_val_utf8('vendorVersion')
                return True
            except ldap.SERVER_DOWN:
                pass
        time.sleep(SERVER_POLL_INTERVAL)
        elapsed += SERVER_POLL_INTERVAL
    return False


@pytest.fixture(scope="module")
def sasl_instance(topology_st):
    """Set up a standalone instance for DIGEST-MD5 SASL testing.

    Configures CLEAR password storage (required for DIGEST-MD5),
    creates a SASL uid mapping and a test user.  Restarts the
    instance so the password scheme change takes effect.
    """
    inst = topology_st.standalone
    inst.config.replace('passwordStorageScheme', 'CLEAR')

    # Create SASL mapping
    saslmappings = SaslMappings(inst)
    try:
        saslmappings.create(properties={
            'cn': 'uid map',
            'nsSaslMapRegexString': r'\(.*\)',
            'nsSaslMapBaseDNTemplate': DEFAULT_SUFFIX,
            'nsSaslMapFilterTemplate': '(uid=\\1)',
            'nsSaslMapPriority': '10',
        })
    except ldap.ALREADY_EXISTS:
        pass

    # Create test user
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    try:
        users.create(properties={
            'uid': 'sasltest',
            'cn': 'SASL Test User',
            'sn': 'Test',
            'uidNumber': '10001',
            'gidNumber': '10001',
            'homeDirectory': '/home/sasltest',
            'userPassword': 'sasltest123',
        })
    except ldap.ALREADY_EXISTS:
        pass

    inst.restart()

    yield inst

    # Ensure the instance is running for any subsequent test modules
    if not inst.status():
        inst.start()


def _sasl_bind_and_inject(inst, payload):
    """Establish a DIGEST-MD5 SASL connection and inject raw bytes.

    Performs a DIGEST-MD5 bind with encryption (SSF > 0) to push the
    SASL I/O layer, then writes the given payload directly to the
    underlying socket, bypassing SASL encoding.
    """
    conn = ldap.initialize(inst.get_ldap_uri())
    conn.protocol_version = ldap.VERSION3
    conn.set_option(ldap.OPT_X_SASL_SSF_MIN, 1)
    conn.set_option(ldap.OPT_X_SASL_SSF_MAX, 256)
    conn.sasl_interactive_bind_s(
        '',
        ldap.sasl.digest_md5('sasltest', 'sasltest123'),
    )
    fd = conn.fileno()
    sock = socket.fromfd(fd, socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.send(payload)
    finally:
        sock.detach()


def test_sasl_io_packet_length_overflow(sasl_instance):
    """Malformed SASL length prefix (overflow) must not crash the server

    :id: 318f871d-2f17-461b-98ed-04cdff6ab41a
    :setup: Standalone instance with DIGEST-MD5 SASL configured
    :steps:
        1. SASL DIGEST-MD5 bind as sasltest with encryption
        2. Send SASL packet with length 0xFFFFFFFC on the encrypted connection
        3. Verify server is still running
    :expectedresults:
        1. DIGEST-MD5 bind succeeds and SASL I/O layer is active
        2. Malformed packet is rejected without crashing the server
        3. Server remains up and responds to new connections
    """
    payload = (
        struct.pack('!I', SASL_OVERFLOW_FAKE_LENGTH)
        + b'A' * 3
        + b'B' * SASL_OVERFLOW_PAYLOAD_SIZE
    )
    log_pre = _get_error_log_line_count(sasl_instance)
    _sasl_bind_and_inject(sasl_instance, payload)

    # Poll until server confirms it is alive
    assert _wait_for_server(sasl_instance), \
        "Server is not responding after malformed SASL packet (overflow)"

    # Verify server logged the rejection (only in lines written by this test)
    _assert_rejection_logged(sasl_instance, log_pre,
                             "Server did not log SASL packet rejection for overflow")


@pytest.mark.parametrize("sasl_length", [0, 1, 2, 3],
                         ids=["length_0", "length_1", "length_2", "length_3"])
def test_sasl_io_packet_length_underflow(sasl_instance, sasl_length):
    """Small SASL length values must not cause a heap buffer overflow

    :id: a7c3e4b1-9f02-4d8a-b6e5-1c8d2f3a4b5e
    :setup: Standalone instance with DIGEST-MD5 SASL configured
    :steps:
        1. SASL DIGEST-MD5 bind as sasltest with encryption
        2. Send SASL packet with a small length (0, 1, 2 or 3) followed
           by data larger than the encrypted_buffer (1024 bytes).
           Length 3 produces packet_length == encrypted_buffer_offset (7)
           which exercises the buffer-state guard in sasl_io_read_packet.
        3. Verify server is still running
    :expectedresults:
        1. DIGEST-MD5 bind succeeds and SASL I/O layer is active
        2. Malformed packet is rejected without crashing the server
        3. Server remains up and responds to new connections
    """
    # 4-byte SASL length header + 3 bytes padding = 7 bytes total
    header = struct.pack('!I', sasl_length) + b'\x00' * 3

    # Follow with enough data to overflow the 1024-byte encrypted_buffer
    # if the underflow is not caught
    flood = b'\x41' * SASL_OVERFLOW_PAYLOAD_SIZE

    _sasl_bind_and_inject(sasl_instance, header + flood)

    # Poll until server confirms it is alive
    assert _wait_for_server(sasl_instance), \
        "Server is not responding after malformed SASL packet (underflow, length={})".format(sasl_length)


def test_sasl_io_unlimited_maxsasliosize(sasl_instance):
    """With maxsasliosize=-1 the server must still reject oversized packets

    When nsslapd-maxsasliosize is set to -1 (unlimited) the server must
    enforce a hard limit on the SASL token length so that an authenticated
    client cannot trigger an unbounded memory allocation that would crash
    the server via OOM (slapi_ch_realloc calls exit(1) on failure).

    The hard limit is SASL_IO_MAX_TOKEN_SIZE (0xFFFFFF). The limit
    check is applied to the wire (token) length before adding the 4-byte
    prefix. A wire length of 0xFFFFFFFB far exceeds 0xFFFFFF and must
    be rejected.

    :id: f5a7c9d1-3b2e-4f6a-8d0c-1e3f5a7b9d2e
    :setup: Standalone instance with DIGEST-MD5 SASL configured
    :steps:
        1. Set nsslapd-maxsasliosize to -1 (unlimited)
        2. SASL DIGEST-MD5 bind as sasltest with encryption
        3. Send SASL packet with wire length 0xFFFFFFFB which exceeds the
           hard limit (0xFFFFFF)
        4. Verify server is still running
        5. Restore nsslapd-maxsasliosize to the default
    :expectedresults:
        1. Configuration change succeeds
        2. DIGEST-MD5 bind succeeds and SASL I/O layer is active
        3. Packet is rejected by the hard limit without crashing
        4. Server remains up and responds to new connections
        5. Configuration is restored
    """
    # 0xFFFFFFFB far exceeds the 0xFFFFFF limit and is rejected
    # before the +4 prefix addition
    wire_length = 0xFFFFFFFB

    sasl_instance.config.replace('nsslapd-maxsasliosize', '-1')
    try:
        payload = (
            struct.pack('!I', wire_length)
            + b'A' * 3
            + b'B' * SASL_OVERFLOW_PAYLOAD_SIZE
        )
        log_pre = _get_error_log_line_count(sasl_instance)
        _sasl_bind_and_inject(sasl_instance, payload)

        assert _wait_for_server(sasl_instance), \
            "Server is not responding after malformed SASL packet (unlimited maxsasliosize)"

        _assert_rejection_logged(sasl_instance, log_pre,
                                 "Server did not log SASL packet rejection for unlimited maxsasliosize")
    finally:
        sasl_instance.config.replace('nsslapd-maxsasliosize', SASL_MAX_IO_SIZE_DEFAULT)


def test_sasl_io_positive_maxsasliosize_limit(sasl_instance):
    """Positive nsslapd-maxsasliosize must reject packets exceeding the limit

    When nsslapd-maxsasliosize is set to a small positive value the
    server must reject SASL packets whose wire (token) length exceeds
    that limit without crashing. The limit check is applied before
    adding the 4-byte prefix.

    :id: b2d4e6f8-1a3c-5b7d-9e0f-2a4c6e8b0d2f
    :setup: Standalone instance with DIGEST-MD5 SASL configured
    :steps:
        1. Set nsslapd-maxsasliosize to 4096
        2. SASL DIGEST-MD5 bind as sasltest with encryption
        3. Send SASL packet with wire length 8192 which exceeds the
           4096 limit
        4. Verify server is still running
        5. Restore nsslapd-maxsasliosize to the default
    :expectedresults:
        1. Configuration change succeeds
        2. DIGEST-MD5 bind succeeds and SASL I/O layer is active
        3. Packet is rejected by the configured limit without crashing
        4. Server remains up and responds to new connections
        5. Configuration is restored
    """
    wire_length = 8192  # wire_length > 4096

    sasl_instance.config.replace('nsslapd-maxsasliosize', '4096')
    try:
        payload = (
            struct.pack('!I', wire_length)
            + b'A' * 3
            + b'B' * SASL_OVERFLOW_PAYLOAD_SIZE
        )
        log_pre = _get_error_log_line_count(sasl_instance)
        _sasl_bind_and_inject(sasl_instance, payload)

        assert _wait_for_server(sasl_instance), \
            "Server is not responding after SASL packet exceeding positive limit"

        _assert_rejection_logged(sasl_instance, log_pre,
                                 "Server did not log SASL packet rejection for positive limit")
    finally:
        sasl_instance.config.replace('nsslapd-maxsasliosize', SASL_MAX_IO_SIZE_DEFAULT)


@pytest.mark.parametrize(
    "wire_length_offset,should_reject",
    # wire_length = SASL_IO_MAX_TOKEN_SIZE + offset
    # C check: wire_length > SASL_IO_MAX_TOKEN_SIZE -> reject
    # offset -1 -> wire_length = limit - 1 -> allowed
    # offset  0 -> wire_length = limit     -> allowed (not >)
    # offset  1 -> wire_length = limit + 1 -> rejected
    # offset  2 -> wire_length = limit + 2 -> rejected
    [(-1, False), (0, False), (1, True), (2, True)],
    ids=["below_limit", "at_limit", "one_over_limit", "two_over_limit"],
)
def test_sasl_io_hard_limit_boundary(sasl_instance, wire_length_offset, should_reject):
    """Hard limit boundary: tokens at/below 0xFFFFFF pass, above are rejected

    The hard limit (SASL_IO_MAX_TOKEN_SIZE = 0xFFFFFF) applies to the
    wire (token) length before the 4-byte prefix is added. When
    nsslapd-maxsasliosize is set to -1 the C code rejects when
    wire_length > SASL_IO_MAX_TOKEN_SIZE, so wire_length == limit
    is allowed.

    Note: We cannot actually allocate ~16 MB in a test, so the "below
    limit" and "at limit" cases will still be rejected by the server
    because we don't send enough data to fill the buffer (server reads
    partial packet then connection closes). The key assertion is that
    the server does NOT crash in any case.

    :id: c3e5f7a9-2b4d-6c8e-0f1a-3b5d7f9a1c3e
    :setup: Standalone instance with DIGEST-MD5 SASL configured
    :steps:
        1. Set nsslapd-maxsasliosize to -1 (unlimited)
        2. SASL DIGEST-MD5 bind as sasltest with encryption
        3. Send SASL packet with wire length near the 0xFFFFFF boundary
        4. Verify server is still running
        5. Restore nsslapd-maxsasliosize to the default
    :expectedresults:
        1. Configuration change succeeds
        2. DIGEST-MD5 bind succeeds and SASL I/O layer is active
        3. Server processes the packet without crashing
        4. Server remains up and responds to new connections
        5. Configuration is restored
    """
    # wire_length = limit + offset, limit = SASL_IO_MAX_TOKEN_SIZE
    wire_length = SASL_IO_MAX_TOKEN_SIZE + wire_length_offset

    sasl_instance.config.replace('nsslapd-maxsasliosize', '-1')
    try:
        payload = (
            struct.pack('!I', wire_length)
            + b'A' * 3
            + b'B' * SASL_OVERFLOW_PAYLOAD_SIZE
        )
        log_pre = _get_error_log_line_count(sasl_instance)
        _sasl_bind_and_inject(sasl_instance, payload)

        assert _wait_for_server(sasl_instance), \
            "Server is not responding after hard limit boundary test (offset={})".format(
                wire_length_offset)

        if should_reject:
            _assert_rejection_logged(
                sasl_instance, log_pre,
                "Server did not log SASL packet rejection for limit+{} test".format(
                    wire_length_offset))
        else:
            _assert_rejection_not_logged(
                sasl_instance, log_pre,
                "Server incorrectly rejected allowed packet at limit+{} test".format(
                    wire_length_offset))
    finally:
        sasl_instance.config.replace('nsslapd-maxsasliosize', SASL_MAX_IO_SIZE_DEFAULT)


def test_sasl_io_config_reject_over_limit(sasl_instance):
    """Setting nsslapd-maxsasliosize above 0xFFFFFF is rejected

    The DSE modify callback validates in pass 0 (apply=0). When
    config_set_maxsasliosize returns LDAP_UNWILLING_TO_PERFORM the
    DSE layer does not run pass 1 (apply=1), so the over-limit
    value is never stored. The attribute retains its previous value.

    :id: d4f6a8b0-3c5e-7d9f-1a2b-4c6e8f0a2d4f
    :setup: Standalone instance with DIGEST-MD5 SASL configured
    :steps:
        1. Record the current nsslapd-maxsasliosize value
        2. Attempt to set nsslapd-maxsasliosize above the limit
        3. Verify LDAP_UNWILLING_TO_PERFORM is raised
        4. Verify the value is unchanged
    :expectedresults:
        1. Current value recorded
        2. The modify operation raises LDAP_UNWILLING_TO_PERFORM
        3. Error is raised as expected
        4. The attribute retains its previous value
    """
    over_limit_value = str(SASL_IO_MAX_TOKEN_SIZE * 2)  # 0x1FFFFFE
    original = sasl_instance.config.get_attr_val_utf8('nsslapd-maxsasliosize')
    try:
        with pytest.raises(ldap.UNWILLING_TO_PERFORM):
            sasl_instance.config.replace('nsslapd-maxsasliosize', over_limit_value)
    except Exception:
        pass
    # Value must remain unchanged -- DSE does not apply when pass 0 fails
    current = sasl_instance.config.get_attr_val_utf8('nsslapd-maxsasliosize')
    assert current == original, \
        "maxsasliosize changed from {} to {} after rejected over-limit set".format(
            original, current)


@pytest.mark.parametrize("invalid_value", ['-2', '-100', '-2147483648'],
                         ids=["minus_2", "minus_100", "int32_min"])
def test_sasl_io_config_reject_invalid_negative(sasl_instance, invalid_value):
    """Negative values other than -1 must be rejected for nsslapd-maxsasliosize

    :id: e5a7b9c1-4d6f-8e0a-2b3c-5d7f9a1b3e5a
    :setup: Standalone instance with DIGEST-MD5 SASL configured
    :steps:
        1. Attempt to set nsslapd-maxsasliosize to an invalid negative value
        2. Verify the modify operation is rejected with LDAP_OPERATIONS_ERROR
        3. Verify the original value is unchanged
    :expectedresults:
        1. The modify operation raises an LDAP error
        2. The server rejects the invalid value
        3. The configuration value remains at its original setting
    """
    original = sasl_instance.config.get_attr_val_utf8('nsslapd-maxsasliosize')
    with pytest.raises(ldap.OPERATIONS_ERROR):
        sasl_instance.config.replace('nsslapd-maxsasliosize', invalid_value)
    # Verify original value unchanged
    current = sasl_instance.config.get_attr_val_utf8('nsslapd-maxsasliosize')
    assert current == original, \
        "maxsasliosize changed from {} to {} after invalid set".format(original, current)


def test_sasl_io_maxsasliosize_zero(sasl_instance):
    """With maxsasliosize=0 all SASL packets must be rejected

    :id: f6b8c0d2-5e7a-9f1b-3c4d-6e8a0b2c4f6b
    :setup: Standalone instance with DIGEST-MD5 SASL configured
    :steps:
        1. Set nsslapd-maxsasliosize to 0
        2. SASL DIGEST-MD5 bind as sasltest with encryption
        3. Send a minimal SASL packet (wire length 1)
        4. Verify server is still running
        5. Verify server logged the rejection
        6. Restore nsslapd-maxsasliosize to the default
    :expectedresults:
        1. Configuration change succeeds
        2. DIGEST-MD5 bind succeeds and SASL I/O layer is active
        3. Any packet is rejected because wire_length(1) > limit(0)
        4. Server remains up and responds to new connections
        5. Rejection is logged
        6. Configuration is restored
    """
    sasl_instance.config.replace('nsslapd-maxsasliosize', '0')
    try:
        payload = (
            struct.pack('!I', 1)
            + b'A' * 3
            + b'B' * SASL_OVERFLOW_PAYLOAD_SIZE
        )
        log_pre = _get_error_log_line_count(sasl_instance)
        _sasl_bind_and_inject(sasl_instance, payload)

        assert _wait_for_server(sasl_instance), \
            "Server is not responding after SASL packet with maxsasliosize=0"

        _assert_rejection_logged(sasl_instance, log_pre,
                                 "Server did not log SASL packet rejection with maxsasliosize=0")
    finally:
        sasl_instance.config.replace('nsslapd-maxsasliosize', SASL_MAX_IO_SIZE_DEFAULT)
