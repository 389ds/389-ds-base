# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

import pytest, os, ldap
from lib389._constants import DEFAULT_SUFFIX, PW_DM, ErrorLog
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.account import Accounts
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain

pytestmark = pytest.mark.tier1

CONTAINER_1_DELADD = "ou=Product Development,{}".format(DEFAULT_SUFFIX)
CONTAINER_2_DELADD = "ou=Accounting,{}".format(DEFAULT_SUFFIX)
USER_ANUJ = "uid=Anuj Borah,{}".format(CONTAINER_1_DELADD)
USER_ANANDA = "uid=Ananda Borah,{}".format(CONTAINER_2_DELADD)


@pytest.fixture(scope="function")
def aci_of_user(request, topo):
    # Add anonymous access aci
    ACI_TARGET = "(targetattr != \"userpassword\")(target = \"ldap:///%s\")" % (DEFAULT_SUFFIX)
    ACI_ALLOW = "(version 3.0; acl \"Anonymous Read access\"; allow (read,search,compare)"
    ACI_SUBJECT = "(userdn=\"ldap:///anyone\");)"
    ANON_ACI = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    try:
        suffix.add('aci', ANON_ACI)
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass

    aci_list = Domain(topo.standalone, DEFAULT_SUFFIX).get_attr_vals('aci')

    def finofaci():
        domain = Domain(topo.standalone, DEFAULT_SUFFIX)
        domain.set('aci', None)
        for i in aci_list:
            domain.add("aci", i)
            pass

    request.addfinalizer(finofaci)


@pytest.fixture(scope="module")
def add_test_user(request, topo):
    topo.standalone.config.loglevel((ErrorLog.ACL_SUMMARY,))

    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    for i in ['Product Development', 'Accounting']:
        ous.create(properties={'ou': i})

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


def test_deny_all_access_with__target_set_on_non_leaf(topo, add_test_user, aci_of_user):
    """Search Test 11 Deny all access with != target set on non-leaf

    :id: f1c5d72a-6e11-11e8-aa9d-8c16451d917b
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
    ACI_TARGET = "(target != ldap:///{})(targetattr=\"*\")".format(CONTAINER_2_DELADD)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # After binding with USER_ANANDA , aci will limit the search to itself
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # After binding with USER_ANUJ , aci will limit the search to itself
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # After binding with root , the actual number of users will be given
    assert 4 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))


def test_deny_all_access_with__target_set_on_wildcard_non_leaf(
    topo, add_test_user, aci_of_user
):
    """Search Test 12 Deny all access with != target set on wildcard non-leaf

    :id: 02f34640-6e12-11e8-a382-8c16451d917b
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
    ACI_TARGET = "(target != ldap:///ou=Product*,{})(targetattr=\"*\")".format(
        DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will limit the search to ou=Product it will block others
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will limit the search to ou=Product it will block others
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root , aci will give actual no of users , without any limit.
    assert 4 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))


def test_deny_all_access_with__target_set_on_wildcard_leaf(
    topo, add_test_user, aci_of_user
):
    """Search Test 13 Deny all access with != target set on wildcard leaf

    :id: 16c54d76-6e12-11e8-b5ba-8c16451d917b
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
    ACI_TARGET = "(target != ldap:///uid=Anuj*, ou=*,{})(targetattr=\"*\")".format(
        DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will limit the search to cn=Jeff it will block others
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will limit the search to cn=Jeff it will block others
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    assert 4 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))


def test_deny_all_access_with_targetfilter_using_equality_search(
    topo, add_test_user, aci_of_user
):
    """Search Test 14 Deny all access with targetfilter using equality search

    :id: 27255e04-6e12-11e8-8e35-8c16451d917b
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
    ACI_TARGET = '(targetfilter ="(uid=Anuj Borah)")(target = ldap:///{})(targetattr="*")'.format(
        DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block the search to cn=Jeff
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(uid=Anuj Borah)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block the search to cn=Jeff
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(uid=Anuj Borah)'))
    # with root there is no blockage
    assert 1 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(uid=Anuj Borah)'))


def test_deny_all_access_with_targetfilter_using_equality_search_two(
    topo, add_test_user, aci_of_user
):
    """Test that Search Test 15 Deny all access with targetfilter using != equality search

    :id: 3966bcd4-6e12-11e8-83ce-8c16451d917b
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
    ACI_TARGET = '(targetfilter !="(uid=Anuj Borah)")(target = ldap:///{})(targetattr="*")'.format(
        DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will limit the search to cn=Jeff it will block others
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will limit the search to cn=Jeff it will block others
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no blockage
    assert 4 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))


def test_deny_all_access_with_targetfilter_using_substring_search(
    topo, add_test_user, aci_of_user
):
    """Test that Search Test 16 Deny all access with targetfilter using substring search

    :id: 44d7b4ba-6e12-11e8-b420-8c16451d917b
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
    ACI_TARGET = '(targetfilter ="(uid=Anu*)")(target = ldap:///{})(targetattr="*")'.format(
        DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci block anything cn=j*
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=Anu*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci block anything cn=j*
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=Anu*)'))
    # with root there is no blockage
    assert 1 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=Anu*)'))


