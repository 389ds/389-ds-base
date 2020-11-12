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

NESTEDGROUP_OU_GLOBAL = "ou=nestedgroup, {}".format(DEFAULT_SUFFIX)
DEEPUSER_GLOBAL = "uid=DEEPUSER_GLOBAL, {}".format(NESTEDGROUP_OU_GLOBAL)
DEEPUSER2_GLOBAL = "uid=DEEPUSER2_GLOBAL, {}".format(NESTEDGROUP_OU_GLOBAL)
DEEPUSER3_GLOBAL = "uid=DEEPUSER3_GLOBAL, {}".format(NESTEDGROUP_OU_GLOBAL)
DEEPGROUPSCRATCHENTRY_GLOBAL = "uid=scratchEntry,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPDNATTRSCRATCHENTRY_GLOBAL = "uid=GROUPDNATTRSCRATCHENTRY_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPDNATTRCHILDSCRATCHENTRY_GLOBAL = "uid=c1,{}".format(GROUPDNATTRSCRATCHENTRY_GLOBAL)
NEWCHILDSCRATCHENTRY_GLOBAL = "uid=newChild,{}".format(NESTEDGROUP_OU_GLOBAL)
ALLGROUPS_GLOBAL = "cn=ALLGROUPS_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPA_GLOBAL = "cn=GROUPA_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPB_GLOBAL = "cn=GROUPB_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPC_GLOBAL = "cn=GROUPC_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPD_GLOBAL = "cn=GROUPD_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPE_GLOBAL = "cn=GROUPE_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPF_GLOBAL = "cn=GROUPF_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPG_GLOBAL = "cn=GROUPG_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
GROUPH_GLOBAL = "cn=GROUPH_GLOBAL,{}".format(NESTEDGROUP_OU_GLOBAL)
CHILD1_GLOBAL = "uid=CHILD1_GLOBAL,{}".format(GROUPDNATTRSCRATCHENTRY_GLOBAL)
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
def test_user(request, topo):
    for demo in ['Product Development', 'Accounting', 'nestedgroup']:
        OrganizationalUnit(topo.standalone, "ou={},{}".format(demo, DEFAULT_SUFFIX)).create(properties={'ou': demo})

    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX, 'ou=nestedgroup')
    for demo1 in ['DEEPUSER_GLOBAL', 'scratchEntry', 'DEEPUSER2_GLOBAL',
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

    # Add anonymous access aci
    ACI_TARGET = "(targetattr=\"*\")(target = \"ldap:///%s\")" % (DEFAULT_SUFFIX)
    ACI_ALLOW = "(version 3.0; acl \"Anonymous Read access\"; allow (read,search,compare)"
    ACI_SUBJECT = "(userdn=\"ldap:///anyone\");)"
    ANON_ACI = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    suffix.add('aci', ANON_ACI)

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


def test_undefined_in_group_eval_five(topo, test_user, aci_of_user):
    """
        Aci will not allow access as Group dn is not allowed so members will not allowed access.

        :id: 11451a96-7841-11e8-9f79-8c16451d917b
        :setup: server
        :steps:
            1. Add test entry
            2. Take a count of users using DN_DM
            3. Add test user
            4. add aci
            5. test should fulfil the aci rules
        :expectedresults:
            1. Entry should be added
            2. Operation should  succeed
            3. Operation should  succeed
            4. Operation should  succeed
            5. Operation should  succeed
    """

    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) groupdn != "ldap:///{}\ || ldap:///{}";)'.format(ALLGROUPS_GLOBAL, GROUPF_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER2_GLOBAL).bind(PW_DM)
    # This aci should NOT allow access
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace("description", "Fred")
    assert user.get_attr_val_utf8('uid') == 'scratchEntry'


