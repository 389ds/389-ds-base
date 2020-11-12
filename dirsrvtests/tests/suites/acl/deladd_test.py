# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
Importing necessary Modules.
"""

import os
import pytest

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.group import Groups
from lib389.idm.organizationalunit import OrganizationalUnit, OrganizationalUnits
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain

import ldap

pytestmark = pytest.mark.tier1


USER_WITH_ACI_DELADD = 'uid=test_user_1000,ou=People,dc=example,dc=com'
USER_DELADD = 'uid=test_user_1,ou=Accounting,dc=example,dc=com'


@pytest.fixture(scope="function")
def _aci_of_user(request, topo):
    """
    Removes and Restores ACIs after the test.
    """
    aci_list = Domain(topo.standalone, DEFAULT_SUFFIX).get_attr_vals('aci')

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
def _add_user(request, topo):
    """
    This function will create user for the test and in the end entries will be deleted .
    """

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user()
    user.set("userPassword", PW_DM)

    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ous.create(properties={'ou':'Accounting'})

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=Accounting')
    for i in range(1, 3):
        user = users.create_test_user(uid=i, gid=i)
        user.set("userPassword", PW_DM)

    def fin():
        """
        Deletes entries after the test.
        """
        users1 = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
        for dn_dn in users1.list():
            dn_dn.delete()

        groups = Groups(topo.standalone, DEFAULT_SUFFIX)
        for dn_dn in groups.list():
            dn_dn.delete()

        ou_ou = OrganizationalUnit(topo.standalone, f'ou=Accounting,{DEFAULT_SUFFIX}')
        ou_ou.delete()

    request.addfinalizer(fin)


def test_allow_delete_access_to_groupdn(topo, _add_user, _aci_of_user):

    """Test allow delete access to groupdn

    :id: 7cf15992-68ad-11e8-85af-54e1ad30572c
    :setup: topo.standalone
    :steps:
        1. Add test entry
        2. Add ACI that allows groupdn to delete
        3. Delete something using test USER_DELADD
        4. Remove ACI
    :expectedresults:
        1. Entry should be added
        2. ACI should be added
        3. Delete operation should succeed
        4. Delete operation for ACI should succeed
    """
    # Create Group and add member
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={"cn": "group1",
                                      "description": "testgroup"})
    group.add_member(USER_WITH_ACI_DELADD)

    # set aci
    aci_target = f'(targetattr="*")'
    aci_allow = f'(version 3.0; acl "All rights for {group.dn}"; allow (delete) '
    aci_subject = f'groupdn="ldap:///{group.dn}";)'

    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", (aci_target + aci_allow + aci_subject))

    # create connection with USER_WITH_ACI_DELADD
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)

    # Perform delete operation
    for i in [USER_DELADD, USER_WITH_ACI_DELADD]:
        UserAccount(conn, i).delete()


def test_allow_add_access_to_anyone(topo, _add_user, _aci_of_user):

    """Test to allow add access to anyone

    :id: 5ca31cc4-68e0-11e8-8666-8c16451d917b
    :setup: topo.standalone
    :steps:
        1. Add test entry
        2. Add ACI that allows groupdn to add
        3. Add something using test USER_DELADD
        4. Remove ACI
    :expectedresults:
        1. Entry should be added
        2. ACI should be added
        3. Add operation should succeed
        4. Delete operation for ACI should succeed
    """
    # set aci
    aci_target = f'(targetattr="*")'
    aci_allow = f'(version 3.0; acl "All rights for anyone"; allow (add) '
    aci_subject = f'userdn="ldap:///anyone";)'
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", (aci_target + aci_allow + aci_subject))

    # create connection with USER_WITH_ACI_DELADD
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)

    # Perform add operation
    users = UserAccounts(conn, DEFAULT_SUFFIX, rdn='ou=Accounting')
    user = users.create_test_user(gid=3, uid=3)
    assert user.exists()

    users = UserAccounts(conn, DEFAULT_SUFFIX)
    user = users.create_test_user(gid=3, uid=3)
    assert user.exists()


def test_allow_delete_access_to_anyone(topo, _add_user, _aci_of_user):

    """Test to allow delete access to anyone

    :id: f5447c7e-68e1-11e8-84c4-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI that allows groupdn  to delete some userdn
        3. Delete  something using test USER_DELADD
        4. Remove ACI
    :expectedresults:
        1. Entry should be added
        2. ACI should be added
        3. Operation should  succeed
        4. Delete operation for ACI should succeed
    """
    # set aci
    aci_target = f'(targetattr="*")'
    aci_allow = f'(version 3.0; acl "All rights for anyone"; allow (delete) '
    aci_subject = f'userdn="ldap:///anyone";)'

    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", (aci_target + aci_allow + aci_subject))

    # create connection with USER_WITH_ACI_DELADD
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)

    # Perform delete operation
    UserAccount(conn, USER_DELADD).delete()


def test_allow_delete_access_not_to_userdn(topo, _add_user, _aci_of_user):

    """Test to Allow delete access to != userdn

    :id: 00637f6e-68e3-11e8-92a3-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI that allows userdn  not to delete some userdn
        3. Delete  something using test USER_DELADD
        4. Remove ACI
    :expectedresults:
        1. Entry should be added
        2. ACI should be added
        3. Operation should  not succeed
        4. Delete operation for ACI should succeed
    """
    # set aci
    aci_target = f'(targetattr="*")'
    aci_allow = f'(version 3.0; acl "All rights for %s"; allow (delete) ' % USER_DELADD
    aci_subject = f'userdn!="ldap:///{USER_WITH_ACI_DELADD}";)'

    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", (aci_target + aci_allow + aci_subject))

    # create connection with USER_WITH_ACI_DELADD
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)

    # Perform delete operation
    user = UserAccount(conn, USER_DELADD)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.delete()


def test_allow_delete_access_not_to_group(topo, _add_user, _aci_of_user):

    """Test to Allow delete access to != groupdn

    :id: f58fc8b0-68e5-11e8-9313-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI that allows groupdn  not to delete some userdn
        3. Delete  something using test USER_DELADD belong to test group
        4. Remove ACI
    :expectedresults:
        1. Entry should be added
        2. ACI should be added
        3. Operation should  not succeed
        4. Delete operation for ACI should succeed
    """
    # Create group
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={"cn": "group1",
                                      "description": "testgroup"})
    group.add_member(USER_WITH_ACI_DELADD)

    # set aci
    aci_target = f'(targetattr="*")'
    aci_allow = f'(version 3.0; acl "All rights for {group.dn}"; allow (delete)'
    aci_subject = f'groupdn!="ldap:///{group.dn}";)'

    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", (aci_target + aci_allow + aci_subject))

    # create connection with USER_WITH_ACI_DELADD
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    user = UserAccount(conn, USER_DELADD)

    # Perform delete operation
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.delete()


def test_allow_add_access_to_parent(topo, _add_user, _aci_of_user):

    """Test to Allow add privilege to parent

    :id: 9f099845-9dbc-412f-bdb9-19a5ea729694
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI that Allow add privilege to parent
        3. Add something using test USER_DELADD
        4. Remove ACI
    :expectedresults:
        1. Entry should be added
        2. ACI should be added
        3. Operation should   succeed
        4. Delete operation for ACI should succeed
    """
    # set aci
    aci_target = f'(targetattr="*")'
    aci_allow = f'(version 3.0; acl "All rights for parent"; allow (add) '
    aci_subject = f'userdn="ldap:///parent";)'

    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", (aci_target + aci_allow + aci_subject))

    # create connection with USER_WITH_ACI_DELADD
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)

    # Perform Allow add privilege to parent
    users = UserAccounts(conn, DEFAULT_SUFFIX, rdn='uid=test_user_1000, ou=people')
    user = users.create_test_user(gid=1, uid=1)
    assert user.exists()

    # Delete created user
    UserAccounts(topo.standalone, DEFAULT_SUFFIX).get('test_user_1').delete()


def test_allow_delete_access_to_parent(topo, _add_user, _aci_of_user):

    """Test to Allow delete access to parent

    :id: 2dd7f624-68e7-11e8-8591-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI that Allow delete privilege to parent
        3. Delete something using test USER_DELADD
        4. Remove ACI
    :expectedresults:
        1. Entry should be added
        2. ACI should be added
        3. Operation should   succeed
        4. Delete operation for ACI should succeed
    """
    # set aci
    aci_target = f'(targetattr="*")'
    aci_allow = f'(version 3.0; acl "All rights for parent"; allow (add,delete) '
    aci_subject = f'userdn="ldap:///parent";)'

    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", (aci_target + aci_allow + aci_subject))

    # create connection with USER_WITH_ACI_DELADD
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)

    # Create a user with parent 'uid=test_user_1000, ou=people, {}'.format(DEFAULT_SUFFIX)
    users = UserAccounts(conn, DEFAULT_SUFFIX, rdn='uid=test_user_1000, ou=people')
    new_user = users.create_test_user(gid=1, uid=1)
    assert new_user.exists()

    # Perform Allow delete access to parent
    new_user.delete()


def test_allow_delete_access_to_dynamic_group(topo, _add_user, _aci_of_user, request):

    """Test to Allow delete access to dynamic group

    :id: 14ffa452-68ed-11e8-a60d-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI that Allow delete privilege to dynamic group
        3. Delete something using test USER_DELADD
        4. Remove ACI
    :expectedresults:
        1. Entry should be added
        2. ACI should be added
        3. Operation should   succeed
        4. Delete operation for ACI should succeed
    """
    # Create dynamic group
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={"cn": "group1",
                                      "description": "testgroup"})

    group.add("objectclass", "groupOfURLs")
    group.add("memberURL",
              f"ldap:///dc=example,dc=com??sub?(&(objectclass=person)(uid=test_user_1000))")

    # Set ACI
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add("aci", f'(target = ldap:///{DEFAULT_SUFFIX})(targetattr="*")'
                   f'(version 3.0; acl "{request.node.name}"; '
                   f'allow (delete) (groupdn = "ldap:///{group.dn}"); )')

    # create connection with USER_WITH_ACI_DELADD
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)

    # Perform Allow delete access to dynamic group
    UserAccount(conn, USER_DELADD).delete()


def test_allow_delete_access_to_dynamic_group_uid(topo, _add_user, _aci_of_user, request):

    """Test to Allow delete access to dynamic group

    :id: 010a4f20-752a-4173-b763-f520c7a85b82
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI that Allow delete privilege to dynamic group
        3. Delete something using test USER_DELADD
        4. Remove ACI
    :expectedresults:
        1. Entry should be added
        2. ACI should be added
        3. Operation should   succeed
        4. Delete operation for ACI should succeed
    """
    # Create dynamic group
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={"cn": "group1",
                                      "description": "testgroup"})

    group.add("objectclass", "groupOfURLs")
    group.add("memberURL",
              f'ldap:///{DEFAULT_SUFFIX}??sub?(&(objectclass=person)(cn=test_user_1000))')

    # Set ACI
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add("aci", f'(target = ldap:///{DEFAULT_SUFFIX})'
                   f'(targetattr="uid")(version 3.0; acl "{request.node.name}"; '
                   f'allow (delete) (groupdn = "ldap:///{group.dn}"); )')

    # create connection with USER_WITH_ACI_DELADD
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)

    # Perform Allow delete access to dynamic group
    UserAccount(conn, USER_DELADD).delete()


def test_allow_delete_access_not_to_dynamic_group(topo, _add_user, _aci_of_user, request):

    """Test to  Allow delete access to != dynamic group

    :id: 9ecb139d-bca8-428e-9044-fd89db5a3d14
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI that delete access to != dynamic group
        3. Delete something using test USER_DELADD
        4. Remove ACI
    :expectedresults:
        1. Entry should be added
        2. ACI should be added
        3. Operation should  not succeed
        4. Delete operation for ACI should succeed
    """
    # Create dynamic group
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={"cn": "group1",
                                      "description": "testgroup"})
    group.add("objectclass", "groupOfURLs")
    group.add("memberURL",
              f'ldap:///{DEFAULT_SUFFIX}??sub?(&(objectclass=person)(cn=test_user_1000))')

    # Set ACI
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add("aci", f'(target = ldap:///{DEFAULT_SUFFIX})'
                   f'(targetattr="*")(version 3.0; acl "{request.node.name}"; '
                   f'allow (delete) (groupdn != "ldap:///{group.dn}"); )')

    # create connection with USER_WITH_ACI_DELADD
    conn = UserAccount(topo.standalone, USER_WITH_ACI_DELADD).bind(PW_DM)
    user = UserAccount(conn, USER_DELADD)

    # Perform Allow delete access to != dynamic group
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.delete()


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
