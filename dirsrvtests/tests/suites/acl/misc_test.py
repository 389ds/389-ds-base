"""
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 RED Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----
"""

import ldap
import os
import pytest

from lib389._constants import DEFAULT_SUFFIX, PW_DM, DN_DM
from lib389.idm.user import UserAccount, UserAccounts
from lib389._mapped_object import DSLdapObject
from lib389.idm.account import Accounts, Anonymous
from lib389.idm.organizationalunit import OrganizationalUnit, OrganizationalUnits
from lib389.idm.group import Group, Groups
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain
from lib389.plugins import ACLPlugin

pytestmark = pytest.mark.tier1

PEOPLE = "ou=PEOPLE,{}".format(DEFAULT_SUFFIX)
DYNGROUP = "cn=DYNGROUP,{}".format(PEOPLE)
CONTAINER_1_DELADD = "ou=Product Development,{}".format(DEFAULT_SUFFIX)
CONTAINER_2_DELADD = "ou=Accounting,{}".format(DEFAULT_SUFFIX)


@pytest.fixture(scope="function")
def aci_of_user(request, topo):
    """
    :param request:
    :param topo:
    """

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

    aci_list = suffix.get_attr_vals('aci')

    def finofaci():
        """
        Removes and Restores ACIs after the test.
        """
        domain = Domain(topo.standalone, DEFAULT_SUFFIX)
        domain.remove_all('aci')
        for i in aci_list:
            domain.add("aci", i)

    request.addfinalizer(finofaci)


@pytest.fixture(scope="function")
def clean(request, topo):
    """
    :param request:
    :param topo:
    """
    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    try:
        for i in ['Product Development', 'Accounting']:
            ous.create(properties={'ou': i})
    except ldap.ALREADY_EXISTS as eoor_eoor:
        topo.standalone.log.info("Exception (expected): %s" % type(eoor_eoor).__name__)

    def fin():
        """
        Deletes entries after the test.
        """
        for scope_scope in [CONTAINER_1_DELADD, CONTAINER_2_DELADD, PEOPLE]:
            try:
                DSLdapObject(topo.standalone, scope_scope).delete()
            except ldap.ALREADY_EXISTS as eoor_eoor:
                topo.standalone.log.info("Exception (expected): %s" % type(eoor_eoor).__name__)

    request.addfinalizer(fin)


def test_accept_aci_in_addition_to_acl(topo, clean, aci_of_user):
    """Misc Test 2 accept aci in addition to acl

    :id: 8e9408fa-7db8-11e8-adaa-8c16451d917b
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
    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=product development')
    user = uas.create_test_user()
    for i in [('mail', 'anujborah@okok.com'), ('givenname', 'Anuj'), ('userPassword', PW_DM)]:
        user.set(i[0], i[1])

    aci_target = '(targetattr="givenname")'
    aci_allow = ('(version 3.0; acl "Name of the ACI"; deny (read, search, compare, write)')
    aci_subject = 'userdn="ldap:///anyone";)'
    Domain(topo.standalone, CONTAINER_1_DELADD).add("aci", aci_target + aci_allow + aci_subject)

    conn = Anonymous(topo.standalone).bind()
    # aci will block  targetattr=givenname to anyone
    user = UserAccount(conn, user.dn)
    with pytest.raises(AssertionError):
        assert user.get_attr_val_utf8('givenname') == 'Anuj'
    # aci will allow  targetattr=uid to anyone
    assert user.get_attr_val_utf8('uid') == 'test_user_1000'

    for i in uas.list():
        i.delete()


@pytest.mark.bz334451
def test_more_then_40_acl_will_crash_slapd(topo, clean, aci_of_user):
    """bug 334451 : more then 40 acl will crash slapd
    superseded by Bug 772778 - acl cache overflown problem with > 200 acis

    :id: 93a44c60-7db8-11e8-9439-8c16451d917b
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
    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=Accounting')
    user = uas.create_test_user()

    aci_target = '(target ="ldap:///{}")(targetattr!="userPassword")'.format(CONTAINER_1_DELADD)
    # more_then_40_acl_will not crash_slapd
    for i in range(40):
        aci_allow = '(version 3.0;acl "ACI_{}";allow (read, search, compare)'.format(i)
        aci_subject = 'userdn="ldap:///anyone";)'
        aci_body = aci_target + aci_allow + aci_subject
        Domain(topo.standalone, CONTAINER_1_DELADD).add("aci", aci_body)
    conn = Anonymous(topo.standalone).bind()
    assert UserAccount(conn, user.dn).get_attr_val_utf8('uid') == 'test_user_1000'

    for i in uas.list():
        i.delete()

