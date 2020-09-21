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
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain

pytestmark = pytest.mark.tier1

CONTAINER_1_DELADD = "ou=Product Development,{}".format(DEFAULT_SUFFIX)
CONTAINER_2_DELADD = "ou=Accounting,{}".format(DEFAULT_SUFFIX)
USER_DELADD = "cn=Jeff Vedder,{}".format(CONTAINER_1_DELADD)
USER_WITH_ACI_DELADD = "cn=Sam Carter,{}".format(CONTAINER_2_DELADD)
FRED = "cn=FRED,ou=Accounting,{}".format(DEFAULT_SUFFIX)
HARRY = "cn=HARRY,ou=Accounting,{}".format(DEFAULT_SUFFIX)
KIRSTENVAUGHAN = "cn=Kirsten Vaughan,ou=Human Resources,{}".format(DEFAULT_SUFFIX)
HUMAN_OU_GLOBAL = "ou=Human Resources,{}".format(DEFAULT_SUFFIX)


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

    request.addfinalizer(finofaci)


@pytest.fixture(scope="function")
def _add_user(request, topo):
    for i in ["Product Development", 'Accounting', "Human Resources"]:
        ou = OrganizationalUnit(topo.standalone, "ou={},{}".format(i, DEFAULT_SUFFIX))
        ou.create(properties={'ou': i})

    properties = {
        'uid': 'Jeff Vedder',
        'cn': 'Jeff Vedder',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'JeffVedder',
        'userPassword': 'password'
    }
    user = UserAccount(topo.standalone, 'cn=Jeff Vedder,{}'.format(CONTAINER_1_DELADD))
    user.create(properties=properties)
    user.set('secretary', 'cn=Arpitoo Borah, o=Red Hat, c=As')
    user.set('mail', 'anuj@anuj.Borah')

    properties = {
        'uid': 'Sam Carter',
        'cn': 'Sam Carter',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'SamCarter',
        'userPassword': 'password'
    }
    user = UserAccount(topo.standalone, 'cn=Sam Carter,{}'.format(CONTAINER_2_DELADD))
    user.create(properties=properties)

    properties = {
        'uid': 'Kirsten Vaughan',
        'cn': 'Kirsten Vaughan',
        'sn': 'Kirsten Vaughan',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'KirstenVaughan',
        'userPassword': 'password'
    }
    user = UserAccount(topo.standalone, 'cn=Kirsten Vaughan, ou=Human Resources,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    properties = {
        'uid': 'HARRY',
        'cn': 'HARRY',
        'sn': 'HARRY',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'HARRY',
        'userPassword': 'password'
    }
    user = UserAccount(topo.standalone, 'cn=HARRY, ou=Accounting,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    def fin():
        for DN in [USER_DELADD, USER_WITH_ACI_DELADD, FRED, HARRY, KIRSTENVAUGHAN,
                   HUMAN_OU_GLOBAL, CONTAINER_2_DELADD,CONTAINER_1_DELADD]:
            ua = UserAccount(topo.standalone, DN)
            try:
                ua.delete()
            except:
                pass

    request.addfinalizer(fin)


def test_we_can_search_as_expected(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of the attributes being added (or deleted))
    Test that we can search as expected

    :id: e845dbba-7aa9-11e8-8988-8c16451d917b
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
    ACI_BODY = '(target="ldap:///cn=*,ou=Product Development, {}")' \
               '(targetfilter="cn=Jeff*")(targetattr="secretary || objectclass || mail")' \
               '(targattrfilters = "add=title:(title=arch*)")(version 3.0; acl "{}"; ' \
               'allow (write,read,search,compare) (userdn = "ldap:///anyone") ;)'.format(DEFAULT_SUFFIX, request.node.name)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = Anonymous(topo.standalone).bind()
    # aci will allow secretary , mail , objectclass
    user = UserAccount(conn, USER_DELADD)
    assert user.get_attr_vals('secretary')
    assert user.get_attr_vals('mail')
    assert user.get_attr_vals('objectclass')


def test_we_can_mod_title_as_expected(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the
    value of the attributes being added (or deleted))
    Test search will work with targattrfilters present.

    :id: f8c1ea88-7aa9-11e8-a55c-8c16451d917b
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
    ACI_BODY = '(target="ldap:///cn=*,ou=Product Development, {}")' \
               '(targetfilter="cn=Jeff*")(targetattr="secretary || objectclass || mail")' \
               '(targattrfilters = "add=title:(title=arch*)")(version 3.0; acl "{}"; ' \
               'allow (write,read,search,compare) (userdn = "ldap:///anyone") ;)'.format(DEFAULT_SUFFIX, request.node.name)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    # aci will not allow 'title', 'topdog'
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    user = UserAccount(conn, USER_DELADD)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.add('title', 'topdog')


def test_modify_with_multiple_filters(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the
    value of the attributes being added (or deleted))
    Allowed by multiple filters

    :id: fd9d223e-7aa9-11e8-a83b-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=architect) && secretary:' \
               '(secretary=cn=Meylan,{}), del=title:(title=architect) && secretary:' \
               '(secretary=cn=Meylan,{})")(version 3.0; acl "{}"; allow (write) ' \
               '(userdn = "ldap:///anyone") ;)'.format(
            DEFAULT_SUFFIX, DEFAULT_SUFFIX, request.node.name
        )
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # aci will allow title some attribute only
    user = UserAccount(conn, USER_DELADD)
    user.add("title", "architect")
    assert user.get_attr_val('title')
    user.add("secretary", "cn=Meylan,dc=example,dc=com")
    assert user.get_attr_val('secretary')


