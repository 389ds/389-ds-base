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
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.idm.group import Groups
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain

pytestmark = pytest.mark.tier1

CONTAINER_1_DELADD = "ou=Product Development,{}".format(DEFAULT_SUFFIX)
CONTAINER_2_DELADD = "ou=Accounting,{}".format(DEFAULT_SUFFIX)
USER_DELADD = "cn=Jeff Vedder,{}".format(CONTAINER_1_DELADD)
USER_WITH_ACI_DELADD = "cn=Sam Carter,{}".format(CONTAINER_2_DELADD)
KIRSTENVAUGHAN = "cn=Kirsten Vaughan, ou=Human Resources, {}".format(DEFAULT_SUFFIX)
HUMAN_OU_GLOBAL = "ou=Human Resources,{}".format(DEFAULT_SUFFIX)


@pytest.fixture(scope="function")
def cleanup_tree(request, topo):

    def fin():
        for i in [USER_DELADD, USER_WITH_ACI_DELADD, KIRSTENVAUGHAN, CONTAINER_1_DELADD, CONTAINER_2_DELADD, HUMAN_OU_GLOBAL]:
            try:
                UserAccount(topo.standalone, i).delete()
            except:
                pass

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def aci_of_user(request, topo):
    aci_list = Domain(topo.standalone, DEFAULT_SUFFIX).get_attr_vals('aci')

    def finofaci():
        domain = Domain(topo.standalone, DEFAULT_SUFFIX)
        domain.set('aci', None)
        for i in aci_list:
            domain.add("aci", i)

    request.addfinalizer(finofaci)