@pytest.mark.bz345643
def test_search_access_should_not_include_read_access(topo, clean, aci_of_user):
    """bug 345643
    Misc Test 4 search access should not include read access

    :id: 98ab173e-7db8-11e8-a309-8c16451d917b
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
    assert Domain(topo.standalone, DEFAULT_SUFFIX).present('aci')
    Domain(topo.standalone, DEFAULT_SUFFIX)\
        .replace("aci", [f'(target ="ldap:///{DEFAULT_SUFFIX}")(targetattr != "userPassword")'
                     '(version 3.0;acl "anonymous access";allow (search)'
                     '(userdn = "ldap:///anyone");)',
                     f'(target="ldap:///{DEFAULT_SUFFIX}") (targetattr = "*")(version 3.0; '
                     'acl "allow self write";allow(write) '
                     'userdn = "ldap:///self";)',
                     f'(target="ldap:///{DEFAULT_SUFFIX}") (targetattr = "*")(version 3.0; '
                     'acl "Allow all admin group"; allow(all) groupdn = "ldap:///cn=Directory '
                     'Administrators, {}";)'])

    conn = Anonymous(topo.standalone).bind()
    # search_access_should_not_include_read_access
    suffix = Domain(conn, DEFAULT_SUFFIX)
    with pytest.raises(Exception):
        assert suffix.present('aci')


def test_only_allow_some_targetattr(topo, clean, aci_of_user):
    """Misc Test 5 only allow some targetattr (1/2)

    :id: 9d27f048-7db8-11e8-a71c-8c16451d917b
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

    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    for i in range(1, 3):
        user = uas.create_test_user(uid=i, gid=i)
        user.replace_many(('cn', 'Anuj1'), ('mail', 'annandaBorah@anuj.com'))

    Domain(topo.standalone, DEFAULT_SUFFIX).\
        replace("aci", '(target="ldap:///{}")(targetattr="mail||objectClass")'
                       '(version 3.0; acl "Test";allow (read,search,compare) '
                       '(userdn = "ldap:///anyone"); )'.format(DEFAULT_SUFFIX))

    conn = Anonymous(topo.standalone).bind()
    accounts = Accounts(conn, DEFAULT_SUFFIX)

    # aci will allow only mail targetattr
    assert len(accounts.filter('(mail=*)')) == 2
    # aci will allow only mail targetattr
    assert not accounts.filter('(cn=*)', scope=1)
    # with root no , blockage
    assert len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(uid=*)', scope=1)) == 2

    for i in uas.list():
        i.delete()