def test_denied_by_multiple_filters(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Denied by multiple filters

    :id: 034c6c62-7aaa-11e8-8634-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=architect) && secretary:' \
               '(secretary=cn=Meylan,{}), del=title:(title=architect) && secretary:' \
               '(secretary=cn=Meylan,{})")(version 3.0; acl "{}"; allow (write) ' \
               '(userdn = "ldap:///anyone") ;)'.format(DEFAULT_SUFFIX, DEFAULT_SUFFIX, request.node.name)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # aci will allow title some attribute only
    user = UserAccount(conn, USER_DELADD)
    user.add("title", "architect")
    assert user.get_attr_val('title')
    user.add("secretary", "cn=Meylan,dc=example,dc=com")
    assert user.get_attr_val('secretary')
    # aci will allow title some attribute only
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.add("secretary", "cn=Grenoble,dc=example,dc=com")


def test_allowed_add_one_attribute(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Allowed add one attribute (in presence of multiple filters)

    :id: 086c7f0c-7aaa-11e8-b69f-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=architect) && secretary:(secretary=cn=Meylan, {}), ' \
               'del=title:(title=architect) && secretary:(secretary=cn=Meylan, {})")(version 3.0; acl "{}"; ' \
               'allow (write) (userdn = "ldap:///{}") ;)'.format(
            DEFAULT_SUFFIX, DEFAULT_SUFFIX, request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    user = UserAccount(conn, USER_DELADD)
    # aci will allow add ad delete
    user.add('title', 'architect')
    assert user.get_attr_val('title')
    user.remove('title', 'architect')


