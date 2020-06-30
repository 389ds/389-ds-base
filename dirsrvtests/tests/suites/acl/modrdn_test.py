# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

import pytest, os, ldap
from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.idm.user import UserAccount
from lib389.idm.account import Anonymous
from lib389.idm.group import Group, UniqueGroup
from lib389.idm.organizationalunit import OrganizationalUnit, OrganizationalUnits
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain

pytestmark = pytest.mark.tier1

CONTAINER_1_DELADD = "ou=Product Development,{}".format(DEFAULT_SUFFIX)
CONTAINER_2_DELADD = "ou=Accounting,{}".format(DEFAULT_SUFFIX)
USER_DELADD = "cn=Jeff Vedder,{}".format(CONTAINER_1_DELADD)
USER_WITH_ACI_DELADD = "cn=Sam Carter,{}".format(CONTAINER_2_DELADD)
DYNAMIC_MODRDN = "cn=Test DYNAMIC_MODRDN Group 70, {}".format(DEFAULT_SUFFIX)
SAM_DAMMY_MODRDN = "cn=Sam Carter1,ou=Accounting,{}".format(DEFAULT_SUFFIX)
TRAC340_MODRDN = "cn=TRAC340_MODRDN,{}".format(DEFAULT_SUFFIX)
NEWENTRY9_MODRDN = "cn=NEWENTRY9_MODRDN,{}".format("ou=People,{}".format(DEFAULT_SUFFIX))
OU0_OU_MODRDN = "ou=OU0,{}".format(DEFAULT_SUFFIX)
OU2_OU_MODRDN = "ou=OU2,{}".format(DEFAULT_SUFFIX)


@pytest.fixture(scope="function")
def aci_of_user(request, topo):
    aci_list = Domain(topo.standalone, DEFAULT_SUFFIX).get_attr_vals('aci')

    def finofaci():
        domain = Domain(topo.standalone, DEFAULT_SUFFIX)
        domain.set('aci', None)
        for i in aci_list:
            domain.add("aci", i)

    request.addfinalizer(finofaci)


