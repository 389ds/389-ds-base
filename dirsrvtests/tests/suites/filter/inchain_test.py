# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 RED Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

import pytest, os, re
from lib389.tasks import *
from lib389.utils import *
from ldap import SCOPE_SUBTREE, ALREADY_EXISTS

from lib389._constants import DEFAULT_SUFFIX, PW_DM, PLUGIN_MEMBER_OF
from lib389.topologies import topology_st as topo
from lib389.plugins import MemberOfPlugin

from lib389.schema import Schema
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.account import Accounts
from lib389.idm.account import Anonymous

INCHAIN_OID = "1.2.840.113556.1.4.1941"

pytestmark = pytest.mark.tier0

@pytest.fixture(scope="function")
def provision_inchain(topo):
    """fixture that provision a hierachical tree
    with 'manager' membership relation

    """
    # hierarchy of the entries is
    # 3_1
    #  |__ 2_1
    #  |    |__ 1_1
    #  |    |__ 100
    #   |    |    |__ 101
    #   |    |    |__ 102
    #   |    |    |__ 103
    #   |    |    |__ 104
    #   |    |
    #   |    |__ 1_2
    #   |    |    |__ 200
    #   |    |    |__ 201
    #   |    |    |__ 202
    #   |    |    |__ 203
    #   |    |    |__ 204
    #   |
    #   |__ 2_2
    #        |__ 1_3
    #        |    |__ 300
    #        |    |__ 301
    #        |    |__ 302
    #        |    |__ 303
    #        |    |__ 304
    #        |
    #        |__ 1_4
    #             |__ 400
    #             |__ 401
    #             |__ 402
    #             |__ 403
    #             |__ 404
    #
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    try:
        manager_lvl_3_1 = user.create_test_user(uid=31)
    except ALREADY_EXISTS:
        manager_lvl_3_1 = None
        pass

    try:
        manager_lvl_2_1 = user.create_test_user(uid=21)
        if manager_lvl_3_1:
            manager_lvl_2_1.set("manager", manager_lvl_3_1.dn)
        else:
            manager_lvl_2_1.set("manager", "uid=test_user_31,ou=People,%s" % DEFAULT_SUFFIX)
    except ALREADY_EXISTS:
        manager_lvl_2_1 = None
        pass

    try:
        manager_lvl_2_2 = user.create_test_user(uid=22)
        if manager_lvl_3_1:
            manager_lvl_2_2.set("manager", manager_lvl_3_1.dn)
        else:
            manager_lvl_2_2.set("manager", "uid=test_user_31,ou=People,%s" % DEFAULT_SUFFIX)
    except ALREADY_EXISTS:
        manager_lvl_2_2 = None
        pass

    try:
        manager_lvl_1_1 = user.create_test_user(uid=11)
        if manager_lvl_2_1:
            manager_lvl_1_1.set("manager", manager_lvl_2_1.dn)
        else:
            manager_lvl_1_1.set("manager", "uid=test_user_21,ou=People,%s" % DEFAULT_SUFFIX)
    except ALREADY_EXISTS:
        manager_lvl_1_1 = None
        pass

    try:
        manager_lvl_1_2 = user.create_test_user(uid=12)
        if manager_lvl_2_1:
            manager_lvl_1_2.set("manager", manager_lvl_2_1.dn)
        else:
            manager_lvl_1_2.set("manager", "uid=test_user_21,ou=People,%s" % DEFAULT_SUFFIX)
    except ALREADY_EXISTS:
        manager_lvl_1_2 = None
        pass

    try:
        manager_lvl_1_3 = user.create_test_user(uid=13)
        if manager_lvl_2_2:
            manager_lvl_1_3.set("manager", manager_lvl_2_2.dn)
        else:
            manager_lvl_1_3.set("manager", "uid=test_user_22,ou=People,%s" % DEFAULT_SUFFIX)
    except ALREADY_EXISTS:
        manager_lvl_1_3 = None
        pass

    try:
        manager_lvl_1_4 = user.create_test_user(uid=14)
        if manager_lvl_2_2:
            manager_lvl_1_4.set("manager", manager_lvl_2_2.dn)
        else:
            manager_lvl_1_4.set("manager", "uid=test_user_22,ou=People,%s" % DEFAULT_SUFFIX)
    except ALREADY_EXISTS:
        manager_lvl_1_4 = None
        pass

    for i in range(100, 105):
        try:
            user1 = user.create_test_user(uid=i)
            if manager_lvl_1_1:
                user1.set("manager", manager_lvl_1_1.dn)
            else:
                user1.set("manager", "uid=test_user_11,ou=People,%s" % DEFAULT_SUFFIX)
        except ALREADY_EXISTS:
            pass

    for i in range(200, 205):
        try:
            user1 = user.create_test_user(uid=i)
            if manager_lvl_1_2:
                user1.set("manager", manager_lvl_1_2.dn)
            else:
                user1.set("manager", "uid=test_user_12,ou=People,%s" % DEFAULT_SUFFIX)
        except ALREADY_EXISTS:
            pass

    for i in range(300, 305):
        try:
            user1 = user.create_test_user(uid=i)
            if manager_lvl_1_3:
                user1.set("manager", manager_lvl_1_3.dn)
            else:
                user1.set("manager", "uid=test_user_13,ou=People,%s" % DEFAULT_SUFFIX)
        except ALREADY_EXISTS:
            pass

    for i in range(400, 405):
        try:
            user1 = user.create_test_user(uid=i)
            if manager_lvl_1_4:
                user1.set("manager", manager_lvl_1_4.dn)
            else:
                user1.set("manager", "uid=test_user_14,ou=People,%s" % DEFAULT_SUFFIX)
        except ALREADY_EXISTS:
            pass