def test_only_allow_some_targetattr_two(topo, clean, aci_of_user, request):
    """Misc Test 6 only allow some targetattr (2/2)"

    :id: a188239c-7db8-11e8-903e-8c16451d917b
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
    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    for i in range(5):
        user = uas.create_test_user(uid=i, gid=i)
        user.replace_many(('mail', 'anujborah@anujborah.com'),
                          ('cn', 'Anuj'), ('userPassword', PW_DM))

    user1 = uas.create_test_user()
    user1.replace_many(('mail', 'anujborah@anujborah.com'), ('userPassword', PW_DM))

    Domain(topo.standalone, DEFAULT_SUFFIX).\
        replace("aci", '(target="ldap:///{}") (targetattr="mail||objectClass")'
                       '(targetfilter="cn=Anuj") (version 3.0; acl "{}"; '
                       'allow (compare,read,search) '
                       '(userdn = "ldap:///anyone"); )'.format(DEFAULT_SUFFIX, request.node.name))

    conn = UserAccount(topo.standalone, user.dn).bind(PW_DM)
    # aci will allow only mail targetattr but only for cn=Anuj
    account = Accounts(conn, DEFAULT_SUFFIX)
    assert len(account.filter('(mail=*)', scope=1)) == 5
    assert not account.filter('(cn=*)', scope=1)

    for i in account.filter('(mail=*)'):
        assert i.get_attr_val_utf8('mail') == 'anujborah@anujborah.com'


    conn = Anonymous(topo.standalone).bind()
    # aci will allow only mail targetattr but only for cn=Anuj
    account = Accounts(conn, DEFAULT_SUFFIX)
    assert len(account.filter('(mail=*)', scope=1)) == 5
    assert not account.filter('(cn=*)', scope=1)

    for i in account.filter('(mail=*)'):
        assert i.get_attr_val_utf8('mail') == 'anujborah@anujborah.com'

    # with root no blockage
    assert len(Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(mail=*)')) == 6

    for i in uas.list():
        i.delete()


@pytest.mark.bz326000
def test_memberurl_needs_to_be_normalized(topo, clean, aci_of_user):
    """Non-regression test for BUG 326000: MemberURL needs to be normalized

    :id: a5d172e6-7db8-11e8-aca7-8c16451d917b
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
    ou_ou = OrganizationalUnit(topo.standalone, "ou=PEOPLE,{}".format(DEFAULT_SUFFIX))
    ou_ou.set('aci', '(targetattr="*")'
                     '(version 3.0; acl "tester"; allow(all) '
                     'groupdn = "ldap:///cn =DYNGROUP,ou=PEOPLE, {}";)'.format(DEFAULT_SUFFIX))

    groups = Groups(topo.standalone, DEFAULT_SUFFIX, rdn='ou=PEOPLE')
    groups.create(properties={"cn": "DYNGROUP",
                              "description": "DYNGROUP",
                              'objectClass': 'groupOfURLS',
                              'memberURL': "ldap:///ou=PEOPLE,{}??sub?"
                                           "(uid=test_user_2)".format(DEFAULT_SUFFIX)})

    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for demo1 in [(1, "Entry to test rights on."), (2, "Member of DYNGROUP")]:
        user = uas.create_test_user(uid=demo1[0], gid=demo1[0])
        user.replace_many(('description', demo1[1]), ('userPassword', PW_DM))

    ##with normal aci
    conn = UserAccount(topo.standalone, uas.list()[1].dn).bind(PW_DM)
    harry = UserAccount(conn, uas.list()[1].dn)
    harry.add('sn', 'FRED')

    ##with abnomal aci
    dygrp = Group(topo.standalone, DYNGROUP)
    dygrp.remove('memberurl', "ldap:///ou=PEOPLE,{}??sub?(uid=test_user_2)".format(DEFAULT_SUFFIX))
    dygrp.add('memberurl', "ldap:///ou=PEOPLE,{}??sub?(uid=tesT_UsEr_2)".format(DEFAULT_SUFFIX))
    harry.add('sn', 'Not FRED')

    for i in uas.list():
        i.delete()

@pytest.mark.bz624370
def test_greater_than_200_acls_can_be_created(topo, clean, aci_of_user):
    """Misc 10, check that greater than 200 ACLs can be created. Bug 624370

    :id: ac020252-7db8-11e8-8652-8c16451d917b
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
    # greater_than_200_acls_can_be_created
    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for i in range(200):
        user = uas.create_test_user(uid=i, gid=i)
        user.set('aci', '(targetattr = "description")'
                        '(version 3.0;acl "foo{}";  allow (read, search, compare)'
                        '(userdn="ldap:///anyone");)'.format(i))

        assert user.\
                   get_attr_val_utf8('aci') == '(targetattr = "description")' \
                                               '(version 3.0;acl "foo{}";  allow ' \
                                               '(read, search, compare)' \
                                               '(userdn="ldap:///anyone");)'.format(i)
    for i in uas.list():
        i.delete()


@pytest.mark.bz624453
def test_server_bahaves_properly_with_very_long_attribute_names(topo, clean, aci_of_user):
    """Make sure the server bahaves properly with very long attribute names. Bug 624453.

    :id: b0d31942-7db8-11e8-a833-8c16451d917b
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
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    users.create_test_user()
    users.list()[0].set('userpassword', PW_DM)

    user = UserAccount(topo.standalone, 'uid=test_user_1000,ou=People,{}'.format(DEFAULT_SUFFIX))
    with pytest.raises(ldap.INVALID_SYNTAX):
        user.add("aci", "a" * 9000)


