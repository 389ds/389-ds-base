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
from lib389.idm.account import Accounts
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.idm.group import Groups
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain
from lib389.idm.posixgroup import PosixGroups

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


def test_deny_all_access_with_target_set(topo, test_uer, aci_of_user):
    """Test that Deny all access with target set

    :id: 0550e680-6e0e-11e8-82f4-8c16451d917b
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
    ACI_TARGET = "(target = ldap:///{})(targetattr=*)".format(USER_ANANDA)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block all for all usrs
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=Ananda*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block all for all usrs
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=Ananda*)'))
    # with root there is no aci blockage
    assert 1 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=Ananda*)'))


def test_deny_all_access_to_a_target_with_wild_card(topo, test_uer, aci_of_user):
    """Search Test 2 Deny all access to a target with wild card

    :id: 1c370f98-6e11-11e8-9f10-8c16451d917b
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
    ACI_TARGET = "(target = ldap:///uid=Ananda*, ou=*,{})(targetattr=*)".format(
        DEFAULT_SUFFIX
    )
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block (cn=Sam*) for all usrs
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=Ananda*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block (cn=Sam*) for all usrs
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=Ananda*)'))
    # with root there is no aci blockage
    assert 1 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=Ananda*)'))


def test_deny_all_access_without_a_target_set(topo, test_uer, aci_of_user):
    """Search Test 3 Deny all access without a target set

    :id: 2dbeb36a-6e11-11e8-ab9f-8c16451d917b
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
    ACI_TARGET = "(targetattr=*)"
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block all for all usrs
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(ou=Accounting)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block all for all usrs
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(ou=Accounting)'))
    # with root there is no aci blockage
    assert 1 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(ou=Accounting)'))


def test_deny_read_search_and_compare_access_with_target_and_targetattr_set(
    topo, test_uer, aci_of_user
):
    """Search Test 4 Deny read, search and compare access with target and targetattr set

    :id: 3f4a87e4-6e11-11e8-a09f-8c16451d917b
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
    ACI_TARGET = "(target = ldap:///{})(targetattr=*)".format(CONTAINER_2_DELADD)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block all for all usrs
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(ou=Accounting)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block all for all usrs
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(ou=Accounting)'))
    # with root there is no aci blockage
    assert 1 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(ou=Accounting)'))


def test_deny_read_access_to_multiple_groupdns(topo, test_uer, aci_of_user):
    """Search Test 6 Deny read access to multiple groupdn's

    :id: 8f3ba440-6e11-11e8-8b20-8c16451d917b
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
    group.add_member(USER_ANANDA)

    posix_groups = PosixGroups(topo.standalone, DEFAULT_SUFFIX)
    posix_group = posix_groups.create(properties={
        "cn": "group2",
        "description": "testgroup2",
        "gidNumber": "2000",
    })
    posix_group.add_member(USER_ANUJ)

    ACI_TARGET = '(targetattr="*")'
    ACI_ALLOW = '(version 3.0; acl "All rights for cn=group1,ou=Groups,{}"; deny(read)'.format(DEFAULT_SUFFIX)
    ACI_SUBJECT = 'groupdn="ldap:///cn=group1,ou=Groups,{}||ldap:///cn=group2,ou=Groups,{}";)'.format(DEFAULT_SUFFIX, DEFAULT_SUFFIX)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block 'groupdn="ldap:///cn=group1,ou=Groups,dc=example,dc=com||ldap:///cn=group2,ou=Groups,dc=example,dc=com";)
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block 'groupdn="ldap:///cn=group1,ou=Groups,dc=example,dc=com||ldap:///cn=group2,ou=Groups,dc=example,dc=com";)
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    assert 3 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))
    group = groups.get("group1")
    group.delete()
    posix_groups.get("group2")
    posix_group.delete()


def test_deny_all_access_to_userdnattr(topo, test_uer, aci_of_user):
    """Search Test 7 Deny all access to userdnattr"

    :id: ae482494-6e11-11e8-ae33-8c16451d917b
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
    UserAccount(topo.standalone, USER_ANUJ).add('manager', USER_ANANDA)
    ACI_TARGET = "(target = ldap:///{})(targetattr=*)".format(DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdnattr="manager";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block only 'userdnattr="manager"
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=Anuj Borah)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block only 'userdnattr="manager"
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=Anuj Borah)'))
    # with root there is no aci blockage
    assert 1 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=Anuj Borah)'))
    UserAccount(topo.standalone, USER_ANUJ).remove('manager', USER_ANANDA)


def test_deny_all_access_with__target_set(topo, test_uer, aci_of_user, request):
    """Search Test 8 Deny all access with != target set

    :id: bc00aed0-6e11-11e8-be66-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(target != "ldap:///{}")(targetattr = "*")'
    '(version 3.0; acl "{}"; deny absolute (all) (userdn = "ldap:///anyone") ;)'.format(USER_ANANDA, request.node.name))
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will not block USER_ANANDA will block others
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will not block USER_ANANDA will block others
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    assert 2 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))


def test_deny_all_access_with__targetattr_set(topo, test_uer, aci_of_user):
    """Search Test 9 Deny all access with != targetattr set

    :id: d2d73b2e-6e11-11e8-ad3d-8c16451d917b
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
    testusers = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = testusers.create(properties={
        'uid': 'Anuj',
        'cn': 'Anuj',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'Anuj',
        'userPassword': PW_DM
    })

    ACI_TARGET = "(targetattr != uid||Objectclass)"
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will allow only uid=*
    assert 3 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(uid=*)'))
    # aci will allow only uid=*
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will allow only uid=*
    assert 3 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(uid=*)'))
    # aci will allow only uid=*
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    assert 3 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(uid=*)'))
    # with root there is no aci blockage
    assert 3 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))
    user.delete()


def test_deny_all_access_with_targetattr_set(topo, test_uer, aci_of_user):
    """Search Test 10 Deny all access with targetattr set

    :id: e1602ff2-6e11-11e8-8e55-8c16451d917b
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
    testuser = UserAccount(topo.standalone, "cn=Anuj12,ou=People,{}".format(DEFAULT_SUFFIX))
    testuser.create(properties={
        'uid': 'Anuj12',
        'cn': 'Anuj12',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'Anuj12'
    })

    ACI_TARGET = "(targetattr = uid)"
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block only uid=*
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(uid=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block only uid=*
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(uid=*)'))
    # with root there is no aci blockage
    assert 3 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(uid=*)'))
    testuser.delete()


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