def check_subordinates(topo, uid, expected):
    """Test filter can search attributes

    :id: 39640b4b-0e64-44a4-8611-191aae412370
    :setup: Standalone instance
    :steps:
        1. Add test entry
        2. make search
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
    """
    manager = "uid=%s,ou=People,%s" % (uid, DEFAULT_SUFFIX)
    topo.standalone.log.info("Subordinate of manager %s" % manager)

    subordinates = topo.standalone.search_s(DEFAULT_SUFFIX, SCOPE_SUBTREE, "(manager:%s:=%s)" % (INCHAIN_OID, manager))
    found = []
    for sub in subordinates:
        p =re.compile("uid=(.*),ou.*$")
        res = p.search(sub.dn)
        found.append(res.group(1))
        topo.standalone.log.info("Subordinate found   : %s" % res.group(1))


    for sub in expected:
        assert sub in found

    for sub in found:
        assert sub in expected


def test_manager_lvl_1(topo, provision_inchain):
    """Test that it succeeds to retrieve the subordinate
    of level 1 manager

    :id: 193040ef-861e-41bd-84c7-c07a53a74e18
    :setup: Standalone instance
    :steps:
        1. fixture provision a hierachical tree
        2. Check subordinates of 1_4 entry
        3. Check subordinates of 1_3 entry
        4. Check subordinates of 1_2 entry
        5. Check subordinates of 1_1 entry
    :expectedresults:
        1. provisioning done
        2. found subordinates should match expected ones
        3. found subordinates should match expected ones
        4. found subordinates should match expected ones
        5. found subordinates should match expected ones
    """

    # Check subordinates of user_14
    #     |__ 1_4
    #          |__ 400
    #          |__ 401
    #          |__ 402
    #          |__ 403
    #          |__ 404
    uid = "test_user_14"
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []
    for i in range(400, 405):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

    # Check subordinates of user_13
    #     |__ 1_3
    #     |    |__ 300
    #     |    |__ 301
    #     |    |__ 302
    #     |    |__ 303
    #     |    |__ 304
    uid = "test_user_13"
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []
    for i in range(300, 305):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

    # Check subordinates of user_12
    #|    |__ 1_2
    #|    |    |__ 200
    #|    |    |__ 201
    #|    |    |__ 202
    #|    |    |__ 203
    #|    |    |__ 204

    uid = "test_user_12"
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []
    for i in range(200, 205):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

    # Check subordinates of user_11
    #|    |__ 1_1
    #|    |    |__ 100
    #|    |    |__ 101
    #|    |    |__ 102
    #|    |    |__ 103
    #|    |    |__ 104
    uid = "test_user_11"
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []
    for i in range(100, 105):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

