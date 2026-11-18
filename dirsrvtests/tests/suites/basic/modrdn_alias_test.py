# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Regression tests for MODRDN when an RDN uses a schema alias of the attribute
type (i.e. "commonName" instead of the canonical name "cn").

The server stores the RDN attribute under its canonical name, so looking the
attribute up with the alias the client used fails.  For the new RDN that used
to leave the entry with a duplicated/inconsistent attribute list and made the
subsequent RDN lookup in entry_get_rdn_mods() dereference a NULL Slapi_Attr,
killing ns-slapd.  For the old RDN the generated deleteoldrdn mod matched no
attribute of the entry and the rename failed.
"""

import ldap
import logging
import os
import pytest
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.group import Groups
from lib389.idm.user import UserAccount, UserAccounts
from test389.topologies import topology_st as topo

log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
logging.getLogger(__name__).setLevel(logging.DEBUG if DEBUGGING else logging.INFO)

USER_CN = 'Alias Rdn User'
USER_NEW_CN = 'Alias Rdn User Renamed'

# Every test works on its own entries: the topology is shared by the whole
# module, so reusing a DN would make a test fail on a leftover of another one.
GROUP_CN = 'alias_new_rdn_group'
GROUP_NEW_CN = 'alias_new_rdn_group_renamed'
OLD_GROUP_CN = 'alias_old_rdn_group'
OLD_GROUP_ALIAS_CN = 'alias_old_rdn_group_aliased'
OLD_GROUP_FINAL_CN = 'alias_old_rdn_group_final'


def _rename(inst, entry, new_rdn, deloldrdn):
    """Rename an entry and make sure ns-slapd survived the operation.

    A crash shows up as a lost connection, and systemd may have restarted
    the instance by the time we check its status, so both are tested.
    """

    log.info(f"Renaming {entry.dn} to {new_rdn} (deleteoldrdn={deloldrdn})")
    try:
        entry.rename(new_rdn, deloldrdn=deloldrdn)
    except (ldap.SERVER_DOWN, ldap.CONNECT_ERROR) as e:
        pytest.fail(f"Connection lost on MODRDN to '{new_rdn}' - "
                    f"ns-slapd most likely crashed: {e}")
    assert inst.status(), \
        f"ns-slapd is no longer running after the MODRDN to '{new_rdn}'"


def _remove_users(inst, dns):
    """Remove the listed entries if they exist."""

    for dn in dns:
        entry = UserAccount(inst, dn)
        if entry.exists():
            entry.delete()


def _remove_groups(groups, cns):
    """Remove the groups holding any of the listed cn values."""

    for cn in cns:
        if groups.exists(cn):
            groups.get(cn).delete()


@pytest.fixture(scope="function")
def alias_user(topo, request):
    """An inetOrgPerson/posixAccount whose RDN is uid and that has a cn value."""

    inst = topo.standalone
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    # Every DN the entry may be left at
    dns = (f'uid=aliasRdnUser,ou=People,{DEFAULT_SUFFIX}',
           f'commonName={USER_NEW_CN},ou=People,{DEFAULT_SUFFIX}')

    # Start from a clean state: in debug mode a previous test keeps its entries
    _remove_users(inst, dns)
    user = users.create(properties={
        'uid': 'aliasRdnUser',
        'cn': USER_CN,
        'sn': 'User',
        'uidNumber': '1010',
        'gidNumber': '1010',
        'homeDirectory': '/home/aliasRdnUser',
    })

    def fin():
        if DEBUGGING or not inst.status():
            # Keep the entries so that the run can be investigated
            return
        _remove_users(inst, dns)

    request.addfinalizer(fin)
    return user


def _create_group(topo, request, cns):
    """Create the group cns[0], cns being every cn value it may end up with."""

    inst = topo.standalone
    groups = Groups(inst, DEFAULT_SUFFIX)

    # Start from a clean state: in debug mode a previous test keeps its entries
    _remove_groups(groups, cns)
    group = groups.create(properties={
        'cn': cns[0],
        'description': 'group renamed through a cn alias',
    })

    def fin():
        if DEBUGGING or not inst.status():
            # Keep the entries so that the run can be investigated
            return
        _remove_groups(groups, cns)

    request.addfinalizer(fin)
    return group


@pytest.fixture(scope="function")
def alias_group(topo, request):
    """A group whose RDN attribute (cn) is renamed through its alias."""

    return _create_group(topo, request, (GROUP_CN, GROUP_NEW_CN))


@pytest.fixture(scope="function")
def old_rdn_alias_group(topo, request):
    """A group that gets an aliased RDN before being renamed again."""

    return _create_group(topo, request,
                         (OLD_GROUP_CN, OLD_GROUP_ALIAS_CN, OLD_GROUP_FINAL_CN))


def test_modrdn_new_rdn_uses_attribute_alias(topo, alias_user):
    """MODRDN to an RDN spelled with an attribute alias must not crash the server

    :id: ee58579b-1b8e-4895-8c3f-5406279d2e55
    :setup: Standalone instance with a user uid=aliasRdnUser holding a cn value
    :steps:
        1. Rename the user to "commonName=<new value>" (alias of cn),
           keeping the old RDN
        2. Check that the server is still running
        3. Read the renamed entry back
        4. Check that cn holds both the original and the new RDN value
        5. Check the entry is returned by a search on the new cn value
    :expectedresults:
        1. The rename succeeds
        2. ns-slapd is still running
        3. The entry is found under its new DN
        4. Both values are present in the single cn attribute
        5. Exactly one entry is returned
    """

    inst = topo.standalone
    new_rdn = f'commonName={USER_NEW_CN}'

    # Keep the old RDN: uid is a MUST of posixAccount and removing it would
    # make the operation fail on the schema check before the flaw is reached.
    _rename(inst, alias_user, new_rdn, deloldrdn=False)

    user = UserAccount(inst, f'{new_rdn},ou=People,{DEFAULT_SUFFIX}')
    assert user.exists(), f"Entry not found under its new DN {user.dn}"

    cn_values = set(user.get_attr_vals_utf8('cn'))
    log.info(f"cn values after the rename: {cn_values}")
    assert cn_values == {USER_CN, USER_NEW_CN}, \
        f"cn is inconsistent after the rename: {cn_values}"
    assert user.get_attr_val_utf8('uid') == 'aliasRdnUser'

    found = UserAccounts(inst, DEFAULT_SUFFIX).filter(f'(cn={USER_NEW_CN})')
    assert len(found) == 1, \
        f"Expected 1 entry with cn={USER_NEW_CN}, got {[e.dn for e in found]}"


def test_modrdn_old_and_new_rdn_use_same_attribute_alias(topo, alias_group):
    """MODRDN replacing the RDN value through an alias must not crash the server

    :id: fe469ef2-920d-4008-b263-e85880369e08
    :setup: Standalone instance with a group cn=alias_rdn_group
    :steps:
        1. Rename the group to "commonName=<new value>" with deleteoldrdn set
        2. Check that the server is still running
        3. Read the renamed entry back
        4. Check that cn only holds the new RDN value
        5. Check that the old DN is gone
    :expectedresults:
        1. The rename succeeds
        2. ns-slapd is still running
        3. The entry is found under its new DN
        4. cn holds the new value only
        5. The old entry no longer exists
    """

    inst = topo.standalone
    groups = Groups(inst, DEFAULT_SUFFIX)
    new_rdn = f'commonName={GROUP_NEW_CN}'

    _rename(inst, alias_group, new_rdn, deloldrdn=True)

    group = groups.get(GROUP_NEW_CN)
    cn_values = set(group.get_attr_vals_utf8('cn'))
    log.info(f"cn values after the rename: {cn_values}")
    assert cn_values == {GROUP_NEW_CN}, \
        f"cn is inconsistent after the rename: {cn_values}"
    assert not groups.exists(GROUP_CN), "The old entry still exists"


def test_modrdn_old_rdn_uses_attribute_alias(topo, old_rdn_alias_group):
    """MODRDN of an entry whose current RDN is spelled with an attribute alias

    The deleteoldrdn mod must target the attribute under the name the entry
    holds it with, otherwise it matches nothing and the rename fails.

    :id: d31d0845-b05d-4619-acd7-7e1f321cb5b2
    :setup: Standalone instance with a group cn=alias_old_rdn_group
    :steps:
        1. Rename the group to "commonName=<value>" so its RDN uses the alias
        2. Rename it again to "cn=<other value>" with deleteoldrdn set
        3. Check that the server is still running
        4. Check that cn only holds the last RDN value
        5. Check that neither of the two previous entries is left behind
    :expectedresults:
        1. The rename succeeds
        2. The rename succeeds
        3. ns-slapd is still running
        4. cn holds the last value only
        5. Both previous entries are gone
    """

    inst = topo.standalone
    groups = Groups(inst, DEFAULT_SUFFIX)

    _rename(inst, old_rdn_alias_group, f'commonName={OLD_GROUP_ALIAS_CN}',
            deloldrdn=True)
    _rename(inst, old_rdn_alias_group, f'cn={OLD_GROUP_FINAL_CN}',
            deloldrdn=True)

    group = groups.get(OLD_GROUP_FINAL_CN)
    cn_values = set(group.get_attr_vals_utf8('cn'))
    log.info(f"cn values after the renames: {cn_values}")
    assert cn_values == {OLD_GROUP_FINAL_CN}, \
        f"cn is inconsistent after the renames: {cn_values}"
    assert not groups.exists(OLD_GROUP_CN), "The original entry still exists"
    assert not groups.exists(OLD_GROUP_ALIAS_CN), \
        "The intermediate entry still exists"


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
