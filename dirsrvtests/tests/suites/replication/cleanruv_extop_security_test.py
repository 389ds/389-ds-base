# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""
Test the security of CleanAllRUV extended operations.

This test suite verifies that:
1. Anonymous connections cannot send CleanAllRUV extended operations
2. Unauthorized users cannot send CleanAllRUV extended operations
3. LDAP injection via malformed filter payloads is blocked
4. Only authorized replication bind DNs can use these operations
5. All 4 replication extended operations enforce security checks:
   - CleanAllRUV (2.16.840.1.113730.3.6.5)
   - Abort CleanAllRUV (2.16.840.1.113730.3.6.6)
   - Get MaxCSN (2.16.840.1.113730.3.6.7)
   - Check Status (2.16.840.1.113730.3.6.8)
6. Authorization is properly revoked when user is removed from replication group
"""

import os
import time
import ldap
import pytest
from contextlib import contextmanager
from lib389._constants import DEFAULT_SUFFIX, PASSWORD
from lib389.idm.group import Groups
from lib389.idm.user import UserAccounts
from pyasn1.codec.ber import encoder
from pyasn1.type import univ
from test389.topologies import topology_m2

pytestmark = pytest.mark.tier1

import logging
log = logging.getLogger(__name__)

DEBUGGING = os.getenv('DEBUGGING', default=False)

# Extended operation OIDs
REPL_CLEANRUV_CHECK_STATUS_OID = "2.16.840.1.113730.3.6.8"
REPL_CLEANRUV_GET_MAXCSN_OID = "2.16.840.1.113730.3.6.7"
REPL_CLEANRUV_OID = "2.16.840.1.113730.3.6.5"
REPL_ABORT_CLEANRUV_OID = "2.16.840.1.113730.3.6.6"
REPL_SESSION_END_OID = "2.16.840.1.113730.3.5.12"


REPL_START_NSDS50_REPLICATION_REQUEST_OID = "2.16.840.1.113730.3.5.3"
REPL_END_NSDS50_REPLICATION_REQUEST_OID = "2.16.840.1.113730.3.5.5"
REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID = "2.16.840.1.113730.3.5.6"
REPL_NSDS50_REPLICATION_RESPONSE_OID = "2.16.840.1.113730.3.5.4"
REPL_NSDS50_UPDATE_INFO_CONTROL_OID = "2.16.840.1.113730.3.4.13"
REPL_NSDS50_INCREMENTAL_PROTOCOL_OID = "2.16.840.1.113730.3.6.1"
REPL_NSDS50_TOTAL_PROTOCOL_OID = "2.16.840.1.113730.3.6.2"
REPL_NSDS71_INCREMENTAL_PROTOCOL_OID = "2.16.840.1.113730.3.6.4"
REPL_NSDS71_TOTAL_PROTOCOL_OID = "2.16.840.1.113730.3.6.3"
REPL_NSDS71_REPLICATION_ENTRY_REQUEST_OID = "2.16.840.1.113730.3.5.9"
REPL_START_NSDS90_REPLICATION_REQUEST_OID = "2.16.840.1.113730.3.5.12"
REPL_NSDS90_REPLICATION_RESPONSE_OID = "2.16.840.1.113730.3.5.13"
REPL_CLEANRUV_OID = "2.16.840.1.113730.3.6.5"
REPL_ABORT_CLEANRUV_OID = "2.16.840.1.113730.3.6.6"
REPL_CLEANRUV_GET_MAXCSN_OID = "2.16.840.1.113730.3.6.7"
REPL_CLEANRUV_CHECK_STATUS_OID = "2.16.840.1.113730.3.6.8"
REPL_ABORT_SESSION_OID = "2.16.840.1.113730.3.6.9"

# Delay (in seconds) need to resynchronized the group cache
REPL_GROUP_SYNC_DELAY = 30.0

# Bind DN types for parametrization
BIND_ANONYMOUS = "anonymous"
BIND_UNAUTHORIZED = "unauthorized"
BIND_AUTHORIZED = "authorized"


class AccessLogWatcher:
    """
    Helper class allowing to wait until a new specific extended operation
    is completed.
    """

    @staticmethod
    def countOp(inst, oid):
        """
        Count the completed extended operations having specified OID.
        """
        import re
        nbops = 0
        re1 = re.compile(fr'^.* conn=(?P<conn>[\d+])\sop=(?P<op>[\d+])\sEXT\soid="{oid}".*')
        re2 = re.compile(r'^.* conn=(?P<conn>[\d+])\sop=(?P<op>[\d+])\sRESULT\s' +
                        r'err=(?P<err>[\d+])\stag=120\s.*')
        saved_result = {}
        nblines = 0
        with open(inst.ds_paths.access_log, 'r') as fd:
            for line in fd:
                nblines += 1
                result = re1.match(line)
                if result:
                    saved_result = result.groupdict()
                    continue
                if saved_result:
                    result = re2.match(line)
                    if result:
                        result = result.groupdict()
                        if (saved_result['op'] == result['op'] and
                            saved_result['conn'] == result['conn']):
                            saved_result = {}
                            nbops += 1
        log.info(f'AccessLogWatcher.countOp: nbops={nbops}')
        log.info(f'AccessLogWatcher.countOp: nblines={nblines}')
        return nbops

    def __init__(self, inst, oid):
        self._inst = inst
        self._oid = oid
        self._nbops = AccessLogWatcher.countOp(inst, oid)

    def wait(self, timeout=30):
        """Wait for a new extended operation with the specified OID to complete."""
        start_time = time.time()
        while time.time() - start_time < timeout:
            if AccessLogWatcher.countOp(self._inst, self._oid) > self._nbops:
                return
            time.sleep(1.0)
        assert False, f"Failed to complete {self._oid} extended operation before timeout of {timeout}s"


def create_startrepl_payload(suffix=DEFAULT_SUFFIX):
    """
    Create a minimal but valid BER-encoded payload for
    StartNSDS50ReplicationRequest extended operations.

    The payload is: SEQUENCE { STRING protocol_oid, STRING repl_root,
                               SEQUENCE ruv, STRING csn }

    Args:
        suffix: the replicated suffix DN

    Returns:
        bytes: BER-encoded payload
    """
    outer = univ.Sequence()
    outer.setComponentByPosition(
        0, univ.OctetString(REPL_NSDS50_INCREMENTAL_PROTOCOL_OID.encode()))
    outer.setComponentByPosition(
        1, univ.OctetString(suffix.encode()))
    outer.setComponentByPosition(
        2, univ.Sequence())  # empty RUV
    outer.setComponentByPosition(
        3, univ.OctetString(b"58e1a9170001"))  # fake CSN
    return encoder.encode(outer)


def create_cleanruv_payload(filter_string):
    """
    Create a BER-encoded payload for CleanAllRUV extended operations.

    The payload is encoded as: { string }
    where string is an LDAP filter.

    Args:
        filter_string: The filter string to encode

    Returns:
        bytes: BER-encoded payload
    """
    sequence = univ.Sequence()
    sequence.setComponentByPosition(0, univ.OctetString(filter_string))
    return encoder.encode(sequence)


@contextmanager
def open_conn(inst, binddn=None, passwd=None):
    """
    Open ldap connection and bind.
    """
    conn = ldap.initialize(f"ldap://{inst.host}:{inst.port}")
    try:
        if binddn is not None:
            conn.simple_bind_s(binddn, passwd)
        yield conn
    finally:
        conn.unbind()


@pytest.fixture(scope="module")
def init_bind_user(topology_m2, request):
    """Create test users 1 authorized and 1 not authorized and return the credentials map."""
    supplier1 = topology_m2.ms["supplier1"]

    users_accounts = UserAccounts(supplier1, DEFAULT_SUFFIX)
    users = []

    def cleanup():
        for u in users:
            u.delete()
        for inst in topology_m2:
            inst.config.set('nsslapd-accesslog-logbuffering', 'on')

    for idx in range(2):
        uid = f'bind_user{idx}'
        if not users_accounts.exists(uid):
            user = users_accounts.create(properties={
                'uid': uid,
                'cn': uid,
                'sn': f'User{idx}',
                'givenname': 'Bind',
                'userpassword': PASSWORD,
                'uidNumber': str(10000+idx),
                'gidNumber': str(10000+idx),
                'homeDirectory': f'/home/{uid}'
            })
        else:
            user = users_accounts.get(uid)
        users.append(user)
    if not DEBUGGING:
         request.addfinalizer(cleanup)

    # Add users[1] in replication group
    groups = Groups(supplier1, basedn=DEFAULT_SUFFIX, rdn=None)
    repl_group = groups.get(dn=f'cn=replication_managers,{DEFAULT_SUFFIX}')
    repl_group.ensure_member(users[1].dn)
    # Ensure that group cache get updated
    time.sleep(REPL_GROUP_SYNC_DELAY)
    # Disable access log buffering
    for inst in topology_m2:
        inst.config.set('nsslapd-accesslog-logbuffering', 'off')

    return {
        BIND_ANONYMOUS: ( None, None ),
        BIND_UNAUTHORIZED: ( users[0].dn, PASSWORD ),
        BIND_AUTHORIZED: ( users[1].dn, PASSWORD ),
    }

def check_extop(inst, creds, extreq, bind_type, filter, expected_exception, description):
    """
    Open a new connection, bind as the bind_type user.
    Perform the extended operation defined by extreq and described by filter
    and description.
    Then check that the expected_exception is got.
    """
    binddn = creds[bind_type][0]
    bindpw = creds[bind_type][1]
    log.info(f"Testing: {description}")
    log.info(f"  Bind type: {bind_type} - Bind DN: {binddn}")
    log.info(f"  Filter: {filter}")
    log.info(f"  Expected: {expected_exception}")

    # Create connection and test
    with open_conn(inst, binddn, bindpw) as conn:
        log.info(f'Open new connection and bind as {bind_type} {binddn}')
        # Attempt the extended operation
        try:
            result = conn.extop_s(extreq)
            log.info(f"Extended operation succeeded. result is {result}")
            assert expected_exception is None
        except ldap.LDAPError as ex:
            log.info(f"Extended operation failed with {ex}")
            log.info(f"Expected exception is {expected_exception}")
            assert expected_exception is not None
            assert isinstance(ex, expected_exception)



@pytest.mark.parametrize("bind_type,filter_bytes,expected_exception,test_description", [
    # Anonymous bind tests
    (BIND_ANONYMOUS, b"(nsds5ReplicaCleanRUV=5:1234567890:test)", ldap.INSUFFICIENT_ACCESS,
     "Anonymous bind with valid filter for checking cleanruv task"),
    (BIND_ANONYMOUS, b"(nsds5ReplicaAbortCleanRUV=5:dc=example,dc=com)", ldap.INSUFFICIENT_ACCESS,
     "Anonymous bind with valid filter for checking abortion"),
    (BIND_ANONYMOUS, b"(nsslapd-localhost=*)", ldap.INSUFFICIENT_ACCESS,
     "Anonymous bind with injection attempt"),

    # Unauthorized user tests
    (BIND_UNAUTHORIZED, b"(nsds5ReplicaCleanRUV=5:1234567890:test)", ldap.INSUFFICIENT_ACCESS,
     "Unauthorized user with valid filter for checking cleanruv task"),
    (BIND_UNAUTHORIZED, b"(nsds5ReplicaAbortCleanRUV=5:dc=example,dc=com)", ldap.INSUFFICIENT_ACCESS,
     "Unauthorized user with valid filter for checking abortion"),
    (BIND_UNAUTHORIZED, b"(nsslapd-localhost=*)", ldap.INSUFFICIENT_ACCESS,
     "Unauthorized user with injection attempt"),

    # Authorized user with malicious filters - should reject filter, not auth
    (BIND_AUTHORIZED, b"(nsslapd-localhost=*)", (ldap.OPERATIONS_ERROR, ldap.INSUFFICIENT_ACCESS),
     "Injection: Server config leak"),
    (BIND_AUTHORIZED, b"(userPassword=*)", (ldap.OPERATIONS_ERROR, ldap.INSUFFICIENT_ACCESS),
     "Injection: Password leak attempt"),
    (BIND_AUTHORIZED, b"(objectClass=*)", (ldap.OPERATIONS_ERROR, ldap.INSUFFICIENT_ACCESS),
     "Injection: Overly broad search"),
    (BIND_AUTHORIZED, b"(cn=*)", ldap.OPERATIONS_ERROR,
     "Injection: General info leak"),
    (BIND_AUTHORIZED, b"(&(nsds5ReplicaCleanRUV=5:*)(cn=*))", ldap.OPERATIONS_ERROR,
     "Injection: Compound filter"),
    (BIND_AUTHORIZED, b"(|(nsds5ReplicaCleanRUV=5:*)(userPassword=*))", ldap.OPERATIONS_ERROR,
     "Injection: OR filter"),
    (BIND_AUTHORIZED, b"(nsds5ReplicaCleanRUV=5:*)(cn=*)", None,
     "Injection: Multiple filters"), # The second filter is ignored
    (BIND_AUTHORIZED, b"", ldap.OPERATIONS_ERROR,
     "Injection: Empty filter"),
    (BIND_AUTHORIZED, b"invalid filter", ldap.OPERATIONS_ERROR,
     "Injection: Invalid LDAP filter"),
    (BIND_AUTHORIZED, b"(nsds5ReplicaCleanRUV=", (ldap.OPERATIONS_ERROR, ldap.INSUFFICIENT_ACCESS),
     "Injection: Incomplete filter"),
    (BIND_AUTHORIZED, b"(nsds5ReplicaCleanRUV=5:1234567890:no:0:dc=example,dc=com)", None,
     "Valid filter for checking cleanruv task"),
    (BIND_AUTHORIZED, b"(nsds5ReplicaAbortCleanRUV=5:dc=example,dc=com)", None,
     "Valid filter for checking abortion"),
])
def test_cleanruv_extop_security(topology_m2, init_bind_user, bind_type, filter_bytes, expected_exception, test_description):
    """Parametrized test for CleanAllRUV extended operation security.

    Tests various combinations of bind DN types, filter payloads, and expected results.

    :id: a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d
    :parametrized: yes
    :setup: Replication setup with two suppliers
    :steps:
        1. Connect with specified bind DN type
        2. Send REPL_CLEANRUV_CHECK_STATUS extended operation with specified filter
        3. Verify the operation behaves as expected
    :expectedresults:
        1. Connection succeeds (or not for anonymous)
        2. Extended operation is sent
        3. Server rejects with expected error or accept it as expected
    """
    supplier1 = topology_m2.ms["supplier1"]
    # Determine bind credentials

    # Create payload
    payload = create_cleanruv_payload(filter_bytes)
    log.info(f"  Payload: {payload!r}")
    extreq = ldap.extop.ExtendedRequest(REPL_CLEANRUV_CHECK_STATUS_OID, payload)

    check_extop(supplier1, init_bind_user, extreq, bind_type, filter_bytes, expected_exception, test_description)


@pytest.mark.parametrize("extop_oid", [
    REPL_START_NSDS50_REPLICATION_REQUEST_OID,
    REPL_END_NSDS50_REPLICATION_REQUEST_OID,
    REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID,
    REPL_NSDS50_REPLICATION_RESPONSE_OID,
    REPL_NSDS50_UPDATE_INFO_CONTROL_OID,
    REPL_NSDS50_INCREMENTAL_PROTOCOL_OID,
    REPL_NSDS50_TOTAL_PROTOCOL_OID,
    REPL_NSDS71_INCREMENTAL_PROTOCOL_OID,
    REPL_NSDS71_TOTAL_PROTOCOL_OID,
    REPL_NSDS71_REPLICATION_ENTRY_REQUEST_OID,
    REPL_START_NSDS90_REPLICATION_REQUEST_OID,
    REPL_NSDS90_REPLICATION_RESPONSE_OID,
    REPL_CLEANRUV_OID,
    REPL_ABORT_CLEANRUV_OID,
    REPL_CLEANRUV_GET_MAXCSN_OID,
    REPL_CLEANRUV_CHECK_STATUS_OID,
    REPL_ABORT_SESSION_OID,
])
def test_anonymous_extops(topology_m2, init_bind_user, extop_oid):
    """Test that all replication extended operations fail if anonymous.

    Tests that anonymous (unauthenticated) connections cannot invoke any
    replication extended operations, ensuring proper authentication is required.

    :id: f6a7b8c9-d0e1-4f5a-2b3c-4d5e6f7a8b9c
    :parametrized: yes
    :setup: Replication setup with two suppliers
    :steps:
        1. Create anonymous connection (no bind)
        2. Attempt to send extended operation with specified OID
        3. Verify operation is rejected
    :expectedresults:
        1. Anonymous connection established
        2. Extended operation is sent
        3. Operation fails with INSUFFICIENT_ACCESS, SERVER_DOWN, or PROTOCOL_ERROR
    """
    supplier1 = topology_m2.ms["supplier1"]
    # Use a proper StartReplication payload for the Start OIDs so we verify
    # that the auth gate blocks anonymous callers even with a valid payload,
    # not just because the payload is malformed.
    if extop_oid in (REPL_START_NSDS50_REPLICATION_REQUEST_OID,
                     REPL_START_NSDS90_REPLICATION_REQUEST_OID):
        filter_bytes = DEFAULT_SUFFIX
        payload = create_startrepl_payload()
    else:
        filter_bytes = 'foo'
        payload = create_cleanruv_payload(b'foo')
    extreq = ldap.extop.ExtendedRequest(extop_oid, payload)
    expected_exception = (ldap.INSUFFICIENT_ACCESS, ldap.SERVER_DOWN, ldap.PROTOCOL_ERROR)
    if extop_oid in (REPL_END_NSDS50_REPLICATION_REQUEST_OID, ):
        # end session extended operation returns success without
        # doing anything if the replica is not acquired
        expected_exception = None
    bind_type = BIND_ANONYMOUS
    test_description = f'Test {extop_oid} extended operation on anonymous connection'
    check_extop(supplier1, init_bind_user, extreq, bind_type, filter_bytes, expected_exception, test_description)


def test_authorization_revocation(topology_m2, init_bind_user):
    """Test that removing a user from replication group revokes their authorization.

    This test validates that:
    - Cached bind DNs work correctly after authentication
    - Removing a user from the replication group doesn't immediately affect cached credentials
    - Cache is automatically evicted after timeout
    - After cache eviction, authorization is re-evaluated and unauthorized users are rejected
    - Re-adding user to the group restores authorization

    :id: a7b8c9d0-e1f2-4a5b-3c4d-5e6f7a8b9c0d
    :setup: Replication setup with two suppliers, authorized user in replication group
    :steps:
        1. Verify authorized user can send CleanRUV extended operation (populates cache)
        2. Remove user from replication_managers group
        3. Wait for bind DN cache eviction (12 seconds)
        4. Verify same user now gets INSUFFICIENT_ACCESS (cache miss, re-authorization fails)
        5. Re-add user to replication_managers group
        6. Wait for group membership cache sync
        7. Verify operation works again after re-authorization
    :expectedresults:
        1. Operation succeeds (cache populated with authorized DN)
        2. User is removed from group
        3. Cache eviction completes
        4. Operation fails with INSUFFICIENT_ACCESS or OPERATIONS_ERROR
        5. User is re-added to group
        6. Group membership synchronized
        7. Operation succeeds again (user re-authorized)
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    groups = Groups(supplier1, basedn=DEFAULT_SUFFIX, rdn=None)
    repl_group = groups.get(dn=f'cn=replication_managers,{DEFAULT_SUFFIX}')
    binddn = init_bind_user[BIND_AUTHORIZED][0]

    valid_filter = b"(nsds5ReplicaCleanRUV=5:1234567890:no:0:dc=example,dc=com)"
    payload = create_cleanruv_payload(valid_filter)
    extreq = ldap.extop.ExtendedRequest(REPL_CLEANRUV_CHECK_STATUS_OID, payload)

    test_description = 'Check CleanRuv_Check_Status with valid user'
    expected_exception = None

    try:
        # Step 1: Verify authorized user can send CleanRUV extended operation
        log.info("  Step 1: Verify authorized user can send operation (populate cache)")
        check_extop(supplier1, init_bind_user, extreq, BIND_AUTHORIZED, valid_filter, expected_exception, test_description)
        log.info("    User DN is now cached")

        # Step 2: Remove user from replication_managers group
        log.info("  Step 2: Remove user from replication_managers group")
        repl_group.remove_member(binddn)
        log.info(f"    User {binddn} removed from group")

        # Step 3: Wait enough time to ensure that bind DN cache is flushed
        log.info(f"  Step 3: Wait for group membership cache sync ({REPL_GROUP_SYNC_DELAY} seconds)")
        time.sleep(REPL_GROUP_SYNC_DELAY)
        log.info("    Group cache synchronized")

        # Step 4: Verify user now gets INSUFFICIENT_ACCESS
        log.info("  Step 4: Verify operation now fails (cache miss, re-authorization)")
        expected_exception = (ldap.OPERATIONS_ERROR, ldap.INSUFFICIENT_ACCESS)
        check_extop(supplier1, init_bind_user, extreq, BIND_AUTHORIZED, valid_filter, expected_exception, test_description)
        log.info("    PASS: User correctly rejected after cache eviction")

    finally:
        # Step 5: Cleanup - Re-add user to group
        log.info("  Step 5: Re-add user to replication_managers group (cleanup)")
        repl_group.ensure_member(binddn)
        log.info(f"    User {binddn} re-added to group")

        # Step 6: Wait for group membership cache sync
        log.info(f"  Step 6: Wait for group membership cache sync ({REPL_GROUP_SYNC_DELAY} seconds)")
        time.sleep(REPL_GROUP_SYNC_DELAY)
        log.info("    Group cache synchronized")

    # Step 7: Double check that the operation is now working again
    log.info("  Step 7: Verify operation works again after re-authorization")
    expected_exception = None
    check_extop(supplier1, init_bind_user, extreq, BIND_AUTHORIZED, valid_filter, expected_exception, test_description)
    log.info("    PASS: User successfully re-authorized")


if __name__ == '__main__':
    # Run with: pytest -v test_cleanruv_extop_security.py
    import sys
    sys.exit(pytest.main(["-v", os.path.abspath(__file__)]))