def test_cannot_add_an_entry_with_attribute_values_we_are_not_allowed_add(
    topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test not allowed add an entry

    :id: 0d0effee-7aaa-11e8-b673-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(|(title=engineer)(title=cool dude)(title=scum)) ' \
               '&& secretary:(secretary=cn=Meylan, {}), del=title:(|(title=engineer)(title=cool dude)' \
               '(title=scum))")(version 3.0; aci "{}"; allow (add) userdn = "ldap:///{}";)'.format(
            DEFAULT_SUFFIX, request.node.name, DEFAULT_SUFFIX)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    properties = {
        'uid': 'FRED',
        'cn': 'FRED',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'FRED'
    }
    user = UserAccount(topo.standalone, 'cn=FRED,ou=Accounting,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)
    user.set('title', ['anuj', 'kumar', 'borah'])
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # aci will not allow adding objectclass
    user = UserAccount(conn, USER_WITH_ACI_DELADD)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.add("objectclass", "person")


def test_on_modrdn(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that valuacls kick in for modrdn operation.

    :id: 12985dde-7aaa-11e8-abde-8c16451d917b
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
    ACI_BODY = '(target="ldap:///cn=*,ou=Accounting,{}")(targattrfilters = "add=cn:(|(cn=engineer)), ' \
               'del=title:(|(title=engineer)(title=cool dude)(title=scum))")(version 3.0; aci "{}"; ' \
               'allow (write) userdn = "ldap:///{}";)'.format(DEFAULT_SUFFIX, request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # modrdn_s is not allowed with ou=OU1
    useraccount = UserAccount(conn, FRED)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        useraccount.rename("ou=OU1")


def test_on_modrdn_allow(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of the attributes being
    added (or deleted))
    Test modrdn still works (2)

    :id: 17720562-7aaa-11e8-82ee-8c16451d917b
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
    ACI_BODY = '(target="ldap:///{}")(targattrfilters = "add=cn:((cn=engineer)), del=cn:((cn=jonny))")' \
               '(version 3.0; aci "{}"; allow (write) ' \
               'userdn = "ldap:///{}";)'.format(DEFAULT_SUFFIX, request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    properties = {
        'uid': 'jonny',
        'cn': 'jonny',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'jonny'
    }
    user = UserAccount(topo.standalone, 'cn=jonny,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # aci will allow modrdn_s on cn=engineer
    useraccount = UserAccount(conn, "cn=jonny,{}".format(DEFAULT_SUFFIX))
    useraccount.rename("cn=engineer")
    assert useraccount.dn == 'cn=engineer,dc=example,dc=com'


@pytest.mark.bz979515
def test_targattrfilters_keyword(topo):
    """Testing the targattrfilters keyword that allows access control based on the value
    of the attributes being added (or deleted))
    "Bug #979515 - ACLs inoperative in some search scenarios [rhel-6.5]"
    "Bug #979516 is a clone for DS8.2 on RHEL5.9"
    "Bug #979514 is a clone for RHEL6.4 zStream errata"

    :id: 23f9e9d0-7aaa-11e8-b16b-8c16451d917b
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
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    domain.set('aci', None)
    ou = OrganizationalUnit(topo.standalone, 'ou=bug979515,{}'.format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'bug979515'})
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", '(target="ldap:///ou=bug979515,{}") '
                '(targetattr= "uid") ( version 3.0; acl "read other subscriber"; allow (compare, read, search) '
                'userdn="ldap:///uid=*,ou=bug979515,{}" ; )'.format(DEFAULT_SUFFIX, DEFAULT_SUFFIX))
    properties = {
        'uid': 'acientryusr1',
        'cn': 'acientryusr1',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'acientryusr1'
    }
    user = UserAccount(topo.standalone, 'cn=acientryusr1,ou=bug979515,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)
    user.set('telephoneNumber', '99972566596')
    user.set('mail', 'anuj@anuj.com')
    user.set("userPassword", "password")

    properties = {
        'uid': 'newaciphoneusr1',
        'cn': 'newaciphoneusr1',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'newaciphoneusr1'
    }
    user = UserAccount(topo.standalone, 'cn=newaciphoneusr1,ou=bug979515,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)
    user.set('telephoneNumber', '99972566596')
    user.set('mail', 'anuj@anuj.com')
    conn = UserAccount(topo.standalone, "cn=acientryusr1,ou=bug979515,{}".format(DEFAULT_SUFFIX)).bind(PW_DM)
    #  Testing the targattrfilters keyword that allows access control based on the value of the attributes being added (or deleted))
    user = UserAccount(conn, "cn=acientryusr1,ou=bug979515,{}".format(DEFAULT_SUFFIX))
    with pytest.raises(IndexError):
        user.get_attr_vals('mail')
        user.get_attr_vals('telephoneNumber')
        user.get_attr_vals('cn')
    user = UserAccount(topo.standalone, "cn=acientryusr1,ou=bug979515,{}".format(DEFAULT_SUFFIX))
    user.get_attr_vals('mail')
    user.get_attr_vals('telephoneNumber')
    user.get_attr_vals('cn')


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
