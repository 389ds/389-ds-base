# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

import pytest, os, ldap
from lib389._constants import DEFAULT_SUFFIX, PW_DM, ErrorLog
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.organization import Organization
from lib389.idm.account import Accounts, Anonymous
from lib389.idm.group import Group, UniqueGroup
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.idm.group import Groups
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain

pytestmark = pytest.mark.tier1

CONTAINER_1_DELADD = "ou=Product Development,{}".format(DEFAULT_SUFFIX)
CONTAINER_2_DELADD = "ou=Accounting,{}".format(DEFAULT_SUFFIX)
USER_ANUJ = "uid=Anuj Borah,{}".format(CONTAINER_1_DELADD)
USER_ANANDA = "uid=Ananda Borah,{}".format(CONTAINER_2_DELADD)


@pytest.fixture(scope="function")
def aci_of_user(request, topo):
    aci_list = Domain(topo.standalone, DEFAULT_SUFFIX).get_attr_vals('aci')

    def finofaci():
        domain = Domain(topo.standalone, DEFAULT_SUFFIX)
        domain.set('aci', None)
        for i in aci_list:
            domain.add("aci", i)

    request.addfinalizer(finofaci)
    

@pytest.fixture(scope="module")
def test_uer(request, topo):
    topo.standalone.config.loglevel((ErrorLog.ACL_SUMMARY,))

    for i in ['Product Development', 'Accounting']:
        OrganizationalUnit(topo.standalone, "ou={},{}".format(i, DEFAULT_SUFFIX)).create(properties={'ou': i})

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=Product Development')
    users.create(properties={
        'uid': 'Anuj Borah',
        'cn': 'Anuj Borah',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'AnujBorah',
        'userPassword': PW_DM
    })

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=Accounting')
    users.create(properties={
        'uid': 'Ananda Borah',
        'cn': 'Ananda Borah',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'AnandaBorah',
        'userPassword': PW_DM
    })


def test_deny_search_access_to_userdn_with_ldap_url(topo, test_uer, aci_of_user):
    """Search Test 23 Deny search access to userdn with LDAP URL

    :id: 94f082d8-6e12-11e8-be72-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Add Entry
        2. Add ACI
        3. Bind with test USER_ANUJ
        4. Try search
        5. Delete Entry,test USER_ANUJ, ACI
    :expectedresults:
        1. Operation should success
        2. Operation should success
        3. Operation should success
        4. Operation should Fail
        5. Operation should success
    """
    ACI_TARGET = "(target = ldap:///{})(targetattr=*)".format(DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny (search)'
    ACI_SUBJECT = (
        'userdn="ldap:///%s";)' % "{}??sub?(&(roomnumber=3445))".format(DEFAULT_SUFFIX)
    )
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    UserAccount(topo.standalone, USER_ANANDA).set('roomnumber', '3445')
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block all users having roomnumber=3445
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block roomnumber=3445 for all users USER_ANUJ does not have roomnumber
    assert 2 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    UserAccount(topo.standalone, USER_ANANDA).remove('roomnumber', '3445')


def test_deny_search_access_to_userdn_with_ldap_url_two(topo, test_uer, aci_of_user):
    """Search Test 24 Deny search access to != userdn with LDAP URL

    :id: a1ee05d2-6e12-11e8-8260-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Add Entry
        2. Add ACI
        3. Bind with test USER_ANUJ
        4. Try search
        5. Delete Entry,test USER_ANUJ, ACI
    :expectedresults:
        1. Operation should success
        2. Operation should success
        3. Operation should success
        4. Operation should Fail
        5. Operation should success
    """
    ACI_TARGET = "(target = ldap:///{})(targetattr=*)".format(DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny (search)'
    ACI_SUBJECT = (
        'userdn != "ldap:///%s";)' % "{}??sub?(&(roomnumber=3445))".format(DEFAULT_SUFFIX)
    )
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    UserAccount(topo.standalone, USER_ANANDA).set('roomnumber', '3445')
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will not block all users having roomnumber=3445 , it will block others
    assert 2 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will not block all users having roomnumber=3445 , it will block others
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    UserAccount(topo.standalone, USER_ANANDA).remove('roomnumber', '3445')


def test_deny_search_access_to_userdn_with_ldap_url_matching_all_users(
    topo, test_uer, aci_of_user
):
    """Search Test 25 Deny search access to userdn with LDAP URL matching all users

    :id: b37f72ae-6e12-11e8-9c98-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Add Entry
        2. Add ACI
        3. Bind with test USER_ANUJ
        4. Try search
        5. Delete Entry,test USER_ANUJ, ACI
    :expectedresults:
        1. Operation should success
        2. Operation should success
        3. Operation should success
        4. Operation should Fail
        5. Operation should success
    """
    ACI_TARGET = "(target = ldap:///{})(targetattr=*)".format(DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny (search)'
    ACI_SUBJECT = 'userdn = "ldap:///%s";)' % "{}??sub?(&(cn=*))".format(DEFAULT_SUFFIX)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will  block all users LDAP URL matching all users
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will  block all users LDAP URL matching all users
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    assert 2 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))


def test_deny_read_access_to_a_dynamic_group(topo, test_uer, aci_of_user):
    """Search Test 26 Deny read access to a dynamic group

    :id: c0c5290e-6e12-11e8-a900-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Add Entry
        2. Add ACI
        3. Bind with test USER_ANUJ
        4. Try search
        5. Delete Entry,test USER_ANUJ, ACI
    :expectedresults:
        1. Operation should success
        2. Operation should success
        3. Operation should success
        4. Operation should Fail
        5. Operation should success
    """
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group_properties = {"cn": "group1", "description": "testgroup"}
    group = groups.create(properties=group_properties)
    group.add('objectClass', 'groupOfURLS')
    group.set('memberURL', "ldap:///{}??sub?(&(ou=Accounting)(cn=Sam*))".format(DEFAULT_SUFFIX))
    group.add_member(USER_ANANDA)

    ACI_TARGET = '(target = ldap:///{})(targetattr = "*")'.format(DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "All rights for %s"; deny(read)' % "Unknown"
    ACI_SUBJECT = 'groupdn = "ldap:///cn=group1,ou=Groups,{}";)'.format(DEFAULT_SUFFIX)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block all 'memberURL', "ldap:///{}??sub?(&(ou=Accounting)(cn=Sam*))".format(DEFAULT_SUFFIX)
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # USER_ANUJ is not a member
    assert 2 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    group.delete()


def test_deny_read_access_to_dynamic_group_with_host_port_set_on_ldap_url(
    topo, test_uer, aci_of_user
):
    """Search Test 27 Deny read access to dynamic group with host:port set on LDAP URL

    :id: ceb62158-6e12-11e8-8c36-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Add Entry
        2. Add ACI
        3. Bind with test USER_ANUJ
        4. Try search
        5. Delete Entry,test USER_ANUJ, ACI
    :expectedresults:
        1. Operation should success
        2. Operation should success
        3. Operation should success
        4. Operation should Fail
        5. Operation should success
    """
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={"cn": "group1",
                                      "description": "testgroup"
                                      })
    group.add('objectClass', 'groupOfURLS')
    group.set('memberURL', "ldap:///localhost:38901/{}??sub?(&(ou=Accounting)(cn=Sam*))".format(DEFAULT_SUFFIX))
    group.add_member(USER_ANANDA)

    ACI_TARGET = '(target = ldap:///{})(targetattr = "*")'.format(DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "All rights for %s"; deny(read)' % "Unknown"
    ACI_SUBJECT = 'groupdn = "ldap:///cn=group1,ou=Groups,{}";)'.format(DEFAULT_SUFFIX)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block 'memberURL', "ldap:///localhost:38901/dc=example,dc=com??sub?(&(ou=Accounting)(cn=Sam*))"
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    assert 2 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))
    group.delete()


