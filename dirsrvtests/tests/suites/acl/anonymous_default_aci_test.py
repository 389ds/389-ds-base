# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Regression tests for the default anonymous access ACI (ticket 48354)."""

import logging
import os

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.account import Anonymous
from lib389.idm.domain import Domain
from test389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

DEFAULT_ANON_ACI = (
    '(targetattr!="userPassword || aci")(version 3.0; '
    'acl "Enable anonymous access"; '
    'allow (read, search, compare) userdn="ldap:///anyone";)'
)

TEST_PASSWORD = 'testpassword'

ENTRY_ACI = (
    '(targetattr="cn || sn || uid")(version 3.0; '
    'acl "test entry aci"; allow (read) userdn="ldap:///self";)'
)


def _user_properties(uid, uid_number):
    return {
        'uid': uid,
        'cn': uid,
        'sn': uid,
        'uidNumber': str(uid_number),
        'gidNumber': str(uid_number),
        'homeDirectory': f'/home/{uid}',
    }


@pytest.fixture(scope='function')
def anonymous_test_users(topo, request):
    """Create users with userPassword, entry-level aci, or both."""

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    created = []

    # Create user with userPassword
    user_pw = users.create(properties={
        **_user_properties('anon_test_pw', 10001),
        'userPassword': TEST_PASSWORD,
    })
    created.append(user_pw)

    # Create user with entry-level aci
    user_aci = users.create(properties=_user_properties('anon_test_aci', 10002))
    user_aci.add('aci', ENTRY_ACI)
    created.append(user_aci)

    # Create user with both userPassword and entry-level aci
    user_pw_aci = users.create(properties={
        **_user_properties('anon_test_pw_aci', 10003),
        'userPassword': TEST_PASSWORD,
    })
    user_pw_aci.add('aci', ENTRY_ACI)
    created.append(user_pw_aci)

    def fin():
        for user in created:
            try:
                user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

    request.addfinalizer(fin)

    return {
        'user_pw': user_pw,
        'user_aci': user_aci,
        'user_pw_aci': user_pw_aci,
    }


def _attr_present(conn, attr_name):
    """Return True if anonymous search finds any entry matching (attr=*)."""
    results = UserAccounts(conn, DEFAULT_SUFFIX).filter(f'({attr_name}=*)', scope=ldap.SCOPE_SUBTREE, strict=True)
    if DEBUGGING:
        log.debug(f"Anonymous ({attr_name}=*) search returned: {results}")
    return len(results) > 0


@pytest.fixture(scope='function')
def anonymous_default_aci_setup(topo, request):
    """Install the default anonymous ACI on the suffix."""
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    saved_acis = domain.get_attr_vals_utf8('aci') or []

    domain.remove_all('aci')
    domain.add('aci', DEFAULT_ANON_ACI)

    def fin():
        domain.remove_all('aci')
        for aci in saved_acis:
            domain.add('aci', aci)

    request.addfinalizer(fin)


def test_anonymous_cannot_read_userpassword_or_aci(topo, anonymous_default_aci_setup, anonymous_test_users):
    """Anonymous users cannot read userPassword or aci with the default ACI

    Regression for ticket 48354: the default anonymous access ACI must exclude
    both userPassword and aci from anonymous read access.

    :id: e67a099d-72c6-4cf2-b0f8-50dfb3430e6b
    :setup: Standalone instance with default anonymous ACI and users that
            have userPassword and/or entry-level aci attributes
    :steps:
        1. Bind anonymously
        2. Search the suffix for entries matching (userPassword=*)
        3. Search the suffix for entries matching (aci=*)
    :expectedresults:
        1. Anonymous bind succeeds
        2. No entries are returned
        3. No entries are returned
    """
    anon_conn = Anonymous(topo.standalone).bind()

    assert not _attr_present(anon_conn, 'userPassword'), (
        'Anonymous user must not see entries with userPassword'
    )
    assert not _attr_present(anon_conn, 'aci'), (
        'Anonymous user must not see entries with aci'
    )


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(['-s', CURRENT_FILE])
