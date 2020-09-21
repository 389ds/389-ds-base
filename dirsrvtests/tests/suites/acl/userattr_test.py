# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

"""
This script will test different type of user attributes.
"""

import os
import pytest

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.group import Groups
from lib389.idm.role import ManagedRoles
from lib389.topologies import topology_st as topo

import ldap

pytestmark = pytest.mark.tier1


OU = f"ou=Accounting,{DEFAULT_SUFFIX}"
OU_2 = f"ou=Inheritance,{DEFAULT_SUFFIX}"
CAN = f"uid=Anuj Borah,{OU}"
CANNOT = f"uid=Ananda Borah,{OU}"
LEVEL_0 = f"uid=Grandson,{OU_2}"
LEVEL_1 = f"uid=Child,{OU_2}"
LEVEL_2 = f"uid=Parent,{OU_2}"
LEVEL_3 = f"uid=Grandparent,{OU_2}"
LEVEL_4 = f"uid=Ancestor,{OU_2}"
ROLE1 = f'cn=ROLE1,{OU}'
ROLE2 = f'cn=ROLE2,{OU}'
NSSIMPLEGROUP = f'cn=NSSIMPLEGROUP,{OU}'
NSSIMPLEGROUP1 = f'cn=NSSIMPLEGROUP1,{OU}'
ROLEDNACCESS = f'uid=ROLEDNACCESS,{OU}'
USERDNACCESS = f'uid=USERDNACCESS,{OU}'
GROUPDNACCESS = f'uid=GROUPDNACCESS,{OU}'
LDAPURLACCESS = f'uid=LDAPURLACCESS,{OU}'
ATTRNAMEACCESS = f'uid=ATTRNAMEACCESS,{OU}'
ANCESTORS = f'ou=ANCESTORS,{OU_2}'
GRANDPARENTS = f'ou=GRANDPARENTS,{ANCESTORS}'
PARENTS = f'ou=PARENTS,{GRANDPARENTS}'
CHILDREN = f'ou=CHILDREN,{PARENTS}'
GRANDSONS = f'ou=GRANDSONS,{CHILDREN}'


@pytest.fixture(scope="module")
def _add_user(topo):
    """
    This function will create user for the test and in the end entries will be deleted .
    """
    role_aci_body = '(targetattr="*")(version 3.0; aci "role aci"; allow(all)'
    # Creating OUs
    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ou_accounting = ous.create(properties={'ou': 'Accounting'})
    ou_accounting.set('aci', [f'(target="ldap:///{ROLEDNACCESS}"){role_aci_body} '
                              f'userattr = "Description#ROLEDN";)',
                              f'(target="ldap:///{USERDNACCESS}"){role_aci_body} '
                              f'userattr = "Description#USERDN";)',
                              f'(target="ldap:///{GROUPDNACCESS}"){role_aci_body} '
                              f'userattr = "Description#GROUPDN";)',
                              f'(target="ldap:///{LDAPURLACCESS}"){role_aci_body} '
                              f'userattr = "Description#LDAPURL";)',
                              f'(target="ldap:///{ATTRNAMEACCESS}"){role_aci_body} '
                              f'userattr = "Description#4612";)'])

    ou_inheritance = ous.create(properties={'ou': 'Inheritance',
                                            'street': LEVEL_4,
                                            'seeAlso': LEVEL_3,
                                            'st': LEVEL_2,
                                            'description': LEVEL_1,
                                            'businessCategory': LEVEL_0})

    inheritance_aci_body = '(targetattr="*")(version 3.0; aci "Inheritance aci"; allow(all) '
    ou_inheritance.set('aci', [f'{inheritance_aci_body} '
                               f'userattr = "parent[0].businessCategory#USERDN";)',
                               f'{inheritance_aci_body} '
                               f'userattr = "parent[0,1].description#USERDN";)',
                               f'{inheritance_aci_body} '
                               f'userattr = "parent[0,1,2].st#USERDN";)',
                               f'{inheritance_aci_body} '
                               f'userattr = "parent[0,1,2,3].seeAlso#USERDN";)',
                               f'{inheritance_aci_body} '
                               f'userattr = "parent[0,1,2,3,4].street#USERDN";)'])

    # Creating Users
    users = UserAccounts(topo.standalone, OU, rdn=None)

    for i in [['Anuj Borah', 'Sunnyvale', ROLE1, '4612'],
              ['Ananda Borah', 'Santa Clara', ROLE2, 'Its Unknown']]:
        users.create(properties={
            'uid': i[0],
            'cn': i[0].split()[0],
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + i[0].split()[0],
            'userPassword': PW_DM,
            'givenname': i[0].split()[0],
            'l': i[1],
            'mail': "anuj@borah.com",
            'telephonenumber': "+1 408 555 4798",
            'facsimiletelephonenumber': "+1 408 555 9751",
            'roomnumber': i[3],
            'Description': i[3],
            'nsRoleDN': i[2]
        })

    for demo1 in [('ROLEDNACCESS', ROLE1),
                  ('USERDNACCESS', CAN),
                  ('GROUPDNACCESS', NSSIMPLEGROUP),
                  ('ATTRNAMEACCESS', '4612'),
                  ('LDAPURLACCESS', f"ldap:///{DEFAULT_SUFFIX}??sub?(l=Sunnyvale)")]:
        users.create(properties={
            'uid': demo1[0],
            'cn': demo1[0],
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + demo1[0],
            'userPassword': PW_DM,
            'Description': demo1[1]
        })

    # Creating roles
    roles = ManagedRoles(topo.standalone, OU)
    for i in ['ROLE1', 'ROLE2']:
        roles.create(properties={"cn": i})

    # Creating Groups
    grps = Groups(topo.standalone, OU, rdn=None)
    for i in [('NSSIMPLEGROUP', CAN), ('NSSIMPLEGROUP1', CANNOT)]:
        grps.create(properties={
            'cn': i[0],
            'ou': 'groups',
            'member': i[1]
        })

    users = UserAccounts(topo.standalone, OU_2, rdn=None)
    for i in ['Grandson', 'Child', 'Parent', 'Grandparent', 'Ancestor']:
        users.create(
            properties={
                'uid': i,
                'cn': i,
                'sn': 'user',
                'uidNumber': '1000',
                'gidNumber': '2000',
                'homeDirectory': '/home/' + i,
                'userPassword': PW_DM
            })

    # Creating Other OUs
    for dn_dn in [(OU_2, 'ANCESTORS'),
                  (ANCESTORS, 'GRANDPARENTS'),
                  (GRANDPARENTS, 'PARENTS'),
                  (PARENTS, 'CHILDREN'),
                  (CHILDREN, 'GRANDSONS')]:
        OrganizationalUnits(topo.standalone, dn_dn[0]).create(properties={'ou': dn_dn[1]})