def test_deny_read_access_to_dynamic_group_with_scope_set_to_one_in_ldap_url(
    topo, test_uer, aci_of_user
):
    """Search Test 28 Deny read access to dynamic group with scope set to "one" in LDAP URL

    :id: ddb30432-6e12-11e8-94db-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Add Entry
        2. Add ACI
        3. Bind with test USER_ANUJ
        4. Try search
        5. Delete Entry,test USER_ANUJ, ACI
    :expectedresults:
        1. Operation should success
        2. Operation should success
        3. Operation should success
        4. Operation should Fail
        5. Operation should success
    """
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={"cn": "group1",
                                      "description": "testgroup"
                                      })
    group.add('objectClass', 'groupOfURLS')
    group.set('memberURL', "ldap:///{}??sub?(&(ou=Accounting)(cn=Sam*))".format(DEFAULT_SUFFIX))
    group.add_member(USER_ANANDA)

    ACI_TARGET = '(targetattr = "*")'
    ACI_ALLOW = '(version 3.0; acl "All rights for %s"; deny(read) ' % "Unknown"
    ACI_SUBJECT = 'groupdn != "ldap:///cn=group1,ou=Groups,{}";)'.format(DEFAULT_SUFFIX)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will allow only 'memberURL', "ldap:///{dc=example,dc=com??sub?(&(ou=Accounting)(cn=Sam*))"
    assert 2 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will allow only 'memberURL', "ldap:///{dc=example,dc=com??sub?(&(ou=Accounting)(cn=Sam*))"
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    group.delete()


def test_deny_read_access_to_dynamic_group_two(topo, test_uer, aci_of_user):
    """Search Test 29 Deny read access to != dynamic group

    :id: eae2a6c6-6e12-11e8-80f3-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Add Entry
        2. Add ACI
        3. Bind with test USER_ANUJ
        4. Try search
        5. Delete Entry,test USER_ANUJ, ACI
    :expectedresults:
        1. Operation should success
        2. Operation should success
        3. Operation should success
        4. Operation should Fail
        5. Operation should success
    """
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group_properties = {"cn": "group1",
                        "description": "testgroup"
                        }
    group = groups.create(properties=group_properties)
    group.add('objectClass', 'groupofuniquenames')
    group.set('uniquemember', [USER_ANANDA,USER_ANUJ])

    ACI_TARGET = '(targetattr = "*")'
    ACI_ALLOW = '(version 3.0; acl "All rights for %s"; deny(read) ' % "Unknown"
    ACI_SUBJECT = 'groupdn = "ldap:///cn=group1,ou=Groups,{}";)'.format(DEFAULT_SUFFIX)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block groupdn = "ldap:///cn=group1,ou=Groups,dc=example,dc=com";)
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block groupdn = "ldap:///cn=group1,ou=Groups,dc=example,dc=com";)
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    assert 2 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))
    group.delete()