def test_do_bind_as_201_distinct_users(topo, clean, aci_of_user):
    """Test bind as 201 distinct users

    :id: c0060532-7db8-11e8-a124-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Add test entries
        2. Increase the nsslapd-aclpb-max-selected-acls in cn=ACL Plugin,cn=plugins,cn=config
        3. Restart the server
        4. Do bind as 201 distinct users
    :expectedresults:
        1. Entries should be added
        2. Operation should succeed
        3. Operation should succeed
        4. Operation should succeed
    """
    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for i in range(201):
        user = uas.create_test_user(uid=i, gid=i)
        user.set('userPassword', PW_DM)

    users = uas.list()
    for user in users:
        user.bind(PW_DM)

    ACLPlugin(topo.standalone).replace("nsslapd-aclpb-max-selected-acls", '220')
    topo.standalone.restart()

    users = uas.list()
    for user in users:
        user.bind(PW_DM)


def test_info_disclosure(request, topo):
    """Test that a search returns 32 when base entry does not exist

    :id: f6dec4c2-65a3-41e4-a4c0-146196863333
    :setup: Standalone Instance
    :steps:
        1. Add aci
        2. Add test user
        3. Bind as user and search for non-existent entry
    :expectedresults:
        1. Success
        2. Success
        3. Error 32 is returned
    """

    ACI_TARGET = "(targetattr = \"*\")(target = \"ldap:///%s\")" % (DEFAULT_SUFFIX)
    ACI_ALLOW = "(version 3.0; acl \"Read/Search permission for all users\"; allow (read,search)"
    ACI_SUBJECT = "(userdn=\"ldap:///all\");)"
    ACI = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT

    # Get current ACi's so we can restore them when we are done
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    preserved_acis = suffix.get_attr_vals_utf8('aci')

    def finofaci():
        domain = Domain(topo.standalone, DEFAULT_SUFFIX)
        try:
            domain.remove_all('aci')
            domain.replace_values('aci', preserved_acis)
        except:
            pass
    request.addfinalizer(finofaci)

    # Remove aci's
    suffix.remove_all('aci')

    # Add test user
    USER_DN = "uid=test,ou=people," + DEFAULT_SUFFIX
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    users.create(properties={
        'uid': 'test',
        'cn': 'test',
        'sn': 'test',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/test',
        'userPassword': PW_DM
    })

    # bind as user
    conn = UserAccount(topo.standalone, USER_DN).bind(PW_DM)

    # Search fo existing base DN
    test = Domain(conn, DEFAULT_SUFFIX)
    assert len(test.get_attr_vals_utf8_l('dc')) == 0

    # Search for a non existent bases
    subtree = Domain(conn, "ou=does_not_exist," + DEFAULT_SUFFIX)
    assert len(subtree.get_attr_vals_utf8_l('objectclass')) == 0

    subtree = Domain(conn, "ou=also does not exist,ou=does_not_exist," + DEFAULT_SUFFIX)
    assert len(subtree.get_attr_vals_utf8_l('objectclass')) == 0

    # Try ONE level search instead of BASE
    assert len(Accounts(conn, "ou=does_not_exist," + DEFAULT_SUFFIX).filter("(objectclass=top)", scope=ldap.SCOPE_ONELEVEL)) == 0

    # add aci
    suffix.add('aci', ACI)

    # Search for a non existent entry which should raise an exception
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        conn = UserAccount(topo.standalone, USER_DN).bind(PW_DM)
        subtree = Domain(conn, "ou=does_not_exist," + DEFAULT_SUFFIX)
        subtree.get_attr_vals_utf8_l('objectclass')
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        conn = UserAccount(topo.standalone, USER_DN).bind(PW_DM)
        subtree = Domain(conn, "ou=also does not exist,ou=does_not_exist," + DEFAULT_SUFFIX)
        subtree.get_attr_vals_utf8_l('objectclass')
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        conn = UserAccount(topo.standalone, USER_DN).bind(PW_DM)
        DN = "ou=also does not exist,ou=does_not_exist," + DEFAULT_SUFFIX
        Accounts(conn, DN).filter("(objectclass=top)", scope=ldap.SCOPE_ONELEVEL, strict=True)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