def test_manager_lvl_2(topo, provision_inchain):
    """Test that it succeeds to retrieve the subordinate
    of level 2 manager

    :id: d0c98211-3b90-4764-913d-f55ea5479029
    :setup: Standalone instance
    :steps:
        1. fixture provision a hierachical tree
        2. Check subordinates of 2_1 entry
        3. Check subordinates of 2_2 entry
    :expectedresults:
        1. provisioning done
        2. found subordinates should match expected ones
        3. found subordinates should match expected ones
    """

    # Check subordinates of user_22
    #|
    #|__ 2_2
    #     |__ 1_3
    #     ...
    #     |__ 1_4
    #     ...
    uid = "test_user_22"
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []

    # it contains user_14 and below
    #     |__ 1_4
    #          |__ 400
    #          |__ 401
    #          |__ 402
    #          |__ 403
    #          |__ 404
    uid_expected = "test_user_14"
    topo.standalone.log.info("Subordinate expected: %s" % uid_expected)
    expected.append(uid_expected)
    for i in range(400, 405):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    # it contains user_13 and below
    #|
    #|__ 2_2
    #     |__ 1_3
    #     |    |__ 300
    #     |    |__ 301
    #     |    |__ 302
    #     |    |__ 303
    #     |    |__ 304
    #     |
    uid_expected = "test_user_13"
    topo.standalone.log.info("Subordinate expected: %s" % uid_expected)
    expected.append(uid_expected)
    for i in range(300, 305):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

    # Check subordinates of user_21
    #|__ 2_1
    #|    |__ 1_1
    #|    |
    #|    ...
    #|    |
    #|    |__ 1_2
    #|    ...
    uid = "test_user_21"
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []

    # it contains user_12 and below
    #|__ 2_1
    #|    ...
    #|    |__ 1_2
    #|    |    |__ 200
    #|    |    |__ 201
    #|    |    |__ 202
    #|    |    |__ 203
    #|    |    |__ 204

    uid_expected = "test_user_12"
    topo.standalone.log.info("Subordinate expected: %s" % uid_expected)
    expected.append(uid_expected)
    for i in range(200, 205):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    # it contains user_11 and below
    #|__ 2_1
    #|    |__ 1_1
    #|    |    |__ 100
    #|    |    |__ 101
    #|    |    |__ 102
    #|    |    |__ 103
    #|    |    |__ 104
    #|    |
    #|    ...
    uid_expected = "test_user_11"
    topo.standalone.log.info("Subordinate expected: %s" % uid_expected)
    expected.append(uid_expected)
    for i in range(100, 105):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

def test_manager_lvl_3(topo, provision_inchain):
    """Test that it succeeds to retrieve the subordinate
    of level 3 manager

    :id: d3708a39-7901-4c88-b4af-272aed2aa846
    :setup: Standalone instance
    :steps:
        1. fixture provision a hierachical tree
        2. Check subordinates of 3_1 entry
    :expectedresults:
        1. provisioning done
        2. found subordinates should match expected ones
    """

    # Check subordinates of user_31
    # 3_1
    #|__ 2_1
    #|    |__ 1_1
    #|    |    |__ 100
    #|    |    |__ 101
    #|    |    |__ 102
    #|    |    |__ 103
    #|    |    |__ 104
    #|    |
    #|    |__ 1_2
    #|    |    |__ 200
    #|    |    |__ 201
    #|    |    |__ 202
    #|    |    |__ 203
    #|    |    |__ 204
    #|
    #|__ 2_2
    #     |__ 1_3
    #     |    |__ 300
    #     |    |__ 301
    #     |    |__ 302
    #     |    |__ 303
    #     |    |__ 304
    #     |
    #     |__ 1_4
    #          |__ 400
    #          |__ 401
    #          |__ 402
    #          |__ 403
    #          |__ 404
    uid = "test_user_31"
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []

    # it contains user_22 and below
    uid_expected = "test_user_22"
    topo.standalone.log.info("Subordinate expected: %s" % uid_expected)
    expected.append(uid_expected)

    # it contains user_14 and below
    uid_expected = "test_user_14"
    topo.standalone.log.info("Subordinate expected: %s" % uid_expected)
    expected.append(uid_expected)
    for i in range(400, 405):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    # it contains user_13 and below
    uid_expected = "test_user_13"
    topo.standalone.log.info("Subordinate expected: %s" % uid_expected)
    expected.append(uid_expected)
    for i in range(300, 305):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)


    # it contains user_21 and below
    uid_expected = "test_user_21"
    topo.standalone.log.info("Subordinate expected: %s" % uid_expected)
    expected.append(uid_expected)

    # it contains user_12 and below
    uid_expected = "test_user_12"
    topo.standalone.log.info("Subordinate expected: %s" % uid_expected)
    expected.append(uid_expected)
    for i in range(200, 205):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    # it contains user_11 and below
    uid_expected = "test_user_11"
    topo.standalone.log.info("Subordinate expected: %s" % uid_expected)
    expected.append(uid_expected)
    for i in range(100, 105):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