def test_deny_all_access_with_targetfilter_using_substring_search_two(
    topo, add_test_user, aci_of_user
):
    """Test that Search Test 17 Deny all access with targetfilter using != substring search

    :id: 55b12d98-6e12-11e8-8cf4-8c16451d917b
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
    ACI_TARGET = '(targetfilter !="(uid=Anu*)")(target = ldap:///{})(targetattr="*")'.format(
        DEFAULT_SUFFIX
    )
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci allow anything cn=j*, it will block others
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(uid=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci allow anything cn=j*, it will block others
    assert 1 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(uid=*)'))
    # with root there is no blockage
    assert 3 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(uid=*)'))


def test_deny_all_access_with_targetfilter_using_boolean_or_of_two_equality_search(
    topo, add_test_user, aci_of_user, request
):
    """Search Test 18 Deny all access with targetfilter using boolean OR of two equality search

    :id: 29cc35fa-793f-11e8-988f-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(target = ldap:///{})(targetattr = "*")'
    '(targetfilter = (|(cn=scarter)(cn=jvaughan)))(version 3.0; acl "{}"; '
    'deny absolute (all) (userdn = "ldap:///anyone") ;)'.format(DEFAULT_SUFFIX, request.node.name))
    UserAccount(topo.standalone, USER_ANANDA).set("cn", "scarter")
    UserAccount(topo.standalone, USER_ANUJ).set("cn", "jvaughan")
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will deny_all_access_with_targetfilter_using_boolean_or_of_two_equality_search
    user = UserAccount(conn, USER_ANANDA)
    with pytest.raises(IndexError):
        user.get_attr_val_utf8('uid')
    # aci will deny_all_access_with_targetfilter_using_boolean_or_of_two_equality_search
    user = UserAccount(conn, USER_ANUJ)
    with pytest.raises(IndexError):
        user.get_attr_val_utf8('uid')
    # with root no blockage
    assert UserAccount(topo.standalone, USER_ANANDA).get_attr_val_utf8('uid') == 'Ananda Borah'
    # with root no blockage
    assert UserAccount(topo.standalone, USER_ANUJ).get_attr_val_utf8('uid') == 'Anuj Borah'


def test_deny_all_access_to__userdn_two(topo, add_test_user, aci_of_user):
    """Search Test 19 Deny all access to != userdn

    :id: 693496c0-6e12-11e8-80dc-8c16451d917b
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
    ACI_TARGET = "(target = ldap:///{})(targetattr=\"*\")".format(DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn!="ldap:///{}";)'.format(USER_ANANDA)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will not block anything for USER_ANANDA , it block other users
    assert 4 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block everything for other users
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root there is no aci blockage
    assert 4 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))


def test_deny_all_access_with_userdn(topo, add_test_user, aci_of_user):
    """Search Test 20 Deny all access with userdn

    :id: 75aada86-6e12-11e8-bd34-8c16451d917b
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
    ACI_TARGET = "(target = ldap:///{})(targetattr=\"*\")".format(DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny (all)'
    ACI_SUBJECT = 'userdn="ldap:///{}";)'.format(USER_ANANDA)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will block anything for USER_ANANDA , it not block other users
    assert 0 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    conn = UserAccount(topo.standalone, USER_ANUJ).bind(PW_DM)
    # aci will block anything for other users
    assert 4 == len(Accounts(conn, DEFAULT_SUFFIX).filter('(cn=*)'))
    # with root thers is no aci blockage
    assert 4 == len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(cn=*)'))


def test_deny_all_access_with_targetfilter_using_presence_search(
    topo, add_test_user, aci_of_user
):
    """Search Test 21 Deny all access with targetfilter using presence search

    :id: 85244a42-6e12-11e8-9480-8c16451d917b
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
    user = UserAccounts(topo.standalone,  DEFAULT_SUFFIX).create_test_user()
    user.set('userPassword', PW_DM)

    ACI_TARGET = '(targetfilter ="(cn=*)")(target = ldap:///{})(targetattr="*")'.format(
        DEFAULT_SUFFIX)
    ACI_ALLOW = '(version 3.0; acl "Name of the ACI"; deny absolute (all)'
    ACI_SUBJECT = 'userdn="ldap:///anyone";)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_ANANDA).bind(PW_DM)
    # aci will eny_all_access_with_targetfilter_using_presence_search
    user = UserAccount(conn, 'uid=test_user_1000,ou=People,{}'.format(DEFAULT_SUFFIX))
    with pytest.raises(IndexError):
        user.get_attr_val_utf8('cn')
    # with root no blockage
    assert UserAccount(topo.standalone, 'uid=test_user_1000,ou=People,{}'.format(DEFAULT_SUFFIX)).get_attr_val_utf8('cn') == 'test_user_1000'


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
