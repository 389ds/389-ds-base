# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
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
            ua =  UserAccount(topo.standalone, DN)
            try:
                ua.delete()
            except:
                pass

    request.addfinalizer(fin)


class _ModTitleArchitectJeffVedder:
    def __init__(self, topo, value, conn):
        self.topo = topo
        self.value = value
        self.conn = conn
        self.user = UserAccount(self.conn, USER_DELADD)

    def add(self):
        self.user.add("title", self.value)

    def delete(self):
        self.user.remove("title", self.value)


class _DelTitleArchitectJeffVedder:
    def __init__(self, topo, conn):
        self.topo = topo
        self.conn = conn

    def delete(self):
        UserAccount(self.conn, USER_DELADD).remove("title", None)


class _AddTitleWithRoot:
    def __init__(self, topo, value):
        self.topo = topo
        self.value = value
        self.user = UserAccount(self.topo.standalone, USER_DELADD)

    def add(self):
        self.user.add("title", self.value)

    def delete(self):
        self.user.remove("title", self.value)


class _AddFREDWithRoot:
    def __init__(self, topo, title1, title2, title3):
        self.topo = topo
        self.title1 = title1
        self.title2 = title2
        self.title3 = title3

    def create(self):
        properties = {
            'uid': 'FRED',
            'cn': 'FRED',
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + 'FRED'
        }
        user = UserAccount(self.topo.standalone, "cn=FRED, ou=Accounting,{}".format(DEFAULT_SUFFIX))
        user.create(properties=properties)
        user.set("title", [self.title1, self.title2, self.title3])


