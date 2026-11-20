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

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.user import UserAccounts
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1
DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

BIND_PASSWORD = 'MvRdn_bind_pw_1'
WRONG_BIND_PASSWORD = 'not_the_right_password'


def _multivalued_cn_rdn(num_components):
    """Return an RDN with num_components cn= AVAs joined by '+'."""
    return '+'.join('cn={}'.format(n) for n in range(num_components))


def _user_properties(num_components, uid_suffix, user_password=None):
    """Build UserAccount properties for a multi-valued cn RDN entry."""
    cn_values = ['{}'.format(n) for n in range(num_components)]
    uid = 'mvrdn_{}'.format(uid_suffix)
    uid_number = 10000 + num_components
    props = {
        'uid': uid,
        'cn': cn_values,
        'sn': uid,
        'uidNumber': str(uid_number),
        'gidNumber': '2000',
        'homeDirectory': '/home/{}'.format(uid),
    }
    if user_password is not None:
        props['userPassword'] = user_password
    return props


def test_multivalued_rdn_add_and_invalid_syntax(topo):
    """Add entries with large multi-valued RDNs and reject malformed RDN tails

    :id: 39351bb0-fd39-45f0-9fe1-63c5a12bfab9
    :setup: Standalone instance
    :steps:
        1. Add an entry whose RDN has five cn AVAs
        2. Add an entry whose RDN has fifteen cn AVAs
        3. Attempt to add an entry with a trailing invalid RDN AVA (two cn plus \\,)
        4. Attempt the same malformed RDN pattern with more than eleven cn AVAs
    :expectedresults:
        1. Add succeeds and the entry is readable
        2. Add succeeds and the entry is readable
        3. Add fails with Invalid DN syntax
        4. Add fails with Invalid DN syntax
    """
    inst = topo.standalone
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    created = []

    try:
        user5 = users.create(
            rdn=_multivalued_cn_rdn(5),
            properties=_user_properties(5, 'five'))
        created.append(user5)
        assert user5.exists()

        user15 = users.create(
            rdn=_multivalued_cn_rdn(15),
            properties=_user_properties(15, 'fifteen'))
        created.append(user15)
        assert user15.exists()

        with pytest.raises(ldap.INVALID_DN_SYNTAX) as exc_info:
            users.create(
                rdn='cn=1+cn=2+\\,',
                properties=_user_properties(2, 'invalid_short'))

        invalid_long_rdn = '{}+\\,'.format(_multivalued_cn_rdn(12))
        with pytest.raises(ldap.INVALID_DN_SYNTAX) as exc_info:
            users.create(
                rdn=invalid_long_rdn,
                properties=_user_properties(12, 'invalid_long'))
    finally:
        for user in created:
            try:
                user.delete()
            except ldap.LDAPError as exc:
                log.warning('cleanup delete of %s failed: %s', user.dn,
                            str(exc))


def test_multivalued_rdn_bind(topo):
    """Bind to accounts whose RDNs have many cn AVAs

    :id: 04ac32ba-1cd0-4cef-b932-33afcef01bfe
    :setup: Standalone instance
    :steps:
        1. Create users with five- and fifteen-AVA multivalued cn RDNs and passwords
        2. Bind with the correct password for each user
        3. Bind with a wrong password for each user
    :expectedresults:
        1. Users are created
        2. Binds succeed
        3. Binds fail with Invalid credentials
    """
    inst = topo.standalone
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    created = []

    try:
        for count, suffix in ((5, 'bind_five'), (15, 'bind_fifteen')):
            user = users.create(
                rdn=_multivalued_cn_rdn(count),
                properties=_user_properties(count, suffix,
                                            user_password=BIND_PASSWORD))
            created.append(user)
            assert user.exists()

            user.bind(BIND_PASSWORD)

            with pytest.raises(ldap.INVALID_CREDENTIALS):
                user.bind(WRONG_BIND_PASSWORD)

            DirectoryManager(inst).rebind()
    finally:
        DirectoryManager(inst).rebind()
        for user in created:
            try:
                user.delete()
            except ldap.LDAPError as exc:
                log.warning('cleanup delete of %s failed: %s', user.dn,
                            str(exc))