def test_allow_write_access_to_targetattr_with_a_single_attribute(
        topo, aci_of_user, cleanup_tree):
    """Modify Test 1 Allow write access to targetattr with a single attribute

    :id: 620d7b82-7abf-11e8-a4db-8c16451d917b
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
    ACI_BODY = '(targetattr = "title")(version 3.0; acl "ACI NAME"; allow (write) (userdn = "ldap:///anyone") ;)'
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)

    ou = OrganizationalUnit(topo.standalone, "ou=Product Development,{}".format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'Product Development'})

    properties = {
            'uid': 'Jeff Vedder',
            'cn': 'Jeff Vedder',
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + 'JeffVedder',
            'userPassword': PW_DM
        }
    user = UserAccount(topo.standalone, "cn=Jeff Vedder,ou=Product Development,{}".format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    # Allow write access to targetattr with a single attribute
    conn = Anonymous(topo.standalone).bind()
    ua = UserAccount(conn, USER_DELADD)
    ua.add("title", "Architect")
    assert ua.get_attr_val('title')
    ua.remove("title", "Architect")


def test_allow_write_access_to_targetattr_with_multiple_attibutes(
        topo, aci_of_user, cleanup_tree):
    """Modify Test 2 Allow write access to targetattr with multiple attibutes

    :id: 6b9f05c6-7abf-11e8-9ba1-8c16451d917b
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
    ACI_BODY = '(targetattr = "telephonenumber || roomnumber")(version 3.0; acl "ACI NAME"; allow (write) (userdn = "ldap:///anyone") ;)'
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)

    ou = OrganizationalUnit(topo.standalone, "ou=Product Development,{}".format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'Product Development'})

    properties = {
        'uid': 'Jeff Vedder',
        'cn': 'Jeff Vedder',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'JeffVedder',
        'userPassword': PW_DM
    }
    user = UserAccount(topo.standalone, "cn=Jeff Vedder,ou=Product Development,{}".format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    # Allow write access to targetattr with multiple attibutes
    conn = Anonymous(topo.standalone).bind()
    ua = UserAccount(conn, USER_DELADD)
    ua.add("telephonenumber", "+1 408 555 1212")
    assert ua.get_attr_val('telephonenumber')
    ua.add("roomnumber", "101")
    assert ua.get_attr_val('roomnumber')


def test_allow_write_access_to_userdn_all(topo, aci_of_user, cleanup_tree):
    """Modify Test 3 Allow write access to userdn 'all'

    :id: 70c58818-7abf-11e8-afa1-8c16451d917b
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
    ACI_BODY = '(targetattr = "*")(version 3.0; acl "ACI NAME"; allow (write) (userdn = "ldap:///all") ;)'
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)

    for i in ['Product Development', 'Accounting']:
        ou = OrganizationalUnit(topo.standalone, "ou={},{}".format(i, DEFAULT_SUFFIX))
        ou.create(properties={'ou': i})

    for i in ['Jeff Vedder,ou=Product Development', 'Sam Carter,ou=Accounting']:
        properties = {
            'uid': i,
            'cn': i,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + i,
            'userPassword': PW_DM
        }
        user = UserAccount(topo.standalone, "cn={},{}".format(i, DEFAULT_SUFFIX))
        user.create(properties=properties)

    # Allow write access to userdn 'all'
    conn = Anonymous(topo.standalone).bind()
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        UserAccount(conn, USER_DELADD).add("title", "Architect")
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    UserAccount(conn, USER_DELADD).add("title", "Architect")
    assert UserAccount(conn, USER_DELADD).get_attr_val('title')


def test_allow_write_access_to_userdn_with_wildcards_in_dn(
        topo, aci_of_user, cleanup_tree):
    """Modify Test 4 Allow write access to userdn with wildcards in DN

    :id: 766c2312-7abf-11e8-b57d-8c16451d917b
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
    ACI_BODY = '(targetattr = "*")(version 3.0; acl "ACI NAME"; allow (write)(userdn = "ldap:///cn=*, ou=Product Development,{}") ;)'.format(DEFAULT_SUFFIX)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)

    ou = OrganizationalUnit(topo.standalone, "ou=Product Development,{}".format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'Product Development'})

    properties = {
        'uid': 'Jeff Vedder',
        'cn': 'Jeff Vedder',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'JeffVedder',
        'userPassword': PW_DM
    }
    user = UserAccount(topo.standalone, "cn=Jeff Vedder,ou=Product Development,{}".format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    conn = UserAccount(topo.standalone, USER_DELADD).bind(PW_DM)
    # Allow write access to userdn with wildcards in DN
    ua = UserAccount(conn, USER_DELADD)
    ua.add("title", "Architect")
    assert ua.get_attr_val('title')


def test_allow_write_access_to_userdn_with_multiple_dns(topo, aci_of_user, cleanup_tree):
    """Modify Test 5 Allow write access to userdn with multiple DNs

    :id: 7aae760a-7abf-11e8-bc3a-8c16451d917b
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
    ACI_BODY = '(targetattr = "*")(version 3.0; acl "ACI NAME"; allow (write)(userdn = "ldap:///{} || ldap:///{}") ;)'.format(USER_DELADD, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)

    for i in ['Product Development', 'Accounting', 'Human Resources']:
        ou = OrganizationalUnit(topo.standalone, "ou={},{}".format(i, DEFAULT_SUFFIX))
        ou.create(properties={'ou': i})

    for i in ['Jeff Vedder,ou=Product Development', 'Sam Carter,ou=Accounting', 'Kirsten Vaughan, ou=Human Resources']:
        properties = {
            'uid': i,
            'cn': i,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + i,
            'userPassword': PW_DM
        }
        user = UserAccount(topo.standalone, "cn={},{}".format(i, DEFAULT_SUFFIX))
        user.create(properties=properties)

    conn = UserAccount(topo.standalone, USER_DELADD).bind(PW_DM)
    # Allow write access to userdn with multiple DNs
    ua = UserAccount(conn, KIRSTENVAUGHAN)
    ua.add("title", "Architect")
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # Allow write access to userdn with multiple DNs
    ua = UserAccount(conn, USER_DELADD)
    ua.add("title", "Architect")
    assert ua.get_attr_val('title')
    

def test_allow_write_access_to_target_with_wildcards(topo, aci_of_user, cleanup_tree):
    """Modify Test 6 Allow write access to target with wildcards

    :id: 825fe884-7abf-11e8-8541-8c16451d917b
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
    ACI_BODY = '(target = ldap:///{})(targetattr = "*")(version 3.0; acl "ACI NAME"; allow (write) (userdn = "ldap:///anyone") ;)'.format(DEFAULT_SUFFIX)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)

    for i in ['Product Development', 'Accounting', 'Human Resources']:
        ou = OrganizationalUnit(topo.standalone, "ou={},{}".format(i, DEFAULT_SUFFIX))
        ou.create(properties={'ou': i})

    for i in ['Jeff Vedder,ou=Product Development', 'Sam Carter,ou=Accounting', 'Kirsten Vaughan, ou=Human Resources']:
        properties = {
            'uid': i,
            'cn': i,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + i,
            'userPassword': PW_DM
        }
        user = UserAccount(topo.standalone, "cn={},{}".format(i, DEFAULT_SUFFIX))
        user.create(properties=properties)

    conn = UserAccount(topo.standalone, USER_DELADD).bind(PW_DM)
    # Allow write access to target with wildcards
    ua = UserAccount(conn, KIRSTENVAUGHAN)
    ua.add("title", "Architect")
    assert ua.get_attr_val('title')
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # Allow write access to target with wildcards
    ua = UserAccount(conn, USER_DELADD)
    ua.add("title", "Architect")
    assert ua.get_attr_val('title')


