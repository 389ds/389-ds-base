# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Tests for CVE: Stale SASL auxprop identity lets a failed cn=Directory
Manager bind be inherited by any subsequent successful bind on the same
connection.

Three root-cause defects combine into one privilege-escalation vulnerability:

1. prop_set() in ids_sasl_canon_user() APPENDS to the propctx "dn" slot
   instead of replacing -- a failed bind's candidate DN survives.
2. The propctx is not reset after a failed one-shot SASL exchange
   (only after CONN_FLAG_SASL_COMPLETE or continuing).
3. ids_sasl_check_bind() reads dnval[0].values[0] (first value ever stored)
   and installs it as the bind identity without validating it against
   SASL_USERNAME (the actual authenticated user).

Attack vectors tested:
  - PLAIN(DM, wrong_pw) -> ANONYMOUS  (zero credentials)
  - PLAIN(DM, wrong_pw) -> PLAIN(user, correct_pw)
  - DIGEST-MD5(DM, wrong_pw) -> ANONYMOUS (alternate planter)
  - DIGEST-MD5(DM, wrong_pw) -> PLAIN(user, correct_pw)
  - Negative controls for all variants
"""

import os
import pytest
import ldap
import ldap.sasl
import logging

from test389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PW_DM
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

MGR_DN_AUTHCID = "dn:cn=Directory Manager"
TEST_USER_UID = "sasl_test_user"
TEST_USER_PW = "Secret_123"
CONFIG_DN = "cn=config"
ROOT_DN_ATTR = "nsslapd-rootdn"
ROOT_PW_ATTR = "nsslapd-rootpw"


@pytest.fixture(scope="module")
def setup_sasl(topology_st, request):
    """Enable TLS, create a test user with cleartext password for SASL PLAIN
    and DIGEST-MD5 testing.

    Returns a dict with the instance and LDAPS URI.
    """
    inst = topology_st.standalone

    # Enable TLS
    inst.enable_tls()
    ldaps_uri = inst.get_ldaps_uri()

    # Store password as CLEAR so DIGEST-MD5 can use it
    inst.config.replace('passwordStorageScheme', 'CLEAR')
    inst.restart()

    # Create test user
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    try:
        user = users.create(properties={
            'uid': TEST_USER_UID,
            'cn': 'SASL Test User',
            'sn': 'TestUser',
            'uidNumber': '19999',
            'gidNumber': '19999',
            'homeDirectory': '/home/sasl_test_user',
            'userPassword': TEST_USER_PW,
        })
    except ldap.ALREADY_EXISTS:
        user = users.get(TEST_USER_UID)
        user.replace('userPassword', TEST_USER_PW)

    # Rebind as DM to restore admin state
    inst.simple_bind_s(DN_DM, PW_DM, escapehatch='i am sure')

    def fin():
        inst.simple_bind_s(DN_DM, PW_DM, escapehatch='i am sure')
        # Restore password to known value in case a test changed it
        inst.config.replace(ROOT_PW_ATTR, PW_DM)
        inst.config.replace('passwordStorageScheme', 'PBKDF2-SHA512')
        if not DEBUGGING:
            try:
                user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass
            except Exception:
                log.warning("Teardown: failed to delete test user %s",
                            TEST_USER_UID, exc_info=True)

    request.addfinalizer(fin)

    return {
        'inst': inst,
        'ldaps_uri': ldaps_uri,
        'user_dn': user.dn,
    }


def _new_conn(ldaps_uri):
    """Open a raw LDAPS connection with TLS cert verification disabled
    and SASL secprops set to 'none' (required for PLAIN and ANONYMOUS).
    """
    conn = ldap.initialize(ldaps_uri)
    conn.set_option(ldap.OPT_X_TLS_REQUIRE_CERT, ldap.OPT_X_TLS_NEVER)
    conn.set_option(ldap.OPT_X_TLS_NEWCTX, 0)
    conn.set_option(ldap.OPT_PROTOCOL_VERSION, 3)
    conn.set_option(ldap.OPT_X_SASL_SECPROPS, "none")
    return conn


def _plain_bind(conn, authcid, password):
    """Perform a SASL PLAIN bind."""
    auth = ldap.sasl.sasl(
        {ldap.sasl.CB_AUTHNAME: authcid, ldap.sasl.CB_PASS: password},
        "PLAIN",
    )
    conn.sasl_interactive_bind_s("", auth)


def _anonymous_bind(conn):
    """Perform a SASL ANONYMOUS bind."""
    auth = ldap.sasl.sasl(
        {ldap.sasl.CB_AUTHNAME: "tester@example.com"},
        "ANONYMOUS",
    )
    conn.sasl_interactive_bind_s("", auth)


def _digest_md5_bind(conn, authcid, password):
    """Perform a SASL DIGEST-MD5 bind."""
    auth = ldap.sasl.sasl(
        {ldap.sasl.CB_AUTHNAME: authcid, ldap.sasl.CB_PASS: password},
        "DIGEST-MD5",
    )
    conn.sasl_interactive_bind_s("", auth)


def _has_digest_md5(ldaps_uri):
    """Check if the server advertises DIGEST-MD5 in supportedSASLMechanisms."""
    conn = _new_conn(ldaps_uri)
    try:
        res = conn.search_s("", ldap.SCOPE_BASE,
                            attrlist=["supportedSASLMechanisms"])
        mechs = res[0][1].get("supportedSASLMechanisms", [])
        return b"DIGEST-MD5" in mechs
    except ldap.LDAPError:
        return False
    finally:
        conn.unbind_s()


def _can_read_rootdn(conn):
    """Return True if the connection can read nsslapd-rootdn from cn=config.

    Only treats INSUFFICIENT_ACCESS and NO_SUCH_OBJECT as "access denied".
    Other LDAPError subtypes (e.g. SERVER_DOWN) are re-raised so test
    infrastructure failures are not silently masked as false negatives.
    """
    try:
        res = conn.search_s(CONFIG_DN, ldap.SCOPE_BASE, attrlist=[ROOT_DN_ATTR])
        # Non-DM may get empty result list or result with no attributes
        if not res:
            return False
        return bool(res[0][1].get(ROOT_DN_ATTR))
    except (ldap.INSUFFICIENT_ACCESS, ldap.NO_SUCH_OBJECT):
        return False


def _can_write_rootpw(conn, password):
    """Return True if the connection can modify nsslapd-rootpw in cn=config.

    Only treats INSUFFICIENT_ACCESS as "access denied".
    Other LDAPError subtypes (e.g. SERVER_DOWN) are re-raised so test
    infrastructure failures are not silently masked as false negatives.
    """
    try:
        conn.modify_s(CONFIG_DN, [
            (ldap.MOD_REPLACE, ROOT_PW_ATTR, [password.encode()])
        ])
        return True
    except ldap.INSUFFICIENT_ACCESS:
        return False


# =========================================================================
#  Core exploit: PLAIN planter -> ANONYMOUS inheritor (zero credentials)
# =========================================================================

def test_plain_to_anonymous_no_escalation(setup_sasl):
    """Verify PLAIN(DM,wrong) then ANONYMOUS does NOT escalate to DM

    :id: 635b4adb-a3b1-4f1e-8e08-b1c0293605b8
    :setup: Standalone instance with TLS enabled, test user created
    :steps:
        1. Open LDAPS connection
        2. Attempt SASL PLAIN bind as cn=Directory Manager with wrong password
        3. Perform SASL ANONYMOUS bind on the same connection
        4. Check whoami -- should be anonymous, NOT Directory Manager
        5. Attempt to read nsslapd-rootdn from cn=config
        6. Attempt to write nsslapd-rootpw in cn=config
    :expectedresults:
        1. Connection opens
        2. PLAIN bind fails with INVALID_CREDENTIALS
        3. ANONYMOUS bind succeeds
        4. whoami does NOT contain 'directory manager'
        5. Read is denied (no DM access)
        6. Write is denied (no DM access)
    """
    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        # Plant stale DN via failed PLAIN
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _plain_bind(conn, MGR_DN_AUTHCID, "wrong-password")

        # ANONYMOUS inherit attempt
        _anonymous_bind(conn)

        # Verify NO escalation
        whoami = conn.whoami_s()
        log.info("whoami after PLAIN(DM,wrong)->ANONYMOUS: %s", whoami)
        assert "directory manager" not in whoami.lower(), \
            f"ESCALATION: whoami returned '{whoami}' -- stale DM identity inherited!"

        assert not _can_read_rootdn(conn), \
            "ESCALATION: anonymous connection can read nsslapd-rootdn!"

        assert not _can_write_rootpw(conn, PW_DM), \
            "ESCALATION: anonymous connection can write nsslapd-rootpw!"
    finally:
        conn.unbind_s()


def test_plain_to_anonymous_write_rootpw_blocked(setup_sasl):
    """Verify PLAIN(DM,wrong) then ANONYMOUS cannot modify nsslapd-rootpw

    :id: fb406e5a-7165-485b-b929-a709cc25fb5c
    :setup: Standalone instance with TLS enabled
    :steps:
        1. Open LDAPS connection
        2. Fail SASL PLAIN as DM (wrong password)
        3. SASL ANONYMOUS bind on same connection
        4. Attempt to modify nsslapd-rootpw in cn=config
    :expectedresults:
        1. Connection opens
        2. PLAIN bind fails with INVALID_CREDENTIALS
        3. ANONYMOUS bind succeeds
        4. Modify is denied with INSUFFICIENT_ACCESS
    """
    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _plain_bind(conn, MGR_DN_AUTHCID, "wrong-password")

        _anonymous_bind(conn)

        with pytest.raises(ldap.INSUFFICIENT_ACCESS):
            conn.modify_s(CONFIG_DN, [
                (ldap.MOD_REPLACE, ROOT_PW_ATTR, [b"hacked-password"])
            ])
    finally:
        conn.unbind_s()


# =========================================================================
#  PLAIN planter -> PLAIN inheritor (requires one valid account)
# =========================================================================

def test_plain_to_plain_no_escalation(setup_sasl):
    """Verify PLAIN(DM,wrong) then PLAIN(user,correct) does NOT escalate

    :id: fac3d162-d2db-4cdc-b856-6ec59de8d566
    :setup: Standalone instance with TLS enabled, test user created
    :steps:
        1. Open LDAPS connection
        2. Fail SASL PLAIN as DM (wrong password)
        3. SASL PLAIN bind as test user with correct password
        4. Check whoami -- should be test user, NOT Directory Manager
        5. Attempt to read nsslapd-rootdn from cn=config
        6. Attempt to write nsslapd-rootpw in cn=config
    :expectedresults:
        1. Connection opens
        2. PLAIN bind fails with INVALID_CREDENTIALS
        3. PLAIN bind succeeds as test user
        4. whoami returns test user DN
        5. Read is denied
        6. Write is denied
    """
    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _plain_bind(conn, MGR_DN_AUTHCID, "wrong-password")

        _plain_bind(conn, TEST_USER_UID, TEST_USER_PW)

        whoami = conn.whoami_s()
        log.info("whoami after PLAIN(DM,wrong)->PLAIN(user): %s", whoami)
        assert "directory manager" not in whoami.lower(), \
            f"ESCALATION: whoami returned '{whoami}' -- stale DM identity inherited!"
        assert TEST_USER_UID in whoami.lower(), \
            f"Unexpected identity: {whoami}"

        assert not _can_read_rootdn(conn), \
            "ESCALATION: test user can read nsslapd-rootdn!"

        assert not _can_write_rootpw(conn, PW_DM), \
            "ESCALATION: test user can write nsslapd-rootpw!"
    finally:
        conn.unbind_s()


# =========================================================================
#  DIGEST-MD5 planter -> ANONYMOUS inheritor
# =========================================================================

def test_digestmd5_to_anonymous_no_escalation(setup_sasl):
    """Verify DIGEST-MD5(DM,wrong) then ANONYMOUS does NOT escalate

    :id: 6e0dea8d-6a85-41c1-ab7b-fc7b30a6b0dd
    :setup: Standalone instance with TLS enabled
    :steps:
        1. Open LDAPS connection
        2. Fail SASL DIGEST-MD5 as DM (wrong password)
        3. SASL ANONYMOUS bind on same connection
        4. Check whoami -- should be anonymous, NOT Directory Manager
        5. Attempt to read nsslapd-rootdn
        6. Attempt to write nsslapd-rootpw
    :expectedresults:
        1. Connection opens
        2. DIGEST-MD5 bind fails
        3. ANONYMOUS bind succeeds
        4. whoami does NOT contain 'directory manager'
        5. Read is denied
        6. Write is denied
    """
    if not _has_digest_md5(setup_sasl['ldaps_uri']):
        pytest.skip("DIGEST-MD5 not available on this server")

    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _digest_md5_bind(conn, MGR_DN_AUTHCID, "wrong-password")

        _anonymous_bind(conn)

        whoami = conn.whoami_s()
        log.info("whoami after DIGEST-MD5(DM,wrong)->ANONYMOUS: %s", whoami)
        assert "directory manager" not in whoami.lower(), \
            f"ESCALATION: whoami returned '{whoami}' -- stale DM identity inherited!"

        assert not _can_read_rootdn(conn), \
            "ESCALATION: anonymous can read nsslapd-rootdn after DIGEST-MD5 plant!"

        assert not _can_write_rootpw(conn, PW_DM), \
            "ESCALATION: anonymous can write nsslapd-rootpw after DIGEST-MD5 plant!"
    finally:
        conn.unbind_s()


# =========================================================================
#  DIGEST-MD5 planter -> PLAIN inheritor (cross-mechanism)
# =========================================================================

def test_digestmd5_to_plain_no_escalation(setup_sasl):
    """Verify DIGEST-MD5(DM,wrong) then PLAIN(user,correct) does NOT escalate

    :id: 3227e913-b40e-49ba-8d48-42a568be066a
    :setup: Standalone instance with TLS enabled, test user with cleartext pw
    :steps:
        1. Open LDAPS connection
        2. Fail SASL DIGEST-MD5 as DM (wrong password)
        3. SASL PLAIN bind as test user with correct password
        4. Check whoami -- should be test user, NOT Directory Manager
        5. Attempt to read nsslapd-rootdn
        6. Attempt to write nsslapd-rootpw
    :expectedresults:
        1. Connection opens
        2. DIGEST-MD5 bind fails
        3. PLAIN bind succeeds as test user
        4. whoami returns test user DN
        5. Read is denied
        6. Write is denied
    """
    if not _has_digest_md5(setup_sasl['ldaps_uri']):
        pytest.skip("DIGEST-MD5 not available on this server")

    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _digest_md5_bind(conn, MGR_DN_AUTHCID, "wrong-password")

        _plain_bind(conn, TEST_USER_UID, TEST_USER_PW)

        whoami = conn.whoami_s()
        log.info("whoami after DIGEST-MD5(DM,wrong)->PLAIN(user): %s", whoami)
        assert "directory manager" not in whoami.lower(), \
            f"ESCALATION: whoami returned '{whoami}' -- stale DM identity inherited!"
        assert TEST_USER_UID in whoami.lower(), \
            f"Unexpected identity: {whoami}"

        assert not _can_read_rootdn(conn), \
            "ESCALATION: test user can read nsslapd-rootdn!"

        assert not _can_write_rootpw(conn, PW_DM), \
            "ESCALATION: test user can write nsslapd-rootpw!"
    finally:
        conn.unbind_s()


# =========================================================================
#  Negative controls -- verify normal auth still works
# =========================================================================

def test_control_anonymous_alone(setup_sasl):
    """Control: fresh ANONYMOUS bind alone has no DM access

    :id: 282633e3-6f25-40ea-911d-a36918acedb5
    :setup: Standalone instance with TLS enabled
    :steps:
        1. Open LDAPS connection (no prior failed bind)
        2. SASL ANONYMOUS bind
        3. Check whoami -- should be anonymous
        4. Attempt to read nsslapd-rootdn
    :expectedresults:
        1. Connection opens
        2. ANONYMOUS bind succeeds
        3. whoami is anonymous (not DM)
        4. Read is denied
    """
    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        _anonymous_bind(conn)

        whoami = conn.whoami_s()
        log.info("whoami (anonymous alone): %s", whoami)
        assert "directory manager" not in whoami.lower()
        assert not _can_read_rootdn(conn)
    finally:
        conn.unbind_s()


def test_control_plain_user_alone(setup_sasl):
    """Control: PLAIN(user) alone gets correct identity, no DM access

    :id: d4a064b7-c214-4b3c-bed5-557bca8a33b0
    :setup: Standalone instance with TLS enabled, test user created
    :steps:
        1. Open LDAPS connection (no prior failed bind)
        2. SASL PLAIN bind as test user
        3. Check whoami -- should be test user
        4. Attempt to read nsslapd-rootdn
    :expectedresults:
        1. Connection opens
        2. PLAIN bind succeeds
        3. whoami returns test user DN
        4. Read is denied
    """
    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        _plain_bind(conn, TEST_USER_UID, TEST_USER_PW)

        whoami = conn.whoami_s()
        log.info("whoami (plain user alone): %s", whoami)
        assert TEST_USER_UID in whoami.lower()
        assert "directory manager" not in whoami.lower()
        assert not _can_read_rootdn(conn)
    finally:
        conn.unbind_s()


def test_control_reversed_order(setup_sasl):
    """Control: ANONYMOUS first, then failed PLAIN(DM) does NOT escalate

    :id: f2786e51-eb4c-405b-9788-962ea55b5777
    :setup: Standalone instance with TLS enabled
    :steps:
        1. Open LDAPS connection
        2. SASL ANONYMOUS bind (succeeds -- sets CONN_FLAG_SASL_COMPLETE)
        3. Fail SASL PLAIN as DM (wrong password)
        4. Attempt to read nsslapd-rootdn
    :expectedresults:
        1. Connection opens
        2. ANONYMOUS bind succeeds
        3. PLAIN bind fails with INVALID_CREDENTIALS
        4. Read is denied (ANONYMOUS completed -> reset on next bind)
        5. whoami confirms identity is NOT directory manager
    """
    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        _anonymous_bind(conn)

        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _plain_bind(conn, MGR_DN_AUTHCID, "wrong-password")

        assert not _can_read_rootdn(conn)
        whoami = conn.whoami_s()
        log.info("whoami after reversed order: %s", whoami)
        assert "directory manager" not in whoami.lower()
    finally:
        conn.unbind_s()


def test_control_digestmd5_user_alone(setup_sasl):
    """Control: DIGEST-MD5(user) alone gets correct identity

    :id: ad790c63-04fe-47ee-ad01-d0de3d4e6560
    :setup: Standalone instance with TLS, test user with cleartext password
    :steps:
        1. Open LDAPS connection
        2. SASL DIGEST-MD5 bind as test user
        3. Check whoami -- should be test user
        4. Attempt to read nsslapd-rootdn
    :expectedresults:
        1. Connection opens
        2. DIGEST-MD5 bind succeeds
        3. whoami returns test user DN
        4. Read is denied
    """
    if not _has_digest_md5(setup_sasl['ldaps_uri']):
        pytest.skip("DIGEST-MD5 not available on this server")

    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        _digest_md5_bind(conn, TEST_USER_UID, TEST_USER_PW)

        whoami = conn.whoami_s()
        log.info("whoami (digest-md5 user alone): %s", whoami)
        assert TEST_USER_UID in whoami.lower()
        assert "directory manager" not in whoami.lower()
        assert not _can_read_rootdn(conn)
    finally:
        conn.unbind_s()


# =========================================================================
#  Finding 3: SASL_USERNAME vs propctx DN mismatch
# =========================================================================

def test_sasl_username_matches_bind_identity(setup_sasl):
    """Verify the installed bind identity matches the authenticated user,
    not a stale DN from a prior failed bind

    :id: 21eedace-f97e-4f33-b54e-a5b541f6e92d
    :setup: Standalone instance with TLS enabled, test user created
    :steps:
        1. Open LDAPS connection
        2. Fail SASL PLAIN as DM (wrong password)
        3. SASL PLAIN bind as test user with correct password
        4. Check whoami -- must be test user DN, not DM
        5. Verify test user cannot read DM-only attributes
        6. Verify test user cannot write DM-only attributes
    :expectedresults:
        1. Connection opens
        2. PLAIN bind fails with INVALID_CREDENTIALS
        3. PLAIN bind succeeds
        4. whoami returns test user DN (SASL_USERNAME and propctx DN match)
        5. Read is denied
        6. Write is denied
    """
    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _plain_bind(conn, MGR_DN_AUTHCID, "wrong-password")

        _plain_bind(conn, TEST_USER_UID, TEST_USER_PW)

        whoami = conn.whoami_s()
        log.info("whoami: %s (expected: test user)", whoami)

        # The authenticated user is test_user -- identity MUST match
        assert TEST_USER_UID in whoami.lower(), \
            f"Identity mismatch: SASL_USERNAME should be test user but whoami={whoami}"
        assert "directory manager" not in whoami.lower(), \
            f"Stale DN installed: whoami={whoami} despite test user authentication"

        # DM-only operations must fail
        assert not _can_read_rootdn(conn), \
            "SASL_USERNAME ignored: test user got DM read access"
        assert not _can_write_rootpw(conn, PW_DM), \
            "SASL_USERNAME ignored: test user got DM write access"
    finally:
        conn.unbind_s()


# =========================================================================
#  DIGEST-MD5 as inheritor -- Cyrus SASL authz check should block
# =========================================================================

def test_digestmd5_inheritor_blocked_by_authz(setup_sasl):
    """Verify DIGEST-MD5 cannot inherit stale DN due to Cyrus SASL authz check

    :id: b4a1b749-ccdb-4220-9097-b817b5b3a970
    :setup: Standalone instance with TLS, test user with cleartext password
    :steps:
        1. Open LDAPS connection
        2. Fail SASL PLAIN as DM (wrong password)
        3. SASL DIGEST-MD5 bind as test user with correct password
        4. Check whoami -- should be test user (Cyrus SASL blocks identity
           mismatch between authn and authz)
        5. Attempt to read nsslapd-rootdn
    :expectedresults:
        1. Connection opens
        2. PLAIN bind fails
        3. DIGEST-MD5 bind succeeds as test user
        4. whoami returns test user DN (not DM)
        5. Read is denied
    """
    if not _has_digest_md5(setup_sasl['ldaps_uri']):
        pytest.skip("DIGEST-MD5 not available on this server")

    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _plain_bind(conn, MGR_DN_AUTHCID, "wrong-password")

        _digest_md5_bind(conn, TEST_USER_UID, TEST_USER_PW)

        whoami = conn.whoami_s()
        log.info("whoami after PLAIN(DM)->DIGEST-MD5(user): %s", whoami)
        assert TEST_USER_UID in whoami.lower(), \
            f"Unexpected identity: {whoami}"
        assert "directory manager" not in whoami.lower(), \
            f"ESCALATION via DIGEST-MD5 inheritor: {whoami}"
        assert not _can_read_rootdn(conn)
    finally:
        conn.unbind_s()


def test_digestmd5_both_planter_and_inheritor_blocked(setup_sasl):
    """Verify DIGEST-MD5(DM,wrong) then DIGEST-MD5(user,correct) does not escalate

    :id: 432e9f74-007e-42dd-b9a4-359e86541cda
    :setup: Standalone instance with TLS, test user with cleartext password
    :steps:
        1. Open LDAPS connection
        2. Fail SASL DIGEST-MD5 as DM (wrong password)
        3. SASL DIGEST-MD5 bind as test user with correct password
        4. Check whoami -- should be test user
        5. Attempt to read nsslapd-rootdn
    :expectedresults:
        1. Connection opens
        2. DIGEST-MD5 bind fails
        3. DIGEST-MD5 bind succeeds as test user
        4. whoami returns test user DN (not DM)
        5. Read is denied
    """
    if not _has_digest_md5(setup_sasl['ldaps_uri']):
        pytest.skip("DIGEST-MD5 not available on this server")

    conn = _new_conn(setup_sasl['ldaps_uri'])
    try:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _digest_md5_bind(conn, MGR_DN_AUTHCID, "wrong-password")

        _digest_md5_bind(conn, TEST_USER_UID, TEST_USER_PW)

        whoami = conn.whoami_s()
        log.info("whoami after DIGEST-MD5(DM)->DIGEST-MD5(user): %s", whoami)
        assert TEST_USER_UID in whoami.lower(), \
            f"Unexpected identity: {whoami}"
        assert "directory manager" not in whoami.lower(), \
            f"ESCALATION: {whoami}"
        assert not _can_read_rootdn(conn)
    finally:
        conn.unbind_s()


# =========================================================================
#  Full privilege escalation: read + write verification
# =========================================================================

def test_no_rootpw_write_after_any_escalation_vector(setup_sasl):
    """Comprehensive check: no escalation vector can write nsslapd-rootpw

    :id: a16b573a-7e31-4c98-b619-f10fa895921a
    :setup: Standalone instance with TLS enabled, test user created
    :steps:
        1. PLAIN(DM,wrong) -> ANONYMOUS: attempt rootpw write
        2. PLAIN(DM,wrong) -> PLAIN(user): attempt rootpw write
        3. DIGEST-MD5(DM,wrong) -> ANONYMOUS: attempt rootpw write
        4. DIGEST-MD5(DM,wrong) -> PLAIN(user): attempt rootpw write
    :expectedresults:
        1. Write denied
        2. Write denied
        3. Write denied
        4. Write denied
    """
    ldaps_uri = setup_sasl['ldaps_uri']
    has_md5 = _has_digest_md5(ldaps_uri)

    vectors = [
        ("PLAIN->ANONYMOUS", False, lambda c: _plain_bind(c, MGR_DN_AUTHCID, "wrong"), _anonymous_bind),
        ("PLAIN->PLAIN(user)", False, lambda c: _plain_bind(c, MGR_DN_AUTHCID, "wrong"), lambda c: _plain_bind(c, TEST_USER_UID, TEST_USER_PW)),
        ("DIGEST-MD5->ANONYMOUS", True, lambda c: _digest_md5_bind(c, MGR_DN_AUTHCID, "wrong"), _anonymous_bind),
        ("DIGEST-MD5->PLAIN(user)", True, lambda c: _digest_md5_bind(c, MGR_DN_AUTHCID, "wrong"), lambda c: _plain_bind(c, TEST_USER_UID, TEST_USER_PW)),
    ]

    for label, needs_md5, plant_fn, inherit_fn in vectors:
        if needs_md5 and not has_md5:
            log.info("Skipping vector %s: DIGEST-MD5 not available", label)
            continue
        log.info("Testing vector: %s", label)
        conn = _new_conn(ldaps_uri)
        try:
            # Plant
            with pytest.raises(ldap.INVALID_CREDENTIALS):
                plant_fn(conn)

            # Inherit
            inherit_fn(conn)

            # Must NOT be able to write rootpw
            assert not _can_write_rootpw(conn, "hacked-by-" + label), \
                f"ESCALATION via {label}: rootpw write succeeded!"
        finally:
            conn.unbind_s()


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
