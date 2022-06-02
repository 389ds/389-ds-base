# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


import pytest, os, ldap
from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.group import UniqueGroup, UniqueGroups
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain

pytestmark = pytest.mark.tier1

ACLGROUP_OU_GLOBAL = "ou=ACLGroup,{}".format(DEFAULT_SUFFIX)
NESTEDGROUP_OU_GLOBAL = "ou=nestedgroup, {}".format(DEFAULT_SUFFIX)
TESTING_OU_GLOBAL = "ou=Product Testing,{}".format(DEFAULT_SUFFIX)
DEEPUSER_GLOBAL = "uid=DEEPUSER_GLOBAL, {}".format(NESTEDGROUP_OU_GLOBAL)
DEEPUSER1_GLOBAL = "uid=DEEPUSER1_GLOBAL, {}".format(NESTEDGROUP_OU_GLOBAL)
DEEPUSER2_GLOBAL = "uid=DEEPUSER2_GLOBAL, {}".format(NESTEDGROUP_OU_GLOBAL)
DEEPUSER3_GLOBAL = "uid=DEEPUSER3_GLOBAL, {}".format(NESTEDGROUP_OU_GLOBAL)
DEEPGROUPSCRATCHENTRY_GLOBAL = "uid=scratchEntry,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPDNATTRSCRATCHENTRY_GLOBAL = "uid=GROUPDNATTRSCRATCHENTRY_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPDNATTRCHILDSCRATCHENTRY_GLOBAL = "uid=c1,{}".format(GROUPDNATTRSCRATCHENTRY_GLOBAL)
NEWCHILDSCRATCHENTRY_GLOBAL = "uid=newChild,{}".format(NESTEDGROUP_OU_GLOBAL)
BIG_GLOBAL = "cn=BIG_GLOBAL Group,{}".format(DEFAULT_SUFFIX)
ALLGROUPS_GLOBAL = "cn=ALLGROUPS_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPA_GLOBAL = "cn=GROUPA_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPB_GLOBAL = "cn=GROUPB_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPC_GLOBAL = "cn=GROUPC_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPD_GLOBAL = "cn=GROUPD_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPE_GLOBAL = "cn=GROUPE_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPF_GLOBAL = "cn=GROUPF_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPG_GLOBAL = "cn=GROUPG_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPH_GLOBAL = "cn=GROUPH_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
CONTAINER_1_DELADD = "ou=Product Development,{}".format(DEFAULT_SUFFIX)
CONTAINER_2_DELADD = "ou=Accounting,{}".format(DEFAULT_SUFFIX)


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
def add_test_user(request, topo):
    for demo in ['Product Development', 'Accounting', 'Product Testing', 'nestedgroup', 'ACLGroup']:
        OrganizationalUnit(topo.standalone, "ou={},{}".format(demo, DEFAULT_SUFFIX)).create(properties={'ou': demo})

    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=Accounting')
    for demo1 in ['Ted Morris', 'David Miller']:
        user.create(properties= {
            'uid': demo1,
            'cn': demo1,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + demo1,
            'userPassword': PW_DM
        })

    # Add anonymous access aci
    ACI_TARGET = "(targetattr=\"*\")(target = \"ldap:///%s\")" % (DEFAULT_SUFFIX)
    ACI_ALLOW = "(version 3.0; acl \"Anonymous Read access\"; allow (read,search,compare)"
    ACI_SUBJECT = "(userdn=\"ldap:///anyone\");)"
    ANON_ACI = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    suffix.add('aci', ANON_ACI)

    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX, 'ou=nestedgroup')
    for demo1 in ['DEEPUSER_GLOBAL', 'scratchEntry', 'DEEPUSER2_GLOBAL', 'DEEPUSER1_GLOBAL',
                  'DEEPUSER3_GLOBAL', 'GROUPDNATTRSCRATCHENTRY_GLOBAL', 'newChild']:
        uas.create(properties={
            'uid': demo1,
            'cn': demo1,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + demo1,
            'userPassword': PW_DM
        })

    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX, 'uid=GROUPDNATTRSCRATCHENTRY_GLOBAL,ou=nestedgroup')
    for demo1 in ['c1', 'CHILD1_GLOBAL']:
        uas.create(properties={
            'uid': demo1,
            'cn': demo1,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + demo1,
            'userPassword': PW_DM
        })

    grp = UniqueGroups(topo.standalone, DEFAULT_SUFFIX, rdn='ou=nestedgroup')
    for i in [('ALLGROUPS_GLOBAL', GROUPA_GLOBAL), ('GROUPA_GLOBAL', GROUPB_GLOBAL), ('GROUPB_GLOBAL', GROUPC_GLOBAL),
              ('GROUPC_GLOBAL', GROUPD_GLOBAL), ('GROUPD_GLOBAL', GROUPE_GLOBAL), ('GROUPE_GLOBAL', GROUPF_GLOBAL),
              ('GROUPF_GLOBAL', GROUPG_GLOBAL), ('GROUPG_GLOBAL', GROUPH_GLOBAL), ('GROUPH_GLOBAL', DEEPUSER_GLOBAL)]:
        grp.create(properties={'cn': i[0],
                               'ou': 'groups',
                               'uniquemember': i[1]
                               })

    grp = UniqueGroup(topo.standalone, 'cn=BIG_GLOBAL Group,{}'.format(DEFAULT_SUFFIX))
    grp.create(properties={'cn': 'BIG_GLOBAL Group',
                           'ou': 'groups',
                           'uniquemember': ["uid=Ted Morris,ou=Accounting,{}".format(DEFAULT_SUFFIX),
                                            "uid=David Miller,ou=Accounting,{}".format(DEFAULT_SUFFIX),]
                           })