def test_allow_write_access_to_userdnattr(topo, aci_of_user, cleanup_tree, request):
    """Modify Test 7 Allow write access to userdnattr

    :id: 86b418f6-7abf-11e8-ae28-8c16451d917b
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
    ACI_BODY = '(target = ldap:///{})(targetattr=*)(version 3.0; acl "{}";allow (write) (userdn = "ldap:///anyone"); )'.format(DEFAULT_SUFFIX, request.node.name)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)

    for i in ['Product Development', 'Accounting']:
        ou = OrganizationalUnit(topo.standalone, "ou={},{}".format(i, DEFAULT_SUFFIX))
        ou.create(properties={'ou': i})

    for i in ['Jeff Vedder,ou=Product Development', 'Sam Carter,ou=Accounting']:
        properties = {
            'uid': i,
            'cn': i,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + i,
            'userPassword': PW_DM
        }
        user = UserAccount(topo.standalone, "cn={},{}".format(i, DEFAULT_SUFFIX))
        user.create(properties=properties)

    UserAccount(topo.standalone, USER_WITH_ACI_DELADD).add('manager', USER_WITH_ACI_DELADD)
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # Allow write access to userdnattr
    ua = UserAccount(conn, USER_DELADD)
    ua.add('uid', 'scoobie')
    assert ua.get_attr_val('uid')
    ua.add('uid', 'jvedder')
    assert ua.get_attr_val('uid')


def test_allow_selfwrite_access_to_anyone(topo, aci_of_user, cleanup_tree):
    """Modify Test 8 Allow selfwrite access to anyone

    :id: 8b3becf0-7abf-11e8-ac34-8c16451d917b
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
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={"cn": "group1",
                        "description": "testgroup"})

    ACI_BODY = '(target = ldap:///cn=group1,ou=Groups,{})(targetattr = "member")(version 3.0; acl "ACI NAME"; allow (selfwrite) (userdn = "ldap:///anyone") ;)'.format(DEFAULT_SUFFIX)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)

    ou = OrganizationalUnit(topo.standalone, "ou=Product Development,{}".format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'Product Development'})

    properties = {
        'uid': 'Jeff Vedder',
        'cn': 'Jeff Vedder',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'JeffVedder',
        'userPassword': PW_DM
    }
    user = UserAccount(topo.standalone, "cn=Jeff Vedder,ou=Product Development,{}".format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    conn = UserAccount(topo.standalone, USER_DELADD).bind(PW_DM)
    # Allow selfwrite access to anyone
    groups = Groups(conn, DEFAULT_SUFFIX)
    groups.list()[0].add_member(USER_DELADD)
    group.delete()


def test_uniquemember_should_also_be_the_owner(topo,  aci_of_user):
    """Modify Test 10 groupdnattr = \"ldap:///$BASEDN?owner\" if owner is a group, group's
    uniquemember should also be the owner

    :id: 9456b2d4-7abf-11e8-829d-8c16451d917b
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
    for i in ['ACLGroupTest']:
        ou = OrganizationalUnit(topo.standalone, "ou={},{}".format(i, DEFAULT_SUFFIX))
        ou.create(properties={'ou': i})

    ou = OrganizationalUnit(topo.standalone, "ou=ACLDevelopment,{}".format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'ACLDevelopment'})
    ou.set('aci','(targetattr="*")(version 3.0; acl "groupdnattr acl"; '
                 'allow (all)groupdnattr = "ldap:///{}?owner";)'.format(DEFAULT_SUFFIX))

    grp = UniqueGroup(topo.standalone, "uid=anuj,ou=ACLDevelopment, {}".format(DEFAULT_SUFFIX))
    user_props = (
        {'sn': 'Borah',
         'cn': 'Anuj',
         'objectclass': ['top', 'person', 'organizationalPerson', 'inetOrgPerson', 'groupofUniquenames'],
         'userpassword': PW_DM,
         'givenname': 'Anuj',
         'ou': ['ACLDevelopment', 'People'],
         'roomnumber': '123',
         'uniquemember': 'cn=mandatory member'
         }
    )
    grp.create(properties=user_props)

    grp = UniqueGroup(topo.standalone, "uid=2ishani,ou=ACLDevelopment, {}".format(DEFAULT_SUFFIX))
    user_props = (
        {'sn': 'Borah',
         'cn': '2ishani',
         'objectclass': ['top', 'person','organizationalPerson', 'inetOrgPerson', 'groupofUniquenames'],
         'userpassword': PW_DM,
         'givenname': '2ishani',
         'ou': ['ACLDevelopment', 'People'],
         'roomnumber': '1234',
         'uniquemember': 'cn=mandatory member', "owner": "cn=group4, ou=ACLGroupTest, {}".format(DEFAULT_SUFFIX)
         }
    )
    grp.create(properties=user_props)

    grp = UniqueGroup(topo.standalone, 'cn=group1,ou=ACLGroupTest,'+DEFAULT_SUFFIX)
    grp.create(properties={'cn': 'group1',
                           'ou': 'groups'})
    grp.set('uniquemember', ["cn=group2, ou=ACLGroupTest, {}".format(DEFAULT_SUFFIX),
                             "cn=group3, ou=ACLGroupTest, {}".format(DEFAULT_SUFFIX)])

    grp = UniqueGroup(topo.standalone, 'cn=group3,ou=ACLGroupTest,' + DEFAULT_SUFFIX)
    grp.create(properties={'cn': 'group3',
                           'ou': 'groups'})
    grp.set('uniquemember', ["cn=group4, ou=ACLGroupTest, {}".format(DEFAULT_SUFFIX)])

    grp = UniqueGroup(topo.standalone, 'cn=group4,ou=ACLGroupTest,' + DEFAULT_SUFFIX)
    grp.create(properties={
        'cn': 'group4',
        'ou': 'groups'})
    grp.set('uniquemember', ["uid=anuj, ou=ACLDevelopment, {}".format(DEFAULT_SUFFIX)])

    #uniquemember should also be the owner
    conn = UserAccount(topo.standalone, "uid=anuj,ou=ACLDevelopment, {}".format(DEFAULT_SUFFIX)).bind(PW_DM)
    ua = UserAccount(conn, "uid=2ishani, ou=ACLDevelopment, {}".format(DEFAULT_SUFFIX))
    ua.add('roomnumber', '9999')
    assert ua.get_attr_val('roomnumber')

    for DN in ["cn=group4,ou=ACLGroupTest,{}".format(DEFAULT_SUFFIX),
               "cn=group3,ou=ACLGroupTest,{}".format(DEFAULT_SUFFIX),
               "cn=group1,ou=ACLGroupTest,{}".format(DEFAULT_SUFFIX),
               "uid=2ishani,ou=ACLDevelopment,{}".format(DEFAULT_SUFFIX),
               "uid=anuj,ou=ACLDevelopment,{}".format(DEFAULT_SUFFIX), "ou=ACLDevelopment,{}".format(DEFAULT_SUFFIX),
               "ou=ACLGroupTest, {}".format(DEFAULT_SUFFIX)]:
        UserAccount(topo.standalone, DN).delete()


def test_aci_with_both_allow_and_deny(topo, aci_of_user, cleanup_tree):
    """Modify Test 12 aci with both allow and deny

    :id: 9dcfe902-7abf-11e8-86dc-8c16451d917b
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
    ACI_BODY = '(targetattr = "*")(version 3.0; acl "ACI NAME"; deny (read, search)userdn = "ldap:///{}"; allow (all) userdn = "ldap:///{}" ;)'.format(USER_WITH_ACI_DELADD, USER_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)

    for i in ['Product Development', 'Accounting']:
        ou = OrganizationalUnit(topo.standalone, "ou={},{}".format(i, DEFAULT_SUFFIX))
        ou.create(properties={'ou': i})

    for i in ['Jeff Vedder,ou=Product Development', 'Sam Carter,ou=Accounting']:
        properties = {
            'uid': i,
            'cn': i,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + i,
            'userPassword': PW_DM
        }
        user = UserAccount(topo.standalone, "cn={},{}".format(i, DEFAULT_SUFFIX))
        user.create(properties=properties)

    conn = UserAccount(topo.standalone, USER_DELADD).bind(PW_DM)
    # aci with both allow and deny, testing allow
    assert UserAccount(conn, USER_WITH_ACI_DELADD).get_attr_val('uid')
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # aci with both allow and deny, testing deny
    with pytest.raises(IndexError):
        UserAccount(conn, USER_WITH_ACI_DELADD).get_attr_val('uid')


def test_allow_owner_to_modify_entry(topo, aci_of_user, cleanup_tree, request):
    """Modify Test 14 allow userdnattr = owner to modify entry

    :id: aa302090-7abf-11e8-811a-8c16451d917b
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
    grp = UniqueGroup(topo.standalone, 'cn=intranet,' + DEFAULT_SUFFIX)
    grp.create(properties={
        'cn': 'intranet',
        'ou': 'groups'})
    grp.set('owner', USER_WITH_ACI_DELADD)

    ACI_BODY = '(target ="ldap:///cn=intranet, {}") (targetattr ="*")(targetfilter ="(objectclass=groupOfUniqueNames)") (version 3.0;acl "{}";allow(read, write, delete, search, compare, add) (userdnattr = "owner");)'.format(DEFAULT_SUFFIX, request.node.name)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)

    for i in ['Product Development', 'Accounting']:
        ou = OrganizationalUnit(topo.standalone, "ou={},{}".format(i, DEFAULT_SUFFIX))
        ou.create(properties={'ou': i})
    for i in ['Jeff Vedder,ou=Product Development', 'Sam Carter,ou=Accounting']:
        properties = {
            'uid': i,
            'cn': i,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + i,
            'userPassword': PW_DM
        }
        user = UserAccount(topo.standalone, "cn={},{}".format(i, DEFAULT_SUFFIX))
        user.create(properties=properties)

    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # allow userdnattr = owner to modify entry
    ua = UserAccount(conn, 'cn=intranet,dc=example,dc=com')
    ua.set('uniquemember', "cn=Andy Walker, ou=Accounting,dc=example,dc=com")
    assert ua.get_attr_val('uniquemember')


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