def test_recompute_del(topo, provision_inchain):
    """Test that if we delete a subordinate
    the subordinate list is correctly updated

    :id: 6865876d-64b5-41a2-ae6e-4453aae5caab
    :setup: Standalone instance
    :steps:
        1. fixture provision a hierachical tree
        2. Check subordinates of 1_1 entry
        3. Delete user_100
        4. Check subordinates of 1_1 entry
    :expectedresults:
        1. provisioning done
        2. found subordinates should match expected ones
    """

    # Check subordinates of user_11
    #|    |__ 1_1
    #|    |    |__ 100
    #|    |    |__ 101
    #|    |    |__ 102
    #|    |    |__ 103
    #|    |    |__ 104

    uid = "test_user_11"
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []
    for i in range(100, 105):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

    del_dn = "uid=test_user_100,ou=People,%s" % (DEFAULT_SUFFIX)
    user = UserAccount(topo.standalone, del_dn)
    topo.standalone.log.info("Delete: %s" % del_dn)
    user.delete()

    # Check subordinates of user_11
    #|    |__ 1_1
    #|    |    |__ 101
    #|    |    |__ 102
    #|    |    |__ 103
    #|    |    |__ 104
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []
    for i in range(101, 105):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

def test_recompute_add(topo, provision_inchain, request):
    """Test that if we add a subordinate
    the subordinate list is correctly updated

    :id: 60d10233-37c2-400b-ac6e-d29b68706216
    :setup: Standalone instance
    :steps:
        1. fixture provision a hierachical tree
        2. Check subordinates of 1_1 entry
        3. add user_105
        4. Check subordinates of 1_1 entry
    :expectedresults:
        1. provisioning done
        2. found subordinates should match expected ones
    """

    # Check subordinates of user_11
    #|    |__ 1_1
    #|    |    |__ 100
    #|    |    |__ 101
    #|    |    |__ 102
    #|    |    |__ 103
    #|    |    |__ 104
    uid = "test_user_11"
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []
    for i in range(100, 105):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

    # add a new subordinate of user_11
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user_added = users.create_test_user(uid=105)
    topo.standalone.log.info("Add: %s" % user_added.dn)
    user_added.set("manager", "uid=%s,ou=People,%s" % (uid, DEFAULT_SUFFIX))

    # Check subordinates of user_11
    #|    |__ 1_1
    #|    |    |__ 100
    #|    |    |__ 101
    #|    |    |__ 102
    #|    |    |__ 103
    #|    |    |__ 104
    #|    |    |__ 105
    topo.standalone.log.info("Subordinate of uid=%s" % uid)
    expected = []
    for i in range(100, 106):
        uid_expected = "test_user_%s" % i
        expected.append(uid_expected)
        topo.standalone.log.info("Subordinate expected: %s" % uid_expected)

    check_subordinates(topo, uid, expected)

    def fin():
        user_added.delete()

    request.addfinalizer(fin)