def test_undefined_in_group_eval_six(topo, test_user, aci_of_user):
    """
        Aci will not allow access as tested user is not a member of allowed Group dn

        :id: 1904572e-7841-11e8-a9d8-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) groupdn = "ldap:///{} || ldap:///{}" ;)'.format(GROUPH_GLOBAL, ALLGROUPS_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER3_GLOBAL).bind(PW_DM)
    # test UNDEFINED in group
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace("description", "Fred")
    assert user.get_attr_val_utf8('uid') == 'scratchEntry'


def test_undefined_in_group_eval_seven(topo, test_user, aci_of_user):
    """
        Aci will not allow access as tested user is not a member of allowed Group dn

        :id: 206b43c4-7841-11e8-b3ed-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) groupdn = "ldap:///{}\ || ldap:///{}";)'.format(ALLGROUPS_GLOBAL, GROUPH_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER3_GLOBAL).bind(PW_DM)
    # test UNDEFINED in group
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace("description", "Fred")
    assert user.get_attr_val_utf8('uid') == 'scratchEntry'


def test_undefined_in_group_eval_eight(topo, test_user, aci_of_user):
    """
        Aci will not allow access as Group dn is not allowed so members will not allowed access.

        :id: 26ca7456-7841-11e8-801e-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) groupdn != "ldap:///{} || ldap:///{} || ldap:///{}" ;)'.format(GROUPH_GLOBAL, GROUPA_GLOBAL, ALLGROUPS_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER3_GLOBAL).bind(PW_DM)
    # test UNDEFINED in group
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace("description", "Fred")
    assert user.get_attr_val_utf8('uid') == 'scratchEntry'


def test_undefined_in_group_eval_nine(topo, test_user, aci_of_user):
    """
        Aci will not allow access as Group dn is not allowed so members will not allowed access.

        :id: 38c7fbb0-7841-11e8-90aa-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) groupdn != "ldap:///{}\ || ldap:///{} || ldap:///{}";)'.format(ALLGROUPS_GLOBAL, GROUPA_GLOBAL, GROUPH_GLOBAL))
    conn = UserAccount(topo.standalone, DEEPUSER3_GLOBAL).bind(PW_DM)
    # test UNDEFINED in group
    user = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace("sn", "Fred")
    assert user.get_attr_val_utf8('uid') == 'scratchEntry'


def test_undefined_in_group_eval_ten(topo, test_user, aci_of_user):
    """
        Test the userattr keyword to ensure that it evaluates correctly.

        :id: 46c0fb72-7841-11e8-af1d-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) userattr = "description#GROUPDN";)')
    user = UserAccount(topo.standalone, DEEPGROUPSCRATCHENTRY_GLOBAL)
    user.add("description", [ALLGROUPS_GLOBAL, GROUPG_GLOBAL])
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    # Test the userattr keyword
    user.add("sn", "Fred")
    assert UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL).get_attr_val_utf8('uid') == 'scratchEntry'
    user.remove("description", [ALLGROUPS_GLOBAL, GROUPG_GLOBAL])


def test_undefined_in_group_eval_eleven(topo, test_user, aci_of_user):
    """
        Aci will not allow access as description is there with the user entry which is not allowed in ACI

        :id: 4cfa28e2-7841-11e8-8117-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) not( userattr = "description#GROUPDN");)')
    user = UserAccount(topo.standalone, DEEPGROUPSCRATCHENTRY_GLOBAL)
    user.add("description", [ALLGROUPS_GLOBAL, GROUPH_GLOBAL])
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    # Test that not(UNDEFINED(attrval1))
    user1 = UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user1.add("sn", "Fred1")
    assert user.get_attr_val_utf8('cn')
    user.remove("description", [ALLGROUPS_GLOBAL, GROUPH_GLOBAL])