def test_deny_access_to_group_should_deny_access_to_all_uniquemember(
    topo, test_uer, aci_of_user, request
):
    """Search Test 38 Deny access to group should deny access to all uniquemember (including chain group)

    :id: 56b470e4-7941-11e8-912b-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Add Entry
        2. Add ACI
        3. Bind with test USER_ANUJ
        4. Try search
        5. Delete Entry,test USER_ANUJ, ACI
    :expectedresults:
        1. Operation should success
        2. Operation should success
        3. Operation should success
        4. Operation should Fail
        5. Operation should success
    """

    grp = UniqueGroup(topo.standalone, 'cn=Nested Group 1,' + DEFAULT_SUFFIX)
    grp.create(properties={
        'cn': 'Nested Group 1',
        'ou': 'groups',
        'uniquemember': "cn=Nested Group 2, {}".format(DEFAULT_SUFFIX)
    })

    grp = UniqueGroup(topo.standalone, 'cn=Nested Group 2,' + DEFAULT_SUFFIX)
    grp.create(properties={
        'cn': 'Nested Group 2',
        'ou': 'groups',
        'uniquemember': "cn=Nested Group 3, {}".format(DEFAULT_SUFFIX)
    })

    grp = UniqueGroup(topo.standalone, 'cn=Nested Group 3,' + DEFAULT_SUFFIX)
    grp.create(properties={
        'cn': 'Nested Group 3',
        'ou': 'groups',
        'uniquemember': [USER_ANANDA, USER_ANUJ]
    })

    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", '(target = ldap:///{})(targetattr=*)'
    '(version 3.0; acl "{}"; deny(read)(groupdn = "ldap:///cn=Nested Group 1, {}"); )'.format(DEFAULT_SUFFIX, request.node.name, DEFAULT_SUFFIX))
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # deny_access_to_group_should_deny_access_to_all_uniquemember
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # deny_access_to_group_should_deny_access_to_all_uniquemember
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    assert 2 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))


def test_entry_with_lots_100_attributes(topo, test_uer, aci_of_user):
    """Search Test 39 entry with lots (>100) attributes

    :id: fc155f74-6e12-11e8-96ac-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Add Entry
        2. Bind with test USER_ANUJ
        3. Try search
        4. Delete Entry,test USER_ANUJ, ACI
    :expectedresults:
        1. Operation should success
        3. Operation should success
        4. Operation should success
        5. Operation should success
    """
    for i in range(100):
        user = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=People').create_test_user(uid=i)
        user.set("userPassword", "password")

    conn = UserAccount(topo.standalone, "uid=test_user_1,ou=People,{}".format(DEFAULT_SUFFIX)).bind(PW_DM)
    # no aci no blockage
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=Anuj*)'))
    # no aci no blockage
    assert 102 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(uid=*)'))
    conn = Anonymous(topo.standalone).bind()
    # anonymous_search_on_monitor_entry
    assert 102 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(uid=*)'))


@pytest.mark.bz301798
def test_groupdnattr_value_is_another_group(topo):
    """Search Test 42 groupdnattr value is another group test #1

    :id: 52299e16-7944-11e8-b471-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI
        3. USER_ANUJ should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    Organization(topo.standalone).create(properties={"o": "nscpRoot"}, basedn=DEFAULT_SUFFIX)

    user = UserAccount(topo.standalone, "cn=dchan,o=nscpRoot,{}".format(DEFAULT_SUFFIX))
    user.create(properties={
        'uid': 'dchan',
        'cn': 'dchan',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'dchan',
        'userPassword': PW_DM
    })

    grp = UniqueGroup(topo.standalone, 'cn=groupx,o=nscpRoot,' + DEFAULT_SUFFIX)
    grp.create(properties={
        'cn': 'groupx',
        'ou': 'groups',
    })
    grp.set('uniquemember', 'cn=dchan,o=nscpRoot,{}'.format(DEFAULT_SUFFIX))
    grp.set('aci', '(targetattr="*")(version 3.0; acl "Enable Group Expansion"; allow (read, search, compare) groupdnattr="ldap:///o=nscpRoot?uniquemember?sub";)')

    conn = UserAccount(topo.standalone, 'cn=dchan,o=nscpRoot,{}'.format(DEFAULT_SUFFIX),).bind(PW_DM)
    # acil will allow ldap:///o=nscpRoot?uniquemember?sub"
    assert UserAccount(conn, 'cn=groupx,o=nscpRoot,{}'.format(DEFAULT_SUFFIX)).get_attr_val_utf8('cn') == 'groupx'


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