def test_anonymous_inchain(topo, provision_inchain):
    """Test that anonymous connection can not
    retrieve subordinates

    :id: d6c41cc1-7c36-4a3f-bcdf-f479c12310e2
    :setup: Standalone instance
    :steps:
        1. fixture provision a hierachical tree
        2. bound anonymous
        3. Check subordinates of 1_2 entry is empty although hierarchy
    :expectedresults:
        1. provisioning done
        2. succeed
        3. succeeds but 0 subordinates
    """

    # create an anonymous connection
    topo.standalone.log.info("Bind as anonymous user")
    conn = Anonymous(topo.standalone).bind()

    # Check that there are no subordinates of test_user_12
    uid = "test_user_12"
    manager = "uid=%s,ou=People,%s" % (uid, DEFAULT_SUFFIX)
    topo.standalone.log.info("Subordinate of manager %s on anonymous connection" % manager)

    subordinates = conn.search_s(DEFAULT_SUFFIX, SCOPE_SUBTREE, "(manager:%s:=%s)" % (INCHAIN_OID, manager))
    assert len(subordinates) == 0

    # Check the ACI right failure
    assert topo.standalone.ds_error_log.match('.*inchain - Requestor is not allowed to use InChain Matching rule$')

def test_authenticated_inchain(topo, provision_inchain, request):
    """Test that bound connection can not
    retrieve subordinates (only DM is allowed by default)

    :id: 17ebee0d-86e2-4f0d-95fe-8a4cf566a493
    :setup: Standalone instance
    :steps:
        1. fixture provision a hierachical tree
        2. create a test user
        3. create a bound connection
        4. Check subordinates of 1_2 entry is empty although hierarchy
    :expectedresults:
        1. provisioning done
        2. succeed
        3. succeed
        4. succeeds but 0 subordinates
    """

    # create a user
    RDN = "test_bound_user"
    test_user = UserAccount(topo.standalone, "uid=%s,ou=People,%s" % (RDN, DEFAULT_SUFFIX))
    test_user.create(properties={
        'uid': RDN,
        'cn': RDN,
        'sn': RDN,
        'userPassword': "password",
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/inchain',
    })

    # create a bound connection
    conn = test_user.bind("password")

    # Check that there are no subordinates of test_user_12
    uid = "test_user_12"
    manager = "uid=%s,ou=People,%s" % (uid, DEFAULT_SUFFIX)
    topo.standalone.log.info("Subordinate of manager %s on bound connection" % manager)

    subordinates = conn.search_s(DEFAULT_SUFFIX, SCOPE_SUBTREE, "(manager:%s:=%s)" % (INCHAIN_OID, manager))
    assert len(subordinates) == 0

    # Check the ACI right failure
    assert topo.standalone.ds_error_log.match('.*inchain - Requestor is not allowed to use InChain Matching rule$')

    def fin():
        test_user.delete()

    request.addfinalizer(fin)


def _create_user(topology_st, ext):
    user_dn = "uid=%s,ou=People,%s" % (ext, DEFAULT_SUFFIX)
    topology_st.standalone.add_s(Entry((user_dn, {
        'objectclass': 'top extensibleObject'.split(),
        'uid': ext
    })))
    topology_st.standalone.log.info("Create user %s" % user_dn)
    return ensure_bytes(user_dn)

def _create_group(topology_st, ext):
    group_dn = "ou=%s,ou=People,%s" % (ext, DEFAULT_SUFFIX)
    topology_st.standalone.add_s(Entry((group_dn, {
        'objectclass': 'top groupOfNames extensibleObject'.split(),
        'ou': ext,
        'cn': ext
    })))
    topology_st.standalone.log.info("Create group %s" % group_dn)
    return ensure_bytes(group_dn)