def test_caching_changes(topo, aci_of_user, add_test_user):
    """
        Add user and then test deny

        :id: 26ed2dc2-783f-11e8-b1a5-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="roomnumber")(version 3.0; acl "ACLGroup"; deny ( read, search ) userdn = "ldap:///all" ;)')
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX, "ou=AclGroup").create_test_user()
    user.set('roomnumber', '3445')
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    # targetattr="roomnumber" will be denied access
    user = UserAccount(conn, 'uid=test_user_1000,ou=ACLGroup,dc=example,dc=com')
    with pytest.raises(AssertionError):
        assert user.get_attr_val_utf8('roomNumber')
    UserAccount(topo.standalone, 'uid=test_user_1000,ou=ACLGroup,dc=example,dc=com').delete()


def test_deny_group_member_all_rights_to_user(topo, aci_of_user, add_test_user):
    """
        Try deleting user while no access

        :id: 0da68a4c-7840-11e8-98c2-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. delete test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; acl "ACLGroup"; deny (all) groupdn = "ldap:///{}" ;)'.format(BIG_GLOBAL))
    conn = UserAccount(topo.standalone, "uid=Ted Morris, ou=Accounting, {}".format(DEFAULT_SUFFIX)).bind(PW_DM)
    # group BIG_GLOBAL will have no access
    user = UserAccount(conn, DEEPUSER3_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.delete()


def test_deny_group_member_all_rights_to_group_members(topo, aci_of_user, add_test_user):
    """
        Deny group member all rights

        :id: 2d4ff70c-7840-11e8-8472-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; acl "ACLGroup"; deny (all) groupdn = "ldap:///{}" ;)'.format(BIG_GLOBAL))
    UserAccounts(topo.standalone, DEFAULT_SUFFIX, "ou=AclGroup").create_test_user()
    conn = UserAccount(topo.standalone, "uid=Ted Morris, ou=Accounting, {}".format(DEFAULT_SUFFIX)).bind(PW_DM)
    # group BIG_GLOBAL no access
    user = UserAccount(conn, 'uid=test_user_1000,ou=ACLGroup,dc=example,dc=com')
    with pytest.raises(IndexError):
        user.get_attr_val_utf8('uid')
    UserAccount(topo.standalone, 'uid=test_user_1000,ou=ACLGroup,dc=example,dc=com').delete()


def test_deeply_nested_groups_aci_denial(topo, add_test_user, aci_of_user):
    """
        Test deeply nested groups (1)
        This aci will not allow search or modify to a user too deep to be detected.

        :id: 3d98229c-7840-11e8-9f55-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; acl "ACLGroup"; allow (all) groupdn = "ldap:///{}" ;)'.format(ALLGROUPS_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    # ALLGROUPS_GLOBAL have all access
    assert UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL).get_attr_val_utf8('uid') == 'scratchEntry'
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.delete()


