# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

"""
This script will test different type of roles.
"""

import os
import pytest

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain
from lib389.idm.role import NestedRoles, ManagedRoles, FilteredRoles
from lib389.idm.account import Anonymous

import ldap


pytestmark = pytest.mark.tier1


OU_ROLE = f"ou=roledntest,{DEFAULT_SUFFIX}"
STEVE_ROLE = f"uid=STEVE_ROLE,{OU_ROLE}"
HARRY_ROLE = f"uid=HARRY_ROLE,{OU_ROLE}"
MARY_ROLE = f"uid=MARY_ROLE,{OU_ROLE}"
ROLE1 = f"cn=ROLE1,{OU_ROLE}"
ROLE2 = f"cn=ROLE2,{OU_ROLE}"
ROLE3 = f"cn=ROLE3,{OU_ROLE}"
ROLE21 = f"cn=ROLE21,{OU_ROLE}"
ROLE31 = f"cn=ROLE31,{OU_ROLE}"
FILTERROLE = f"cn=FILTERROLE,{OU_ROLE}"
JOE_ROLE = f"uid=JOE_ROLE,{OU_ROLE}"
NOROLEUSER = f"uid=NOROLEUSER,{OU_ROLE}"
SCRACHENTRY = f"uid=SCRACHENTRY,{OU_ROLE}"
ALL_ACCESS = f"uid=all access,{OU_ROLE}"
NOT_RULE_ACCESS = f"uid=not rule access,{OU_ROLE}"
OR_RULE_ACCESS = f"uid=or rule access,{OU_ROLE}"
NESTED_ROLE_TESTER = f"uid=nested role tester,{OU_ROLE}"


@pytest.fixture(scope="function")
def _aci_of_user(request, topo):
    """
    Removes and Restores ACIs after the test.
    """
    aci_list = Domain(topo.standalone, DEFAULT_SUFFIX).get_attr_vals_utf8('aci')

    def finofaci():
        """
        Removes and Restores ACIs after the test.
        """
        domain = Domain(topo.standalone, DEFAULT_SUFFIX)
        domain.remove_all('aci')
        for i in aci_list:
            domain.add("aci", i)

    request.addfinalizer(finofaci)


@pytest.fixture(scope="module")
def _add_user(request, topo):
    """
    A Function that will create necessary users delete the created user
    """
    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ou_ou = ous.create(properties={'ou': 'roledntest'})
    ou_ou.set('aci', [f'(target="ldap:///{NESTED_ROLE_TESTER}")(targetattr="*") '
                      f'(version 3.0; aci "nested role aci"; allow(all)'
                      f'roledn = "ldap:///{ROLE2}";)',
                      f'(target="ldap:///{OR_RULE_ACCESS}")(targetattr="*")'
                      f'(version 3.0; aci "or role aci"; allow(all) '
                      f'roledn = "ldap:///{ROLE1} || ldap:///{ROLE21}";)',
                      f'(target="ldap:///{ALL_ACCESS}")(targetattr="*")'
                      f'(version 3.0; aci "anyone role aci"; allow(all) '
                      f'roledn = "ldap:///anyone";)',
                      f'(target="ldap:///{NOT_RULE_ACCESS}")(targetattr="*")'
                      f'(version 3.0; aci "not role aci"; allow(all)'
                      f'roledn != "ldap:///{ROLE1} || ldap:///{ROLE21}";)'])

    nestedroles = NestedRoles(topo.standalone, OU_ROLE)
    for i in [('role2', [ROLE1, ROLE21]), ('role3', [ROLE2, ROLE31])]:
        nestedroles.create(properties={'cn': i[0],
                                       'nsRoleDN': i[1]})

    managedroles = ManagedRoles(topo.standalone, OU_ROLE)
    for i in ['ROLE1', 'ROLE21', 'ROLE31']:
        managedroles.create(properties={'cn': i})

    filterroles = FilteredRoles(topo.standalone, OU_ROLE)
    filterroles.create(properties={'cn': 'filterRole',
                                   'nsRoleFilter': 'sn=Dr Drake',
                                   'description': 'filter role tester'})

    users = UserAccounts(topo.standalone, OU_ROLE, rdn=None)
    for i in [('STEVE_ROLE', ROLE1, 'Has roles 1, 2 and 3.'),
              ('HARRY_ROLE', ROLE21, 'Has roles 21, 2 and 3.'),
              ('MARY_ROLE', ROLE31, 'Has roles 31 and 3.')]:
        users.create(properties={
            'uid': i[0],
            'cn': i[0],
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + i[0],
            'userPassword': PW_DM,
            'nsRoleDN': i[1],
            'Description': i[2]
        })

    for i in [('JOE_ROLE', 'Has filterRole.'),
              ('NOROLEUSER', 'Has no roles.'),
              ('SCRACHENTRY', 'Entry to test rights on.'),
              ('all access', 'Everyone has acccess (incl anon).'),
              ('not rule access', 'Only accessible to mary.'),
              ('or rule access', 'Only to steve and harry but nbot mary or anon'),
              ('nested role tester', 'Only accessible to harry and steve.')]:
        users.create(properties={
            'uid': i[0],
            'cn': i[0],
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + i[0],
            'userPassword': PW_DM,
            'Description': i[1]
            })

    # Setting SN for user JOE
    UserAccount(topo.standalone, f'uid=JOE_ROLE,ou=roledntest,{DEFAULT_SUFFIX}').set('sn', 'Dr Drake')

    def fin():
        """
        It will delete the created users
        """
        for i in users.list() + managedroles.list() + nestedroles.list():
            i.delete()

    request.addfinalizer(fin)