@pytest.fixture(scope="function")
def _add_user(request, topo):
    ou = OrganizationalUnit(topo.standalone, 'ou=Product Development,{}'.format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'Product Development'})

    ou = OrganizationalUnit(topo.standalone, 'ou=Accounting,{}'.format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'Accounting'})

    groups = Group(topo.standalone, DYNAMIC_MODRDN)
    group_properties = {"cn": "Test DYNAMIC_MODRDN Group 70",
                        "objectclass": ["top", 'groupofURLs'],
                        'memberURL': 'ldap:///{}??base?(cn=*)'.format(USER_WITH_ACI_DELADD)}
    groups.create(properties=group_properties)

    properties = {
        'uid': 'Jeff Vedder',
        'cn': 'Jeff Vedder',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'JeffVedder',
        'userPassword': PW_DM
    }
    user = UserAccount(topo.standalone, 'cn=Jeff Vedder,ou=Product Development,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    properties = {
        'uid': 'Sam Carter',
        'cn': 'Sam Carter',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'SamCarter',
        'userPassword': PW_DM
    }
    user = UserAccount(topo.standalone, 'cn=Sam Carter,ou=Accounting,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    def fin():
        for DN in [USER_DELADD,USER_WITH_ACI_DELADD,DYNAMIC_MODRDN,CONTAINER_2_DELADD,CONTAINER_1_DELADD]:
            UserAccount(topo.standalone, DN).delete()

    request.addfinalizer(fin)


def test_allow_write_privilege_to_anyone(topo, _add_user, aci_of_user, request):
    """Modrdn Test 1 Allow write privilege to anyone

    :id: 4406f12e-7932-11e8-9dea-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",
        '(target ="ldap:///{}")(targetattr=*)(version 3.0;acl "{}";allow '
        '(write) (userdn = "ldap:///anyone");)'.format(DEFAULT_SUFFIX, request.node.name))
    conn = Anonymous(topo.standalone).bind()
    # Allow write privilege to anyone
    useraccount = UserAccount(conn, USER_WITH_ACI_DELADD)
    useraccount.rename("cn=Jeff Vedder")
    assert 'cn=Jeff Vedder,ou=Accounting,dc=example,dc=com' == useraccount.dn
    useraccount = UserAccount(conn, "cn=Jeff Vedder,ou=Accounting,dc=example,dc=com")
    useraccount.rename("cn=Sam Carter")
    assert 'cn=Sam Carter,ou=Accounting,dc=example,dc=com' == useraccount.dn


def test_allow_write_privilege_to_dynamic_group_with_scope_set_to_base_in_ldap_url(
    topo, _add_user, aci_of_user, request
):
    """Modrdn Test 2 Allow write privilege to DYNAMIC_MODRDN group with scope set to base in LDAP URL

    :id: 4c0f8c00-7932-11e8-8398-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(target = ldap:///{})(targetattr=*)(version 3.0; acl "{}"; allow(all)(groupdn = "ldap:///{}"); )'.format(DEFAULT_SUFFIX, request.node.name, DYNAMIC_MODRDN))
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # Allow write privilege to DYNAMIC_MODRDN group with scope set to base in LDAP URL
    useraccount = UserAccount(conn, USER_DELADD)
    useraccount.rename("cn=Jeffbo Vedder")
    assert 'cn=Jeffbo Vedder,ou=Product Development,dc=example,dc=com' == useraccount.dn
    useraccount = UserAccount(conn, "cn=Jeffbo Vedder,{}".format(CONTAINER_1_DELADD))
    useraccount.rename("cn=Jeff Vedder")
    assert 'cn=Jeff Vedder,ou=Product Development,dc=example,dc=com' == useraccount.dn


def test_write_access_to_naming_atributes(topo, _add_user, aci_of_user, request):
    """Test for write access to naming atributes
    Test that check for add writes to the new naming attr

    :id: 532fc630-7932-11e8-8924-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", '(target ="ldap:///{}")(targetattr != "uid")(version 3.0;acl "{}";allow (write) (userdn = "ldap:///anyone");)'.format(DEFAULT_SUFFIX, request.node.name))
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    #Test for write access to naming atributes
    useraccount = UserAccount(conn, USER_WITH_ACI_DELADD)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        useraccount.rename("uid=Jeffbo Vedder")
    

def test_write_access_to_naming_atributes_two(topo, _add_user, aci_of_user, request):
    """Test for write access to naming atributes (2)

    :id: 5a2077d2-7932-11e8-9e7b-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
        4. Now try to modrdn it to cn, won't work if request deleteoldrdn.
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
        4. Operation should  not succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", '(target ="ldap:///{}")(targetattr != "uid")(version 3.0;acl "{}";allow (write) (userdn = "ldap:///anyone");)'.format(DEFAULT_SUFFIX, request.node.name))
    properties = {
        'uid': 'Sam Carter1',
        'cn': 'Sam Carter1',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'SamCarter1'
    }
    user = UserAccount(topo.standalone, 'cn=Sam Carter1,ou=Accounting,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)
    user.set("userPassword", "password")
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # Test for write access to naming atributes
    useraccount = UserAccount(conn, SAM_DAMMY_MODRDN)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        useraccount.rename("uid=Jeffbo Vedder")
    UserAccount(topo.standalone, SAM_DAMMY_MODRDN).delete()


@pytest.mark.bz950351
def test_access_aci_list_contains_any_deny_rule(topo, _add_user, aci_of_user):
    """RHDS denies MODRDN access if ACI list contains any DENY rule
    Bug description: If you create a deny ACI for some or more attributes there is incorrect behaviour
    as you cannot rename the entry anymore

    :id: 62cbbb8a-7932-11e8-96a7-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Adding a new ou ou=People to $BASEDN
        3. Adding a user NEWENTRY9_MODRDN to ou=People,$BASEDN
        4. Adding an allow rule for NEWENTRY9_MODRDN and for others an aci deny rule
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
        4. Operation should  succeed
    """
    properties = {
        'uid': 'NEWENTRY9_MODRDN',
        'cn': 'NEWENTRY9_MODRDN_People',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'NEWENTRY9_MODRDN'
    }
    user = UserAccount(topo.standalone, 'cn=NEWENTRY9_MODRDN,ou=People,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)
    user.set("userPassword", "password")
    user.set("telephoneNumber", "989898191")
    user.set("mail", "anuj@anuj.com")
    user.set("givenName", "givenName")
    user.set("uid", "NEWENTRY9_MODRDN")
    OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX).get('People').add("aci", ['(targetattr = "*") '
        '(version 3.0;acl "admin";allow (all)(userdn = "ldap:///{}");)'.format(NEWENTRY9_MODRDN),
        '(targetattr = "mail") (version 3.0;acl "deny_mail";deny (write)(userdn = "ldap:///anyone");)',
        '(targetattr = "uid") (version 3.0;acl "allow uid";allow (write)(userdn = "ldap:///{}");)'.format(NEWENTRY9_MODRDN)])
    UserAccount(topo.standalone, NEWENTRY9_MODRDN).replace("userpassword", "Anuj")
    useraccount = UserAccount(topo.standalone, NEWENTRY9_MODRDN)
    useraccount.rename("uid=newrdnchnged")
    assert 'uid=newrdnchnged,ou=People,dc=example,dc=com' == useraccount.dn


def test_renaming_target_entry(topo, _add_user, aci_of_user):
    """Test for renaming target entry

    :id: 6be1d33a-7932-11e8-9115-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Create a test user entry
        3. Create a new ou entry with an aci
        4. Make sure uid=$MYUID has the access
        5. Rename ou=OU0 to ou=OU1
        6. Create another ou=OU2
        7. Move ou=OU1 under ou=OU2
        8. Make sure uid=$MYUID still has the access
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
        4. Operation should  succeed
        5. Operation should  succeed
        6. Operation should  succeed
        7. Operation should  succeed
        8. Operation should  succeed
    """
    properties = {
        'uid': 'TRAC340_MODRDN',
        'cn': 'TRAC340_MODRDN',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'TRAC340_MODRDN'
    }
    user = UserAccount(topo.standalone, 'cn=TRAC340_MODRDN,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)
    user.set("userPassword", "password")
    ou = OrganizationalUnit(topo.standalone, 'ou=OU0,{}'.format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'OU0'})
    ou.set('aci', '(targetattr=*)(version 3.0; acl "$MYUID";allow(read, search, compare) userdn = "ldap:///{}";)'.format(TRAC340_MODRDN))
    conn = UserAccount(topo.standalone, TRAC340_MODRDN).bind(PW_DM)
    assert OrganizationalUnits(conn, DEFAULT_SUFFIX).get('OU0')
    # Test for renaming target entry
    OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX).get('OU0').rename("ou=OU1")
    assert OrganizationalUnits(conn, DEFAULT_SUFFIX).get('OU1')
    ou = OrganizationalUnit(topo.standalone, 'ou=OU2,{}'.format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'OU2'})
    # Test for renaming target entry
    OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX).get('OU1').rename("ou=OU1", newsuperior=OU2_OU_MODRDN)
    assert OrganizationalUnits(conn, DEFAULT_SUFFIX).get('OU1')


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