def test_deeply_nested_groups_aci_denial_two(topo, add_test_user, aci_of_user):
    """
        Test deeply nested groups (2)
        This aci will allow search and modify

        :id: 4ef6348e-7840-11e8-a70c-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; acl "ACLGroup"; allow (all) groupdn = "ldap:///{}" ;)'.format(GROUPE_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    # GROUPE_GLOBAL have all access
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    user.add("sn", "Fred")
    user.remove("sn", "Fred")


def test_deeply_nested_groups_aci_allow(topo, add_test_user, aci_of_user):
    """
        Test deeply nested groups (3)
        This aci will allow search and modify

        :id: 8d338210-7840-11e8-8584-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", ['(targetattr="*")(version 3.0; acl "ACLGroup"; allow (all) groupdn = "ldap:///{}" ;)'.format(ALLGROUPS_GLOBAL), '(targetattr="*")(version 3.0; acl "ACLGroup"; allow (all) groupdn = "ldap:///{}" ;)'.format(GROUPE_GLOBAL)])
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    # test deeply nested groups
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    user.add("sn", "Fred")
    user.remove("sn", "Fred")


def test_deeply_nested_groups_aci_allow_two(topo, add_test_user, aci_of_user):
    """
        This aci will not allow search or modify to a user too deep to be detected.

        :id: 8d3459c4-7840-11e8-8ed8-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; acl "ACLGroup"; allow (all) groupdn = "ldap:///{}" ;)'.format(ALLGROUPS_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    # This aci should not allow search or modify to a user too deep to be detected.
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.add("sn", "Fred")
    assert user.get_attr_val_utf8('uid') == 'scratchEntry'


def test_undefined_in_group_eval(topo, add_test_user, aci_of_user):
    """

        This aci will not allow access .

        :id: f1605e16-7840-11e8-b954-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; acl "ACLGroup"; allow (all) groupdn != "ldap:///{}" ;)'.format(ALLGROUPS_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    # This aci should NOT allow access
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.add("sn", "Fred")
    assert user.get_attr_val_utf8('uid') == 'scratchEntry'


def test_undefined_in_group_eval_two(topo, add_test_user, aci_of_user):
    """
        This aci will allow access

        :id: fcfbcce2-7840-11e8-ba77-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) groupdn = "ldap:///{}\ || ldap:///{}";)'.format(ALLGROUPS_GLOBAL, GROUPG_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    # This aci should  allow access
    user.add("sn", "Fred")
    assert UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL).get_attr_val_utf8('uid') == 'scratchEntry'
    user.remove("sn", "Fred")


def test_undefined_in_group_eval_three(topo, add_test_user, aci_of_user):
    """
        This aci will allow access

        :id: 04943dcc-7841-11e8-8c46-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) groupdn = "ldap:///{}\ || ldap:///{}";)'.format(GROUPG_GLOBAL, ALLGROUPS_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    user = Domain(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    # test UNDEFINED in group
    user.add("sn", "Fred")
    assert UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL).get_attr_val_utf8('uid') == 'scratchEntry'
    user.remove("sn", "Fred")


def test_undefined_in_group_eval_four(topo, add_test_user, aci_of_user):
    """
        This aci will not allow access

        :id: 0b03d10e-7841-11e8-9341-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fullfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) groupdn != "ldap:///{}\ || ldap:///{}";)'.format(ALLGROUPS_GLOBAL, GROUPG_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER1_GLOBAL).bind(PW_DM)
    # test UNDEFINED in group
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.add("sn", "Fred")
    assert user.get_attr_val_utf8('uid') == 'scratchEntry'


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
