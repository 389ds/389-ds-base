# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""
Verify that SELFDN, USERDNATTR, and LDAPURL ACI bind-rule evaluators
reject anonymous clients whose empty bind DN would otherwise
string-match an empty attribute value (the DN syntax validator accepts
zero-length values as valid) or satisfy a broadly-scoped LDAP URL
filter evaluated against the rootDSE.

Reproducer for the vulnerability originally reported as PSIRTSUPT-21812:
the ACI evaluation engine in acllas.c compared the client's bind DN
against a stored attribute value with a plain string compare.  An
anonymous bind has an empty-string DN, and an empty attribute value
passes DN syntax validation, so ``userattr="X#SELFDN"`` or
``userattr="X#USERDN"`` was satisfied by an unauthenticated client
when attribute X held an empty value.  Similarly,
``userattr="X#LDAPURL"`` evaluated the stored LDAP URL filter against
the anonymous client's entry - a search at base ``""`` (rootDSE) - and
a URL with an empty base DN plus a broad filter like
``(objectclass=*)`` would match, granting access.
"""

import logging
import os
import pytest
import ldap

from lib389._constants import DEFAULT_SUFFIX
from lib389._mapped_object import DSLdapObject
from lib389.idm.account import Anonymous
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccount, UserAccounts
from lib389.utils import ensure_bytes
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
OC_NAME = 'selfDnAnonBypass'
OC_OID = '1.2.3.4.5.6.7.8.9.10.99'
OC_MUST = '(cn $ sn)'
OC_MAY = '(owner $ manager $ member $ labeledURI)'
TARGET_OCS = ['top', 'person', OC_NAME]

BIND_NAME = 'selfdn_bind_user'
BIND_DN = 'uid=%s,ou=People,%s' % (BIND_NAME, DEFAULT_SUFFIX)
BIND_PW = 'Secret123'

TARGET_NAME = 'selfdn_target'
TARGET_DN = 'cn=%s,%s' % (TARGET_NAME, DEFAULT_SUFFIX)

SELFDN_ACI_ADD = (
    '(target = "ldap:///cn=*,%s")'
    '(targetfilter = "(objectClass=%s)")'
    '(version 3.0; acl "SELFDN add test"; allow (add)'
    ' userattr = "owner#SELFDN";)' % (DEFAULT_SUFFIX, OC_NAME)
)

USERDNATTR_ACI_SEARCH = (
    '(target = "ldap:///cn=*,%s")'
    '(targetattr = "*")'
    '(targetfilter = "(objectClass=%s)")'
    '(version 3.0; acl "USERDNATTR search test"; allow (read, search, compare)'
    ' userattr = "manager#USERDN";)' % (DEFAULT_SUFFIX, OC_NAME)
)

LDAPURL_ACI_SEARCH = (
    '(target = "ldap:///cn=*,%s")'
    '(targetattr = "*")'
    '(targetfilter = "(objectClass=%s)")'
    '(version 3.0; acl "LDAPURL search test"; allow (read, search, compare)'
    ' userattr = "labeledURI#LDAPURL";)' % (DEFAULT_SUFFIX, OC_NAME)
)

SELFDN_LOG = 'DS_LASUserDnAttrEval - selfdnattr does not match anonymous user'
USERDNATTR_LOG = 'DS_LASUserDnAttrEval - userdnattr does not match anonymous user'
LDAPURL_LOG = 'DS_LASLdapUrlAttrEval - does not match anonymous user'


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _oc_definition():
    """Return the objectclass definition for testing."""
    return ("( %s NAME '%s' DESC 'Test OC for SELFDN/USERDNATTR anon bypass' "
            "SUP person AUXILIARY MUST %s MAY %s )" % (OC_OID, OC_NAME, OC_MUST, OC_MAY))


def _check_error_log(inst, pattern):
    """Return True if *pattern* appears in the instance error log."""
    with open(inst.errlog, 'r') as fh:
        for line in fh:
            if pattern in line:
                return True
    return False


def _create_target_entry(conn, extra_attr, extra_value):
    """Create a target entry with the custom objectclass via DSLdapObject.

    *conn* is the DirSrv connection to use (DM, anon, or authenticated).
    Returns the created DSLdapObject.
    """
    obj = DSLdapObject(conn, dn=TARGET_DN)
    obj._create_objectclasses = list(TARGET_OCS)
    obj._protected = False
    obj.create(rdn='cn=%s' % TARGET_NAME, basedn=DEFAULT_SUFFIX,
               properties={
                   'sn': TARGET_NAME,
                   extra_attr: extra_value,
               })
    return obj


def _delete_target_if_exists(inst):
    """Remove the target entry if it exists (as DM)."""
    obj = DSLdapObject(inst, dn=TARGET_DN)
    obj._protected = False
    if obj.exists():
        obj.delete()


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------
@pytest.fixture(scope="module")
def setup_env(topo):
    """Set up schema, bind user, and ACL logging for the whole module.

    Saves and removes default ACIs to start from a clean deny-all state.
    Restores everything on teardown.
    """
    inst = topo.standalone
    domain = Domain(inst, DEFAULT_SUFFIX)

    # Save current ACIs for restoration
    saved_acis = domain.get_attr_vals_utf8('aci') or []

    # Add the custom objectclass
    inst.schema.add_schema('objectClasses', ensure_bytes(_oc_definition()))

    # Enable ACL debug logging
    inst.config.set('nsslapd-errorlog-level', '128')

    # Create a bind user via lib389 API
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    bind_user = users.create(properties={
        'uid': BIND_NAME,
        'cn': BIND_NAME,
        'sn': BIND_NAME,
        'uidNumber': '99990',
        'gidNumber': '99990',
        'homeDirectory': '/home/%s' % BIND_NAME,
        'userPassword': BIND_PW,
    })

    # Remove default ACIs for deny-all baseline
    domain.remove_all('aci')

    yield topo

    # Teardown: restore ACIs, remove bind user
    if not DEBUGGING:
        bind_user.delete()
    domain.remove_all('aci')
    for aci in saved_acis:
        domain.add('aci', aci)


@pytest.fixture(scope="function")
def selfdn_aci(topo, setup_env, request):
    """Add the SELFDN add ACI and remove it after the test."""
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    domain.add('aci', SELFDN_ACI_ADD)

    def fin():
        domain.ensure_removed('aci', SELFDN_ACI_ADD)
        _delete_target_if_exists(topo.standalone)

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def userdnattr_aci(topo, setup_env, request):
    """Add the USERDNATTR search ACI and remove it after the test."""
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    domain.add('aci', USERDNATTR_ACI_SEARCH)

    def fin():
        domain.ensure_removed('aci', USERDNATTR_ACI_SEARCH)
        _delete_target_if_exists(topo.standalone)

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def ldapurl_aci(topo, setup_env, request):
    """Add the LDAPURL search ACI and remove it after the test."""
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    domain.add('aci', LDAPURL_ACI_SEARCH)

    def fin():
        domain.ensure_removed('aci', LDAPURL_ACI_SEARCH)
        _delete_target_if_exists(topo.standalone)

    request.addfinalizer(fin)


# ---------------------------------------------------------------------------
# SELFDN tests
# ---------------------------------------------------------------------------
def test_selfdn_anon_empty_value_add(topo, selfdn_aci):
    """Anonymous bind must NOT satisfy a SELFDN ACI when the attribute
    holds an empty value.

    :id: 79b27cf0-c646-4a73-802b-8d085bdc4866
    :setup: Standalone instance with custom objectclass, no default ACIs,
            a SELFDN ACI allowing add on the ``owner`` attribute
    :steps:
        1. Bind anonymously
        2. Attempt to add an entry whose ``owner`` attribute is empty ("")
        3. Verify the add is rejected with INSUFFICIENT_ACCESS
        4. Verify the server logged the anonymous-rejection message
    :expectedresults:
        1. Anonymous bind succeeds
        2. Add operation is rejected
        3. INSUFFICIENT_ACCESS is raised
        4. Error log contains the selfdnattr rejection message
    """
    anon_conn = Anonymous(topo.standalone).bind()

    # The vulnerable code path: anonymous DN is "" and owner is "",
    # so slapi_utf8casecmp("", "") == 0 would grant access.
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _create_target_entry(anon_conn, 'owner', '')

    assert _check_error_log(topo.standalone, SELFDN_LOG), \
        "Expected anonymous-rejection log for selfdnattr not found"


def test_selfdn_anon_nonempty_value_add(topo, selfdn_aci):
    """Anonymous bind must NOT satisfy a SELFDN ACI even when the attribute
    holds a non-matching, non-empty value (control case).

    :id: ea88f470-913b-4b06-bfe5-e114f4922d76
    :setup: Standalone instance with custom objectclass, no default ACIs,
            a SELFDN ACI allowing add on the ``owner`` attribute
    :steps:
        1. Bind anonymously
        2. Attempt to add an entry whose ``owner`` is a non-empty DN
        3. Verify the add is rejected with INSUFFICIENT_ACCESS
        4. Verify the server logged the anonymous-rejection message
    :expectedresults:
        1. Anonymous bind succeeds
        2. Add operation is rejected
        3. INSUFFICIENT_ACCESS is raised
        4. Error log contains the selfdnattr rejection message
    """
    anon_conn = Anonymous(topo.standalone).bind()

    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _create_target_entry(anon_conn, 'owner', 'cn=nobody,%s' % DEFAULT_SUFFIX)

    assert _check_error_log(topo.standalone, SELFDN_LOG), \
        "Expected anonymous-rejection log for selfdnattr not found"


def test_selfdn_auth_user_add(topo, selfdn_aci):
    """An authenticated user whose DN matches the attribute value
    MUST be granted access by the SELFDN ACI (positive control).

    :id: 3b19e963-5f03-4a4d-b2c8-1cb585a98af8
    :setup: Standalone instance with custom objectclass, no default ACIs,
            a SELFDN ACI allowing add on the ``owner`` attribute,
            an authenticated bind user
    :steps:
        1. Bind as the test user
        2. Add an entry whose ``owner`` equals the test user's DN
        3. Verify the add succeeds
    :expectedresults:
        1. Authenticated bind succeeds
        2. Add operation succeeds
        3. Entry exists
    """
    user_conn = UserAccount(topo.standalone, BIND_DN).bind(BIND_PW)

    target = _create_target_entry(user_conn, 'owner', BIND_DN)
    assert target.exists(), "Target entry should exist after authenticated add"


# ---------------------------------------------------------------------------
# USERDNATTR tests
# ---------------------------------------------------------------------------
def test_userdnattr_anon_empty_value_search(topo, userdnattr_aci):
    """Anonymous bind must NOT satisfy a USERDNATTR ACI when the attribute
    holds an empty value.

    :id: 95af9f97-5387-4ed9-8187-372158740632
    :setup: Standalone instance with custom objectclass, no default ACIs,
            a USERDNATTR ACI allowing search on the ``manager`` attribute,
            a target entry with ``manager`` set to an empty value
    :steps:
        1. Create target entry (as DM) with ``manager`` set to empty string
        2. Bind anonymously
        3. Search for the target entry
        4. Verify zero results are returned
        5. Verify the server logged the anonymous-rejection message
    :expectedresults:
        1. Entry is created
        2. Anonymous bind succeeds
        3. Search completes
        4. Empty result set confirms anonymous was denied
        5. Error log contains the userdnattr rejection message
    """
    _create_target_entry(topo.standalone, 'manager', '')

    anon_conn = Anonymous(topo.standalone).bind()

    # The vulnerable code path: anonymous DN "" matches manager ""
    ents = anon_conn.search_s(TARGET_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 0, \
        "Anonymous search should return 0 entries with USERDNATTR " \
        "when manager is empty, got %d" % len(ents)

    assert _check_error_log(topo.standalone, USERDNATTR_LOG), \
        "Expected anonymous-rejection log for userdnattr not found"


def test_userdnattr_anon_nonempty_value_search(topo, userdnattr_aci):
    """Anonymous bind must NOT satisfy a USERDNATTR ACI when the attribute
    holds a non-matching DN (control case).

    :id: 00db9c35-e5c2-4bf0-88bf-a4bd59320a1a
    :setup: Standalone instance with custom objectclass, no default ACIs,
            a USERDNATTR ACI allowing search on the ``manager`` attribute,
            a target entry with ``manager`` set to a real DN
    :steps:
        1. Create target entry (as DM) with ``manager`` set to a non-empty DN
        2. Bind anonymously
        3. Search for the target entry
        4. Verify zero results are returned
        5. Verify the server logged the anonymous-rejection message
    :expectedresults:
        1. Entry is created
        2. Anonymous bind succeeds
        3. Search completes
        4. Empty result set confirms anonymous was denied
        5. Error log contains the userdnattr rejection message
    """
    _create_target_entry(topo.standalone, 'manager', 'cn=nobody,%s' % DEFAULT_SUFFIX)

    anon_conn = Anonymous(topo.standalone).bind()

    ents = anon_conn.search_s(TARGET_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 0, \
        "Anonymous search should return 0 entries with USERDNATTR " \
        "when manager is non-matching DN, got %d" % len(ents)

    assert _check_error_log(topo.standalone, USERDNATTR_LOG), \
        "Expected anonymous-rejection log for userdnattr not found"


def test_userdnattr_auth_user_search(topo, userdnattr_aci):
    """An authenticated user whose DN matches the attribute value
    MUST be granted access by the USERDNATTR ACI (positive control).

    :id: 1e04f674-c404-498f-9fe6-8e78f477a588
    :setup: Standalone instance with custom objectclass, no default ACIs,
            a USERDNATTR ACI allowing search on the ``manager`` attribute,
            an authenticated bind user, a target entry with ``manager``
            set to the bind user's DN
    :steps:
        1. Create target entry (as DM) with ``manager`` set to bind user's DN
        2. Bind as the test user
        3. Search for the target entry
        4. Verify exactly one result is returned
    :expectedresults:
        1. Entry is created
        2. Authenticated bind succeeds
        3. Search returns the target entry
        4. Exactly one entry confirms authenticated user was granted access
    """
    _create_target_entry(topo.standalone, 'manager', BIND_DN)

    user_conn = UserAccount(topo.standalone, BIND_DN).bind(BIND_PW)

    ents = user_conn.search_s(TARGET_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 1, \
        "Authenticated search should return 1 entry with USERDNATTR " \
        "when manager matches bind DN, got %d" % len(ents)


# ---------------------------------------------------------------------------
# LDAPURL tests
# ---------------------------------------------------------------------------
def test_ldapurl_anon_empty_basedn_search(topo, ldapurl_aci):
    """Anonymous bind must NOT satisfy an LDAPURL ACI when the stored
    URL has an empty base DN and a broad filter.

    :id: 9a7ff3a1-5747-4850-9d72-a7d560e11ce2
    :setup: Standalone instance with custom objectclass, no default ACIs,
            an LDAPURL ACI allowing search on the ``labeledURI`` attribute,
            a target entry with ``labeledURI`` set to an LDAP URL with an
            empty base DN and ``(objectclass=*)`` filter
    :steps:
        1. Create target entry (as DM) with ``labeledURI`` set to
           ``ldap:///??sub?(objectclass=*)``
        2. Bind anonymously
        3. Search for the target entry
        4. Verify zero results are returned
        5. Verify the server logged the anonymous-rejection message
    :expectedresults:
        1. Entry is created
        2. Anonymous bind succeeds
        3. Search completes
        4. Empty result set confirms anonymous was denied
        5. Error log contains the LDAPURL rejection message
    """
    _create_target_entry(topo.standalone, 'labeledURI',
                         'ldap:///??sub?(objectclass=*)')

    anon_conn = Anonymous(topo.standalone).bind()

    # The vulnerable code path: anonymous client DN is "", the URL has
    # empty base DN, and (objectclass=*) matches the rootDSE.
    ents = anon_conn.search_s(TARGET_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 0, \
        "Anonymous search should return 0 entries with LDAPURL " \
        "when URL has empty base DN, got %d" % len(ents)

    assert _check_error_log(topo.standalone, LDAPURL_LOG), \
        "Expected anonymous-rejection log for LDAPURL not found"


def test_ldapurl_anon_nonempty_basedn_search(topo, ldapurl_aci):
    """Anonymous bind must NOT satisfy an LDAPURL ACI when the stored
    URL has a non-empty base DN (control case - anonymous client's
    empty DN is not under the URL scope).

    :id: 47d2a70b-ef3b-4ec3-981f-2f924910e6a2
    :setup: Standalone instance with custom objectclass, no default ACIs,
            an LDAPURL ACI allowing search on the ``labeledURI`` attribute,
            a target entry with ``labeledURI`` set to an LDAP URL with a
            non-empty base DN
    :steps:
        1. Create target entry (as DM) with ``labeledURI`` set to
           ``ldap:///<suffix>??sub?(objectclass=*)``
        2. Bind anonymously
        3. Search for the target entry
        4. Verify zero results are returned
        5. Verify the server logged the anonymous-rejection message
    :expectedresults:
        1. Entry is created
        2. Anonymous bind succeeds
        3. Search completes
        4. Empty result set confirms anonymous was denied
        5. Error log contains the LDAPURL rejection message
    """
    _create_target_entry(topo.standalone, 'labeledURI',
                         'ldap:///%s??sub?(objectclass=*)' % DEFAULT_SUFFIX)

    anon_conn = Anonymous(topo.standalone).bind()

    ents = anon_conn.search_s(TARGET_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 0, \
        "Anonymous search should return 0 entries with LDAPURL " \
        "when URL has non-empty base DN, got %d" % len(ents)

    assert _check_error_log(topo.standalone, LDAPURL_LOG), \
        "Expected anonymous-rejection log for LDAPURL not found"


def test_ldapurl_auth_user_search(topo, ldapurl_aci):
    """An authenticated user whose entry matches the LDAP URL filter
    MUST be granted access by the LDAPURL ACI (positive control).

    :id: 1c623b15-6c52-494f-b580-fd1d395afbd1
    :setup: Standalone instance with custom objectclass, no default ACIs,
            an LDAPURL ACI allowing search on the ``labeledURI`` attribute,
            an authenticated bind user, a target entry with ``labeledURI``
            set to an LDAP URL whose filter matches the bind user's entry
    :steps:
        1. Create target entry (as DM) with ``labeledURI`` set to
           ``ldap:///<suffix>??sub?(objectclass=person)``
        2. Bind as the test user (whose entry has objectclass=person)
        3. Search for the target entry
        4. Verify exactly one result is returned
    :expectedresults:
        1. Entry is created
        2. Authenticated bind succeeds
        3. Search returns the target entry
        4. Exactly one entry confirms authenticated user was granted access
    """
    # The bind user is uid=...,ou=People,<suffix> with objectclass=person,
    # so the URL filter (objectclass=person) will match.
    _create_target_entry(topo.standalone, 'labeledURI',
                         'ldap:///%s??sub?(objectclass=person)' % DEFAULT_SUFFIX)

    user_conn = UserAccount(topo.standalone, BIND_DN).bind(BIND_PW)

    ents = user_conn.search_s(TARGET_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 1, \
        "Authenticated search should return 1 entry with LDAPURL " \
        "when URL filter matches bind user, got %d" % len(ents)


def test_ldapurl_anon_restrictive_filter_search(topo, ldapurl_aci):
    """Anonymous bind must NOT satisfy an LDAPURL ACI when the stored
    URL filter does not match the rootDSE (even with empty base DN).

    :id: 41b9afe3-21e2-49ba-b12e-a11353be1209
    :setup: Standalone instance with custom objectclass, no default ACIs,
            an LDAPURL ACI allowing search on the ``labeledURI`` attribute,
            a target entry with ``labeledURI`` set to an LDAP URL with
            empty base DN but a restrictive filter
    :steps:
        1. Create target entry (as DM) with ``labeledURI`` set to
           ``ldap:///??sub?(uid=*)`` (rootDSE has no uid attribute)
        2. Bind anonymously
        3. Search for the target entry
        4. Verify zero results are returned
        5. Verify the server logged the anonymous-rejection message
    :expectedresults:
        1. Entry is created
        2. Anonymous bind succeeds
        3. Search completes
        4. Empty result set confirms anonymous was denied
        5. Error log contains the LDAPURL rejection message
    """
    # Even with empty base DN, the filter (uid=*) should not match
    # the rootDSE, so this is denied by filter mismatch AND by the
    # anonymous guard.
    _create_target_entry(topo.standalone, 'labeledURI',
                         'ldap:///??sub?(uid=*)')

    anon_conn = Anonymous(topo.standalone).bind()

    ents = anon_conn.search_s(TARGET_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 0, \
        "Anonymous search should return 0 entries with LDAPURL " \
        "when URL filter is restrictive, got %d" % len(ents)

    assert _check_error_log(topo.standalone, LDAPURL_LOG), \
        "Expected anonymous-rejection log for LDAPURL not found"


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", "-v", CURRENT_FILE])