def test_delete_an_attribute_value_we_are_not_allowed_to_delete(
        topo, _add_user, aci_of_user
):
    """Testing the targattrfilters keyword that allows access control based on the value
    of the attributes being added (or deleted))
    Test that we can MODIFY:add an attribute value we are allowed to add

    :id: 7c41baa6-7aa9-11e8-9bdc-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=architect), del=title:(title=architect)")' \
               '(version 3.0; acl "ACI NAME"; allow (write) (userdn = "ldap:///{}") ;)'.format(USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _ModTitleArchitectJeffVedder(topo, "engineer", conn).add()
    _ModTitleArchitectJeffVedder(topo, "architect", conn).add()


def test_donot_allow_write_access_to_title_if_value_is_not_architect(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that we cannot MODIFY:add an attribute value we are not allowed to add

    :id: 822c607e-7aa9-11e8-b2e7-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=architect), del=title:(title=architect)")' \
               '(version 3.0; acl "{}"; allow (write) (userdn = "ldap:///{}") ;)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    # aci will allow to add title architect
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    _ModTitleArchitectJeffVedder(topo, "architect", conn).add()
    # aci will noo allow to add title architect1
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _ModTitleArchitectJeffVedder(topo, "architect1", conn).add()


def test_delete_an_attribute_value_we_are_allowed_to_delete(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of
    the attributes being added (or deleted))
    Test that we can MODIFY:delete an attribute value we are allowed to delete

    :id: 86f36b34-7aa9-11e8-ab16-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=architect), del=title:(title=architect)")' \
               '(version 3.0; acl "{}"; allow (write) (userdn = "ldap:///{}") ;)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "architect").add()
    # aci will allow to delete title architect
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    r1 = _ModTitleArchitectJeffVedder(topo, "architect", conn)
    r1.delete()


def test_delete_an_attribute_value_we_are_not_allowed_to_deleted(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that we cannot MODIFY:delete an attribute value we are allowed to delete

    :id: 8c9f3a90-7aa9-11e8-bf2e-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=architect), del=title:(title=architect)")' \
               '(version 3.0; acl "{}"; allow (write) (userdn = "ldap:///{}") ;)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "engineer").add()
    # acl will not allow to delete title engineer
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _ModTitleArchitectJeffVedder(topo, "engineer", conn).delete()


def test_allow_modify_replace(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that we can MODIFY:replace an attribute if we have correct add/delete rights.

    :id: 9148a234-7aa9-11e8-a1f1-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=engineer), del=title:(|(title=architect)' \
               '(title=idiot))")(version 3.0; acl "{}"; ' \
               'allow (write) (userdn = "ldap:///{}") ;)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "architect").add()
    _AddTitleWithRoot(topo, "idiot").add()
    _AddTitleWithRoot(topo, "engineer").add()
    # acl will not allow to delete title engineer
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _ModTitleArchitectJeffVedder(topo, "engineer", conn).delete()


def test_allow_modify_delete(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Don't Allow modify:replace because of lack of delete rights

    :id: 962842d2-7aa9-11e8-b39e-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=engineer), del=title:(|(title=architect))")' \
               '(version 3.0; acl "{}"; allow (write) ' \
               '(userdn = "ldap:///{}") ;)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "architect").add()
    _AddTitleWithRoot(topo, "idiot").add()
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    _ModTitleArchitectJeffVedder(topo, "architect", conn).delete()
    # acl will not allow to delete title idiot
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _ModTitleArchitectJeffVedder(topo, "idiot", conn).delete()


def test_replace_an_attribute_if_we_lack(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that we cannot MODIFY:replace an attribute if we lack

    :id: 9b1e6afa-7aa9-11e8-ac5b-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=engineer), del=title:(|(title=architect))")' \
               '(version 3.0; acl "{}"; allow (write) ' \
               '(userdn = "ldap:///{}") ;)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "architect").add()
    _AddTitleWithRoot(topo, "idiot").add()
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    _ModTitleArchitectJeffVedder(topo, "architect", conn).delete()
    # acl will not allow to delete title idiot
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _ModTitleArchitectJeffVedder(topo, "idiot", conn).delete()


def test_remove_an_attribute_if_we_have_del_rights_to_all_attr_value(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that we can use MODIFY:delete to entirely remove an attribute if we have del rights
    to all attr values negative case tested next.

    :id: a0c9e0c4-7aa9-11e8-8880-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=engineer), del=title:(|(title=architect)' \
               '(title=idiot))")(version 3.0; acl "{}"; allow (write)' \
               ' (userdn = "ldap:///{}") ;)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "architect").add()
    _AddTitleWithRoot(topo, "idiot").add()
    # acl will allow to delete title idiot
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    _DelTitleArchitectJeffVedder(topo,conn).delete()


def test_remove_an_attribute_if_we_donot_have_del_rights_to_all_attr_value(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that we can use MODIFY:delete to entirely remove an attribute if we have not del
    rights to all attr values

    :id: a6862eaa-7aa9-11e8-8bf9-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=engineer), del=title:(|(title=architect)' \
               '(title=idiot))")(version 3.0; acl "{}"; allow (write) ' \
               '(userdn = "ldap:///{}") ;)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "architect").add()
    _AddTitleWithRoot(topo, "sailor").add()
    # aci will not allow to delete all titles
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _DelTitleArchitectJeffVedder(topo, conn).delete()


def test_remove_an_attribute_if_we_have_del_rights_to_all_attr_values(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that we can use MODIFY:replace to entirely remove an attribute if we have del rights to all attr values

    :id: ab04c7e8-7aa9-11e8-84db-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=engineer), del=title:(|(title=architect)' \
               '(title=idiot))")(version 3.0; acl "{}"; allow (write) ' \
               '(userdn = "ldap:///{}") ;)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "architect").add()
    _AddTitleWithRoot(topo, "idiot").add()
    # aci allowing to delete an_attribute_if_we_have_del_rights_to_all_attr_values
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    _DelTitleArchitectJeffVedder(topo, conn).delete()


def test_cantnot_delete_an_entry_with_attribute_values_we_are_not_allowed_delete(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of
    the attributes being added (or deleted))
    Test we cannot DELETE an entry with attribute values we are not allowed delete

    :id: b525d94c-7aa9-11e8-8539-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(|(title=engineer)(title=cool dude)(title=scum)), ' \
               'del=title:(|(title=engineer)(title=cool dude)(title=scum))")(version 3.0; ' \
               'aci "{}"; allow (delete) userdn = "ldap:///{}";)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddFREDWithRoot(topo, "engineer", "cool dude", "ANuj").create()
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # aci will not allow to delete
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        UserAccount(conn, FRED).delete()


def test_we_can_add_and_delete_an_entry_with_attribute_values_we_are_allowed_add_and_delete(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test we can DELETE an entry with attribute values we are allowed delete

    :id: ba138e54-7aa9-11e8-8037-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(|(title=engineer)(title=cool dude)(title=scum)), ' \
               'del=title:(|(title=engineer)(title=cool dude)(title=scum))")(version 3.0; ' \
               'aci "{}"; allow (delete) userdn = "ldap:///{}";)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddFREDWithRoot(topo, "engineer", "cool dude", "scum").create()
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    # aci will allow to delete
    UserAccount(conn, FRED).delete()


def test_allow_title(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that if attr appears in targetattr and in targattrfilters then targattrfilters
    applies--ie. targattrfilters is a refinement of targattrfilters.

    :id: beadf328-7aa9-11e8-bb08-8c16451d917b
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
    ACI_BODY = '(targetattr="title")(targattrfilters = "add=title:(|(title=engineer)' \
               '(title=cool dude)(title=scum)), del=title:(|(title=engineer)(title=cool dude)' \
               '(title=scum))")(version 3.0; aci "{}"; allow (write) ' \
               'userdn = "ldap:///{}";)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "engineer").add()
    _AddTitleWithRoot(topo, "cool dude").add()
    # # aci will not allow to add title  topdog
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _ModTitleArchitectJeffVedder(topo, "topdog", conn).add()


def test_allow_to_modify(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that I can have secretary in targetattr and title in targattrfilters.

    :id: c32e4704-7aa9-11e8-951d-8c16451d917b
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
    ACI_BODY = '(targetattr="secretary")(targattrfilters = "add=title:(|(title=engineer)' \
               '(title=cool dude)(title=scum)), del=title:(|(title=engineer)(title=cool dude)' \
               '(title=scum))")(version 3.0; aci "{}"; allow (write)' \
               ' userdn = "ldap:///{}";)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "engineer").add()
    _AddTitleWithRoot(topo, "cool dude").add()
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    user = UserAccount(conn, USER_DELADD)
    # aci will allow to add 'secretary', "cn=emporte quoi
    user.add('secretary', "cn=emporte quoi, {}".format(DEFAULT_SUFFIX))
    assert user.get_attr_val('secretary')


def test_selfwrite_does_not_confer_write_on_a_targattrfilters_atribute(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of
    the attributes being added (or deleted))
    Selfwrite does not confer "write" on a targattrfilters atribute.

    :id: c7b9ec2e-7aa9-11e8-ba4a-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(|(title=engineer)(title=cool dude)(title=scum)), ' \
               'del=title:(|(title=engineer)(title=cool dude)(title=scum))")(version 3.0; ' \
               'aci "{}"; allow (selfwrite) userdn = "ldap:///{}";)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    # aci will not allow to add selfwrite_does_not_confer_write_on_a_targattrfilters_atribute
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _ModTitleArchitectJeffVedder(topo, "engineer", conn).add()


def test_selfwrite_continues_to_give_rights_to_attr_in_targetattr_list(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of
    the attributes being added (or deleted))
    Selfwrite continues to give rights to attr in targetattr list.

    :id: cd287680-7aa9-11e8-a8e2-8c16451d917b
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
    ACI_BODY = '(targetattr="secretary")(targattrfilters = "add=title:(|(title=engineer)' \
               '(title=cool dude)(title=scum)), del=title:(|(title=engineer)(title=cool dude)' \
               '(title=scum))")(version 3.0; aci "{}"; allow (selfwrite) ' \
               'userdn = "ldap:///{}";)'.format(request.node.name, USER_WITH_ACI_DELADD)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    # selfwrite_continues_to_give_rights_to_attr_in_targetattr_list
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _ModTitleArchitectJeffVedder(topo, "engineer", conn).add()


def test_add_an_attribute_value_we_are_allowed_to_add_with_ldapanyone(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that we can MODIFY:add an attribute value we are allowed to add with ldap:///anyone

    :id: d1e1d7ac-7aa9-11e8-b968-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=architect), del=title:(title=architect)")' \
               '(version 3.0; acl "{}"; allow (write) userdn = "ldap:///anyone";)'.format(request.node.name)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    _AddTitleWithRoot(topo, "engineer").add()
    # aci will allow to add title architect
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    _ModTitleArchitectJeffVedder(topo, "architect", conn).add()


def test_hierarchy(topo, _add_user, aci_of_user, request):
    """Testing the targattrfilters keyword that allows access control based on the value of
    the attributes being added (or deleted))
    Test that with two targattrfilters in the hierarchy that the general one applies.
    This is the correct behaviour, even if it's a bit confusing

    :id: d7ae354a-7aa9-11e8-8b0d-8c16451d917b
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
    ACI_BODY = '(targattrfilters = "add=title:(title=arch*)")(version 3.0; acl "{}"; ' \
               'allow (write) (userdn = "ldap:///anyone") ;)'.format(request.node.name)
    ACI_BODY1 = '(targattrfilters = "add=title:(title=architect)")(version 3.0; ' \
                'acl "{}"; allow (write) (userdn = "ldap:///anyone") ;)'.format(request.node.name)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY1)
    _AddTitleWithRoot(topo, "engineer").add()
    # aci will allow to add title architect
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    _ModTitleArchitectJeffVedder(topo, "architect", conn).add()
    # aci will not allow to add title architect
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        _ModTitleArchitectJeffVedder(topo, "engineer", conn).add()