@pytest.mark.parametrize("user,entry", [
    (CAN, ROLEDNACCESS),
    (CAN, USERDNACCESS),
    (CAN, GROUPDNACCESS),
    (CAN, LDAPURLACCESS),
    (CAN, ATTRNAMEACCESS),
    (LEVEL_0, OU_2),
    (LEVEL_1, ANCESTORS),
    (LEVEL_2, GRANDPARENTS),
    (LEVEL_4, OU_2),
    (LEVEL_4, ANCESTORS),
    (LEVEL_4, GRANDPARENTS),
    (LEVEL_4, PARENTS),
    (LEVEL_4, CHILDREN),
    pytest.param(LEVEL_3, CHILDREN, marks=pytest.mark.xfail(reason="May be some bug")),
], ids=[
    "(CAN,ROLEDNACCESS)",
    "(CAN,USERDNACCESS)",
    "(CAN,GROUPDNACCESS)",
    "(CAN,LDAPURLACCESS)",
    "(CAN,ATTRNAMEACCESS)",
    "(LEVEL_0, OU_2)",
    "(LEVEL_1,ANCESTORS)",
    "(LEVEL_2,GRANDPARENTS)",
    "(LEVEL_4,OU_2)",
    "(LEVEL_4, ANCESTORS)",
    "(LEVEL_4,GRANDPARENTS)",
    "(LEVEL_4,PARENTS)",
    "(LEVEL_4,CHILDREN)",
    "(LEVEL_3, CHILDREN)"
])
def test_mod_see_also_positive(topo, _add_user, user, entry):
    """
    Try to set seeAlso on entry with binding specific user, it will success
    as per the ACI.

    :id: 65745426-7a01-11e8-8ac2-8c16451d917b
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
    conn = UserAccount(topo.standalone, user).bind(PW_DM)
    UserAccount(conn, entry).replace('seeAlso', 'cn=1')


@pytest.mark.parametrize("user,entry", [
    (CANNOT, ROLEDNACCESS),
    (CANNOT, USERDNACCESS),
    (CANNOT, GROUPDNACCESS),
    (CANNOT, LDAPURLACCESS),
    (CANNOT, ATTRNAMEACCESS),
    (LEVEL_0, ANCESTORS),
    (LEVEL_0, GRANDPARENTS),
    (LEVEL_0, PARENTS),
    (LEVEL_0, CHILDREN),
    (LEVEL_2, PARENTS),
    (LEVEL_4, GRANDSONS),
], ids=[
    "(CANNOT,ROLEDNACCESS)",
    "(CANNOT,USERDNACCESS)",
    "(CANNOT,GROUPDNACCESS)",
    "(CANNOT,LDAPURLACCESS)",
    "(CANNOT,ATTRNAMEACCESS)",
    "(LEVEL_0, ANCESTORS)",
    "(LEVEL_0,GRANDPARENTS)",
    "(LEVEL_0,PARENTS)",
    "(LEVEL_0,CHILDREN)",
    "(LEVEL_2,PARENTS)",
    "(LEVEL_4,GRANDSONS)",
])
def test_mod_see_also_negative(topo, _add_user, user, entry):
    """
    Try to set seeAlso on entry with binding specific user, it will Fail
    as per the ACI.

    :id: 9ea93252-7a01-11e8-a85b-8c16451d917b
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
    conn = UserAccount(topo.standalone, user).bind(PW_DM)
    user = UserAccount(conn, entry)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace('seeAlso', 'cn=1')


@pytest.mark.parametrize("user,entry", [
    (CANNOT, USERDNACCESS),
    (CANNOT, ROLEDNACCESS),
    (CANNOT, GROUPDNACCESS)
])
def test_last_three(topo, _add_user, user, entry):
    """
    When we use the userattr keyword to associate the entry used to bind
    with the target entry the ACI applies only to the target specified and
    not to subentries.

    :id: add58a0a-7a01-11e8-85f1-8c16451d917b
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
    conn = UserAccount(topo.standalone, user).bind(PW_DM)
    users = UserAccounts(conn, entry)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        users.create_test_user()


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