def test_reuse_memberof(topo, request):
    """Check that slapi_memberof successfully
    compute the membership either using 'memberof' attribute
    or either recomputing it.

    :id: e52bd21a-3ff6-493f-9ec5-8cf4e76696a0
    :setup: Standalone instance
    :steps:
        1. Enable the plugin
        2. Create a user belonging in cascade to 3 groups
        3. Check that slapi_member re-computes membership
        4. Check that slapi_member retrieve membership from memberof
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    # enable the plugin
    topo.standalone.log.info("Enable MemberOf plugin")
    topo.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topo.standalone.restart()

    # Create a user belonging to 3 goups
    # in cascade
    user1 = _create_user(topo, 'user1')

    group1 = _create_group(topo, 'group1')
    mods = [(ldap.MOD_ADD, 'member', user1)]
    topo.standalone.modify_s(ensure_str(group1), mods)

    group2 = _create_group(topo, 'group2')
    mods = [(ldap.MOD_ADD, 'member', group1)]
    topo.standalone.modify_s(ensure_str(group2), mods)

    group3 = _create_group(topo, 'group3')
    mods = [(ldap.MOD_ADD, 'member', group2)]
    topo.standalone.modify_s(ensure_str(group3), mods)

    # Call slapi_member that does *not* reuse the memberof
    # because of 'memberOfEntryScope' not being set
    topo.standalone.log.info("Groups that %s is memberof" % ensure_str(user1))

    topo.standalone.config.set('nsslapd-errorlog-level', '65536') # plugin logging
    memberof = topo.standalone.search_s(DEFAULT_SUFFIX, SCOPE_SUBTREE, "(member:%s:=%s)" % (INCHAIN_OID, ensure_str(user1)))
    topo.standalone.config.set('nsslapd-errorlog-level', '0')
    for sub in memberof:
        topo.standalone.log.info("memberof found   : %s" % sub.dn)
        assert sub.dn in [ensure_str(group1), ensure_str(group2), ensure_str(group3)]
    assert topo.standalone.ds_error_log.match('.*sm_compare_memberof_config: fails because requested include scope is not empty.*')
    assert not topo.standalone.ds_error_log.match('.*slapi_memberof - sm_compare_memberof_config: succeeds. requested options match config.*')

    # Call slapi_member that does reuse the memberof
    # because of 'memberOfEntryScope' being set
    memberof = MemberOfPlugin(topo.standalone)
    memberof.replace('memberOfEntryScope', DEFAULT_SUFFIX)
    topo.standalone.restart()
    topo.standalone.config.set('nsslapd-errorlog-level', '65536') # plugin logging
    memberof = topo.standalone.search_s(DEFAULT_SUFFIX, SCOPE_SUBTREE, "(member:%s:=%s)" % (INCHAIN_OID, ensure_str(user1)))
    topo.standalone.config.set('nsslapd-errorlog-level', '0')
    for sub in memberof:
        topo.standalone.log.info("memberof found   : %s" % sub.dn)
        assert sub.dn in [ensure_str(group1), ensure_str(group2), ensure_str(group3)]
    assert topo.standalone.ds_error_log.match('.*slapi_memberof - sm_compare_memberof_config: succeeds. requested options match config.*')

    def fin():
        topo.standalone.delete_s(ensure_str(user1))
        topo.standalone.delete_s(ensure_str(group1))
        topo.standalone.delete_s(ensure_str(group2))
        topo.standalone.delete_s(ensure_str(group3))

    request.addfinalizer(fin)

def test_invalid_assertion(topo):
    """Check that with invalid assertion
    there is no returned entries

    :id: 0a204b81-e7c0-41a0-97cc-7d9425a603c2
    :setup: Standalone instance
    :steps:
        1. Search with invalid assertion '..:=foo'
        2. Search with not existing entry '..:=<dummy_entry>'
    :expectedresults:
        1. Success
        2. Success
    """
    topo.standalone.log.info("Search with an invalid assertion")
    memberof = topo.standalone.search_s(DEFAULT_SUFFIX, SCOPE_SUBTREE, "(member:%s:=foo)" % (INCHAIN_OID))
    assert len(memberof) == 0

    topo.standalone.log.info("Search with an none exisiting entry")

    user = "uid=not_existing_entry,ou=People,%s" % (DEFAULT_SUFFIX)
    memberof = topo.standalone.search_s(DEFAULT_SUFFIX, SCOPE_SUBTREE, "(member:%s:=%s)" % (INCHAIN_OID, user))
    assert len(memberof) == 0

def test_check_dsconf_matchingrule(topo):
    """Test that the matching rule 'inchain' is listed by dsconf

    :id: b8dd4049-ccec-4316-bc9c-5aa5c5afcfbd
    :setup: Standalone Instance
    :steps:
        1. fetch matching rules from the schema
        2. Checks that matching rules contains inchaineMatch matching rule
    :expectedresults:
        1. Success
        2. Success
    """
    schema = Schema(topo.standalone)
    mrs = [ f"{mr.oid} {mr.names[0]}" for mr in schema.get_matchingrules() if len(mr.names) > 0 ]
    for mr in mrs:
        log.info("retrieved matching rules are: %s", mr)
    assert '1.2.840.113556.1.4.1941 inchainMatch' in mrs

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