def test_targattrfilters_and_search_permissions_and_that_ldapmodify_works_as_expected(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of the
    attributes being added (or deleted))
    Test that we can have targattrfilters and search permissions and that ldapmodify works as expected.

    :id: ddae7a22-7aa9-11e8-ad6b-8c16451d917b
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
    ACI_BODY = '(targetattr="secretary || objectclass || mail")(targattrfilters = "add=title:' \
               '(title=arch*)")(version 3.0; acl "{}"; ' \
               'allow (write,read,search,compare) (userdn = "ldap:///anyone") ;)'.format(request.node.name)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    # aci will allow to add title architect
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    _ModTitleArchitectJeffVedder(topo, "architect", conn).add()


def test_targattrfilters_and_search_permissions_and_that_ldapmodify_works_as_expected_two(
        topo, _add_user, aci_of_user, request
):
    """Testing the targattrfilters keyword that allows access control based on the value of
    the attributes being added (or deleted))
    Test that we can have targattrfilters and search permissions and that ldapsearch works as expected.

    :id: e25d116e-7aa9-11e8-81d8-8c16451d917b
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
    ACI_BODY = '(targetattr="secretary || objectclass || mail")(targattrfilters = ' \
               '"add=title:(title=arch*)")(version 3.0; acl "{}"; allow ' \
               '(write,read,search,compare) (userdn = "ldap:///anyone") ;)'.format(request.node.name)
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ACI_BODY)
    conn = Anonymous(topo.standalone).bind()
    user = UserAccount(conn, USER_DELADD)
    #targattrfilters_and_search_permissions_and_that_ldapmodify_works_as_expected
    assert user.get_attr_vals('secretary')
    assert user.get_attr_vals('mail')
    assert user.get_attr_vals('objectclass')


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
