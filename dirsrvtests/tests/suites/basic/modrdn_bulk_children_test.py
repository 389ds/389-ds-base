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
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.organizationalunit import OrganizationalUnit, OrganizationalUnits
from lib389.idm.user import UserAccount, UserAccounts
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

NUM_USERS = 110
ORGUNIT1_RDN = 'ou=people'
ORGUNIT2_RDN = 'ou=orgunit2'
ORGUNIT1_DN = f'{ORGUNIT1_RDN},{DEFAULT_SUFFIX}'
ORGUNIT2_DN = f'{ORGUNIT2_RDN},{DEFAULT_SUFFIX}'
ORGUNIT1_UNDER_ORGUNIT2_DN = f'{ORGUNIT1_RDN},{ORGUNIT2_DN}'


def _expected_user_dn(uid_str, parent_dn):
    return f'uid={uid_str},{parent_dn}'


def _create_orgunit_users(inst):
    """Create uid=1..NUM_USERS under ou=people."""
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    for uid in range(1, NUM_USERS + 1):
        uid_str = str(uid)
        users.create(
            f'uid={uid_str}',
            properties={
                'uid': uid_str,
                'cn': f'user {uid_str}',
                'sn': f'user{uid_str}',
                'uidNumber': str(1000 + uid),
                'gidNumber': '2000',
                'homeDirectory': f'/home/{uid_str}',
            },
        )


def test_modrdn_orgunit_moves_users_under_new_superior(topo, request):
    """Modrdn an organizational unit to a new superior and verify child user DNs

    :id: 12dae475-b778-4ba6-a48b-d713ff2c0e58
    :setup: Standalone Instance
    :steps:
        1. Create 110 users under ou=people (uid=1 through uid=110)
        2. Create ou=orgunit2 under the default suffix
        3. Modrdn ou=people with superior ou=orgunit2
        4. Verify every uid=1..110 has a DN under ou=people,ou=orgunit2
    :expectedresults:
        1. All 110 users are created
        2. orgunit2 is created
        3. Modrdn succeeds
        4. All 110 user DNs are uid=N,ou=people,ou=orgunit2,<suffix> and none remain at the old location
    """
    inst = topo.standalone
    ou2 = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(properties={'ou': 'orgunit2'})
    assert ou2.exists()

    _create_orgunit_users(inst)
    for uid in range(1, NUM_USERS + 1):
        uid_str = str(uid)
        assert UserAccount(inst, _expected_user_dn(uid_str, ORGUNIT1_DN)).exists()

    people_ou = OrganizationalUnit(inst, ORGUNIT1_DN)
    people_ou.rename(ORGUNIT1_RDN, newsuperior=ou2.dn)
    assert OrganizationalUnit(inst, ORGUNIT1_UNDER_ORGUNIT2_DN).exists()
    assert not OrganizationalUnit(inst, ORGUNIT1_DN).exists()

    for uid in range(1, NUM_USERS + 1):
        uid_str = str(uid)
        expected_dn = _expected_user_dn(uid_str, ORGUNIT1_UNDER_ORGUNIT2_DN)
        user = UserAccount(inst, expected_dn)
        assert user.exists(), expected_dn
        assert user.dn == expected_dn
        old_dn = _expected_user_dn(uid_str, ORGUNIT1_DN)
        assert not UserAccount(inst, old_dn).exists(), old_dn


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
