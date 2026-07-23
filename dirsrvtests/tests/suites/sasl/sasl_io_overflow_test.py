# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import socket
import struct
import time
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

def test_sasl_io_packet_length_overflow(topology_st):
    """Malformed SASL length prefix must not crash the server

    :id: 318f871d-2f17-461b-98ed-04cdff6ab41a
    :setup: Standalone instance
    :steps:
        1. Set passwordStorageScheme to CLEAR and restart the instance
        2. Add SASL uid mapping and user sasltest for DIGEST-MD5 bind
        3. SASL DIGEST-MD5 bind as sasltest
        4. Send malformed SASL packeton the encrypted connection
        5. Verify server is still running
    :expectedresults:
        1. CLEAR scheme and SASL map/user are configured successfully
        2. Test user added
        3. DIGEST-MD5 bind succeeds
        4. Malformed packet is accepted on the wire without crashing the server
        5. Server remains up
    """
    inst = topology_st.standalone
    inst.config.replace('passwordStorageScheme', 'CLEAR')
    saslmappings = SaslMappings(inst)

    # Create SASL mapping
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

    try:
        # Open connection to server and send bad payload
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
        payload = (
            struct.pack('!I', SASL_OVERFLOW_FAKE_LENGTH)
            + b'A' * 3
            + b'B' * SASL_OVERFLOW_PAYLOAD_SIZE
        )
        sock.send(payload)
        sock.detach()
        time.sleep(3)

        # Check if the server is still up
        try:
            inst.rootdse.get_attr_val_utf8('vendorVersion')
        except ldap.SERVER_DOWN:
            pytest.fail("Server is not responding after malformed SASL packet")
    finally:
        if not inst.status():
            inst.start()