def test_undefined_in_group_eval_twelve(topo, test_user, aci_of_user):
    """
        Test with the parent keyord that Yields TRUE as description is present in tested entry

        :id: 54f471ec-7841-11e8-8910-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) userattr = "parent[0,1].description#GROUPDN";)')
    user = UserAccount(topo.standalone, GROUPDNATTRSCRATCHENTRY_GLOBAL)
    user.add("description", [ALLGROUPS_GLOBAL, GROUPD_GLOBAL])
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    # Test with the parent keyord
    UserAccount(conn, GROUPDNATTRCHILDSCRATCHENTRY_GLOBAL).add("sn", "Fred")
    assert UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL).get_attr_val_utf8('cn')
    user.remove("description", [ALLGROUPS_GLOBAL, GROUPD_GLOBAL])


def test_undefined_in_group_eval_fourteen(topo, test_user, aci_of_user):
    """
        Test with parent keyword that Yields FALSE as description is not present in tested entry

        :id: 5c527218-7841-11e8-8909-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) userattr = "parent[0,1].description#GROUPDN";)')
    user = UserAccount(topo.standalone, GROUPDNATTRSCRATCHENTRY_GLOBAL)
    user.add("description", [ALLGROUPS_GLOBAL, GROUPG_GLOBAL])
    conn = UserAccount(topo.standalone, DEEPUSER2_GLOBAL).bind(PW_DM)
    # Test with parent keyword
    user1 = UserAccount(conn, GROUPDNATTRCHILDSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user1.add("sn", "Fred")
    assert UserAccount(conn, DEEPGROUPSCRATCHENTRY_GLOBAL).get_attr_val_utf8('cn')
    user.remove("description", [ALLGROUPS_GLOBAL, GROUPG_GLOBAL])


def test_undefined_in_group_eval_fifteen(topo, test_user, aci_of_user):
    """
        Here do the same tests for userattr  with the parent keyword.

        :id: 6381c070-7841-11e8-a6b6-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) userattr = "parent[0,1].description#USERDN";)')
    UserAccount(topo.standalone, NESTEDGROUP_OU_GLOBAL).add("description", DEEPUSER_GLOBAL)
    # Here do the same tests for userattr  with the parent keyword.
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    UserAccount(conn, NEWCHILDSCRATCHENTRY_GLOBAL).add("description", DEEPUSER_GLOBAL)


def test_undefined_in_group_eval_sixteen(topo, test_user, aci_of_user):
    """
        Test with parent keyword with not key

        :id: 69852688-7841-11e8-8db1-8c16451d917b
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
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    domain.add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) not ( userattr = "parent[0,1].description#USERDN");)')
    domain.add("description", DEEPUSER_GLOBAL)
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    # Test with parent keyword with not key
    user = UserAccount(conn, NEWCHILDSCRATCHENTRY_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.add("description",DEEPUSER_GLOBAL)


def test_undefined_in_group_eval_seventeen(topo, test_user, aci_of_user):
    """
        Test with the parent keyord that Yields TRUE as description is present in tested entry

        :id: 7054d1c0-7841-11e8-8177-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) userattr = "parent[0,1].description#GROUPDN";)')
    user = UserAccount(topo.standalone, GROUPDNATTRSCRATCHENTRY_GLOBAL)
    # Test with the parent keyord
    user.add("description", [ALLGROUPS_GLOBAL, GROUPD_GLOBAL])
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    UserAccount(conn, CHILD1_GLOBAL).add("description", DEEPUSER_GLOBAL)
    user.remove("description", [ALLGROUPS_GLOBAL, GROUPD_GLOBAL])


def test_undefined_in_group_eval_eighteen(topo, test_user, aci_of_user):
    """
        Test with parent keyword with not key

        :id: 768b9ab0-7841-11e8-87c3-8c16451d917b
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
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci",'(targetattr="*")(version 3.0; aci "tester"; allow(all) not (userattr = "parent[0,1].description#GROUPDN" );)')
    user = UserAccount(topo.standalone, GROUPDNATTRSCRATCHENTRY_GLOBAL)
    # Test with parent keyword with not key
    user.add("description", [ALLGROUPS_GLOBAL, GROUPH_GLOBAL])
    conn = UserAccount(topo.standalone, DEEPUSER_GLOBAL).bind(PW_DM)
    user = UserAccount(conn, CHILD1_GLOBAL)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.add("description", DEEPUSER_GLOBAL)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