@pytest.mark.parametrize("user,entry", [
    (STEVE_ROLE, NESTED_ROLE_TESTER),
    (HARRY_ROLE, NESTED_ROLE_TESTER),
    (MARY_ROLE, NOT_RULE_ACCESS),
    (STEVE_ROLE, OR_RULE_ACCESS),
    (HARRY_ROLE, OR_RULE_ACCESS),
    (STEVE_ROLE, ALL_ACCESS),
    (HARRY_ROLE, ALL_ACCESS),
    (MARY_ROLE, ALL_ACCESS),
], ids=[
    "(STEVE_ROLE, NESTED_ROLE_TESTER)",
    "(HARRY_ROLE, NESTED_ROLE_TESTER)",
    "(MARY_ROLE, NOT_RULE_ACCESS)",
    "(STEVE_ROLE, OR_RULE_ACCESS)",
    "(HARRY_ROLE, OR_RULE_ACCESS)",
    "(STEVE_ROLE, ALL_ACCESS)",
    "(HARRY_ROLE, ALL_ACCESS)",
    "(MARY_ROLE, ALL_ACCESS)",
])
def test_mod_seealso_positive(topo, _add_user, _aci_of_user, user, entry):
    """
    Testing the roledn keyword that allows access control
    based on the role  of the bound user.

    :id: a33c5d6a-79f4-11e8-8551-8c16451d917b
    :parametrized: yes
    :setup: Standalone server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    conn = UserAccount(topo.standalone, user).bind(PW_DM)
    UserAccount(conn, entry).replace('seeAlso', 'cn=1')


@pytest.mark.parametrize(
    "user,entry", [
        (MARY_ROLE, NESTED_ROLE_TESTER),
        (STEVE_ROLE, NOT_RULE_ACCESS),
        (HARRY_ROLE, NOT_RULE_ACCESS),
        (MARY_ROLE, OR_RULE_ACCESS),
    ], ids=[
        "(MARY_ROLE, NESTED_ROLE_TESTER)",
        "(STEVE_ROLE, NOT_RULE_ACCESS)",
        "(HARRY_ROLE, NOT_RULE_ACCESS)",
        "(MARY_ROLE , OR_RULE_ACCESS)"]
)
def test_mod_seealso_negative(topo, _add_user, _aci_of_user, user, entry):
    """
    Testing the roledn keyword that do not allows access control
    based on the role  of the bound user.

    :id: b2444aa2-79f4-11e8-a2c3-8c16451d917b
    :parametrized: yes
    :setup: Standalone server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    conn = UserAccount(topo.standalone, user).bind(PW_DM)
    user = UserAccount(conn, entry)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace('seeAlso', 'cn=1')


@pytest.mark.parametrize("entry", [NOT_RULE_ACCESS, ALL_ACCESS],
                         ids=["NOT_RULE_ACCESS", "ALL_ACCESS"])
def test_mod_anonseealso_positive(topo, _add_user, _aci_of_user, entry):
    """
    Testing the roledn keyword that allows access control
    based on the role  of the bound user.

    :id: c3eb41ac-79f4-11e8-aa8b-8c16451d917b
    :parametrized: yes
    :setup: Standalone server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    conn = Anonymous(topo.standalone).bind()
    UserAccount(conn, entry).replace('seeAlso', 'cn=1')


@pytest.mark.parametrize("entry", [NESTED_ROLE_TESTER, OR_RULE_ACCESS],
                         ids=["NESTED_ROLE_TESTER", "OR_RULE_ACCESS"])
def test_mod_anonseealso_negaive(topo, _add_user, _aci_of_user, entry):
    """
    Testing the roledn keyword that do not allows access control
    based on the role  of the bound user.

    :id: d385611a-79f4-11e8-adc8-8c16451d917b
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    conn = Anonymous(topo.standalone).bind()
    user = UserAccount(conn, entry)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace('seeAlso', 'cn=1')


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
