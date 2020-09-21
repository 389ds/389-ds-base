# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import ldap

from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX

from lib389.idm.user import UserAccount, UserAccounts

pytestmark = pytest.mark.tier1

"""
This test case asserts that various logical filters apply correctly and as expected.
This is to assert that we have correct and working search operations, especially related
to indexed content from filterindex.c and idl_sets.

important to note, some tests check greater than 10 elements to assert that k-way intersect
works, where as most of these actually hit the filtertest threshold so they early return.
"""

USER0_DN = 'uid=user0,ou=people,%s' % DEFAULT_SUFFIX
USER1_DN = 'uid=user1,ou=people,%s' % DEFAULT_SUFFIX
USER2_DN = 'uid=user2,ou=people,%s' % DEFAULT_SUFFIX
USER3_DN = 'uid=user3,ou=people,%s' % DEFAULT_SUFFIX
USER4_DN = 'uid=user4,ou=people,%s' % DEFAULT_SUFFIX
USER5_DN = 'uid=user5,ou=people,%s' % DEFAULT_SUFFIX
USER6_DN = 'uid=user6,ou=people,%s' % DEFAULT_SUFFIX
USER7_DN = 'uid=user7,ou=people,%s' % DEFAULT_SUFFIX
USER8_DN = 'uid=user8,ou=people,%s' % DEFAULT_SUFFIX
USER9_DN = 'uid=user9,ou=people,%s' % DEFAULT_SUFFIX
USER10_DN = 'uid=user10,ou=people,%s' % DEFAULT_SUFFIX
USER11_DN = 'uid=user11,ou=people,%s' % DEFAULT_SUFFIX
USER12_DN = 'uid=user12,ou=people,%s' % DEFAULT_SUFFIX
USER13_DN = 'uid=user13,ou=people,%s' % DEFAULT_SUFFIX
USER14_DN = 'uid=user14,ou=people,%s' % DEFAULT_SUFFIX
USER15_DN = 'uid=user15,ou=people,%s' % DEFAULT_SUFFIX
USER16_DN = 'uid=user16,ou=people,%s' % DEFAULT_SUFFIX
USER17_DN = 'uid=user17,ou=people,%s' % DEFAULT_SUFFIX
USER18_DN = 'uid=user18,ou=people,%s' % DEFAULT_SUFFIX
USER19_DN = 'uid=user19,ou=people,%s' % DEFAULT_SUFFIX

@pytest.fixture(scope="module")
def topology_st_f(topology_st):
    # Add our users to the topology_st
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    for i in range(0, 20):
        users.create(properties={
            'uid': 'user%s' % i,
            'cn': 'user%s' % i,
            'sn': '%s' % i,
            'uidNumber': '%s' % i,
            'gidNumber': '%s' % i,
            'homeDirectory': '/home/user%s' % i
        })


    demo_user = UserAccount(topology_st.standalone, "uid=demo_user,ou=people,dc=example,dc=com")
    demo_user.delete()
    # return it
    # print("ATTACH NOW")
    # import time
    # time.sleep(30)
    return topology_st.standalone

def _check_filter(topology_st_f, filt, expect_len, expect_dns):
    # print("checking %s" % filt)
    results = topology_st_f.search_s("ou=people,%s" % DEFAULT_SUFFIX, ldap.SCOPE_ONELEVEL, filt, ['uid',])
    assert len(results) == expect_len
    result_dns = [result.dn for result in results]
    assert set(expect_dns) == set(result_dns)


def test_eq(topology_st_f):
    """Test filter logic with "equal to" operator

    :id: 1b0b7e59-a5ac-4825-8d36-525f4f0149a9
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(uid=user0)``
    :expectedresults:
         1. There should be 1 user listed user0
    """
    _check_filter(topology_st_f, '(uid=user0)', 1, [USER0_DN])


def test_sub(topology_st_f):
    """Test filter logic with "sub"

    :id: 8cfa946d-7ddf-4f8e-9f9f-39da8f35304e
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(uid=user*)``
    :expectedresults:
         1. There should be 20 users listed from user0 to user19
    """
    _check_filter(topology_st_f, '(uid=user*)', 20, [
            USER0_DN, USER1_DN, USER2_DN, USER3_DN, USER4_DN,
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])


def test_not_eq(topology_st_f):
    """Test filter logic with "not equal to" operator

    :id: 1422ec65-421d-473b-89ba-649f8decc1ab
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(!(uid=user0))``
    :expectedresults:
         1. There should be 19 users listed from user1 to user19
    """
    _check_filter(topology_st_f, '(!(uid=user0))', 19, [
            USER1_DN, USER2_DN, USER3_DN, USER4_DN, USER5_DN,
            USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])

# More not cases?

def test_ranges(topology_st_f):
    """Test filter logic with range

    :id: cc7c25f0-6a6e-465b-8d32-7fcc1aec84ee
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(uid>=user5)``
         2. Search for test users with filter ``(uid<=user4)``
         3. Search for test users with filter ``(uid>=ZZZZ)``
         4. Search for test users with filter ``(uid<=aaaa)``
    :expectedresults:
         1. There should be 5 users listed from user5 to user9
         2. There should be 15 users listed from user0 to user4
            and from user10 to user19
         3. There should not be any user listed
         4. There should not be any user listed
    """

    ### REMEMBER: user10 is less than user5 because it's strcmp!!!
    _check_filter(topology_st_f, '(uid>=user5)', 5, [
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
        ])
    _check_filter(topology_st_f, '(uid<=user4)', 15, [
            USER0_DN, USER1_DN, USER2_DN, USER3_DN, USER4_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])
    _check_filter(topology_st_f, '(uid>=ZZZZ)', 0, [])
    _check_filter(topology_st_f, '(uid<=aaaa)', 0, [])


def test_and_eq(topology_st_f):
    """Test filter logic with "AND" operator

    :id: 4721fd7c-8d0b-43e6-b2e8-a5bac7674f99
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(&(uid=user0)(cn=user0))``
         2. Search for test users with filter ``(&(uid=user0)(cn=user1))``
         3. Search for test users with filter ``(&(uid=user0)(cn=user0)(sn=0))``
         4. Search for test users with filter ``(&(uid=user0)(cn=user1)(sn=0))``
         5. Search for test users with filter ``(&(uid=user0)(cn=user0)(sn=1))``
    :expectedresults:
         1. There should be 1 user listed i.e. user0
         2. There should not be any user listed
         3. There should be 1 user listed i.e. user0
         4. There should not be any user listed
         5. There should not be any user listed
    """
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user1))', 0, [])
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user0)(sn=0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user1)(sn=0))', 0, [])
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user0)(sn=1))', 0, [])


def test_range(topology_st_f):
    """Test filter logic with range

    :id: 617e6290-866e-4b5d-a300-d8f1715ad052
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(&(uid>=user5)(cn<=user7))``
    :expectedresults:
         1. There should be 3 users listed i.e. user5 to user7
    """
    _check_filter(topology_st_f, '(&(uid>=user5)(cn<=user7))', 3, [
        USER5_DN, USER6_DN, USER7_DN
        ])


def test_and_allid_shortcut(topology_st_f):
    """Test filter logic with "AND" operator
       and shortcuts

    :id: f4784752-d269-4ceb-aada-fafe0a5fc14c
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(&(objectClass=*)(uid=user0)(cn=user0))``
         2. Search for test users with filter ``(&(uid=user0)(cn=user0)(objectClass=*))``
    :expectedresults:
         1. There should be 1 user listed i.e. user0
         2. There should be 1 user listed i.e. user0
    """
    _check_filter(topology_st_f, '(&(objectClass=*)(uid=user0)(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user0)(objectClass=*))', 1, [USER0_DN])


def test_or_eq(topology_st_f):
    """Test filter logic with "or" and "equal to" operators

    :id: a23a4fc9-0f5c-49ce-b1f7-6ac10bcd7763
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``|(uid=user0)(cn=user0)``
         2. Search for test users with filter ``(|(uid=user0)(uid=user1))``
         3. Search for test users with filter ``(|(uid=user0)(cn=user0)(sn=0))``
         4. Search for test users with filter ``(|(uid=user0)(uid=user1)(sn=0))``
         5. Search for test users with filter ``(|(uid=user0)(uid=user1)(uid=user2))``
    :expectedresults:
         1. There should be 1 user listed i.e. user0
         2. There should be 2 users listed i.e. user0 and user1
         3. There should be 1 user listed i.e. user0
         4. There should be 2 users listed i.e. user0 and user1
         5. There should be 3 users listed i.e. user0 to user2
    """
    _check_filter(topology_st_f, '(|(uid=user0)(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(uid=user0)(uid=user1))', 2, [USER0_DN, USER1_DN])
    _check_filter(topology_st_f, '(|(uid=user0)(cn=user0)(sn=0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(uid=user0)(uid=user1)(sn=0))', 2, [USER0_DN, USER1_DN])
    _check_filter(topology_st_f, '(|(uid=user0)(uid=user1)(uid=user2))', 3, [USER0_DN, USER1_DN, USER2_DN])


def test_and_not_eq(topology_st_f):
    """Test filter logic with "not equal" to operator

    :id: bd00cb2b-35bb-49c0-8387-f60a6ada7c87
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(&(uid=user0)(!(cn=user0)))``
         2. Search for test users with filter ``(&(uid=*)(!(uid=user0)))``
    :expectedresults:
         1. There should be no users listed
         2. There should be 19 users listed i.e. user1 to user19
    """
    _check_filter(topology_st_f, '(&(uid=user0)(!(cn=user0)))', 0, [])
    _check_filter(topology_st_f, '(&(uid=*)(!(uid=user0)))', 19, [
            USER1_DN, USER2_DN, USER3_DN, USER4_DN, USER5_DN,
            USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])


def test_or_not_eq(topology_st_f):
    """Test filter logic with "OR and NOT" operators

    :id: 8f62f339-72c9-49e4-8126-b2a14e61b9c0
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(|(!(uid=user0))(!(uid=user1)))``
    :expectedresults:
         1. There should be 20 users listed i.e. user0 to user19
    """
    _check_filter(topology_st_f, '(|(!(uid=user0))(!(uid=user1)))', 20, [
            USER0_DN, USER1_DN, USER2_DN, USER3_DN, USER4_DN,
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])


def test_and_range(topology_st_f):
    """Test filter logic with range

    :id: 8e5a0e2a-4ee1-4cd7-b5ec-90ad4d3ace64
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(&(uid>=user5)(uid=user6))``
         2. Search for test users with filter ``(&(uid>=user5)(uid=user0))``
         3. Search for test users with filter ``(&(uid>=user5)(uid=user6)(sn=6))``
         4. Search for test users with filter ``(&(uid>=user5)(uid=user0)(sn=0))``
         5. Search for test users with filter ``(&(uid>=user5)(uid=user0)(sn=1))``
         6. Search for test users with filter ``(&(uid>=user5)(uid>=user6))``
         7. Search for test users with filter ``(&(uid>=user5)(uid>=user6)(uid>=user7))``
    :expectedresults:
         1. There should be 1 user listed i.e. user6
         2. There should be no users listed
         3. There should be 1 user listed i.e. user6
         4. There should be no users listed
         5. There should be no users listed
         6. There should be 4 users listed i.e. user6 to user9
         7. There should be 3 users listed i.e. user7 to user9
    """
    # These all hit shortcut cases.
    _check_filter(topology_st_f, '(&(uid>=user5)(uid=user6))', 1, [USER6_DN])
    _check_filter(topology_st_f, '(&(uid>=user5)(uid=user0))', 0, [])
    _check_filter(topology_st_f, '(&(uid>=user5)(uid=user6)(sn=6))', 1, [USER6_DN])
    _check_filter(topology_st_f, '(&(uid>=user5)(uid=user0)(sn=0))', 0, [])
    _check_filter(topology_st_f, '(&(uid>=user5)(uid=user0)(sn=1))', 0, [])
    # These all take 2-way or k-way cases.
    _check_filter(topology_st_f, '(&(uid>=user5)(uid>=user6))', 4, [
            USER6_DN, USER7_DN, USER8_DN, USER9_DN,
        ])
    _check_filter(topology_st_f, '(&(uid>=user5)(uid>=user6)(uid>=user7))', 3, [
            USER7_DN, USER8_DN, USER9_DN,
        ])



def test_or_range(topology_st_f):
    """Test filter logic with range

    :id: bc413e74-667a-48b0-8fbd-e9b7d18a01e4
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(|(uid>=user5)(uid=user6))``
         2. Search for test users with filter ``(|(uid>=user5)(uid=user0))``
    :expectedresults:
         1. There should be 5 users listed i.e. user5 to user9
         2. There should be 6 users listed i.e. user5 to user9 and user0
    """
    _check_filter(topology_st_f, '(|(uid>=user5)(uid=user6))', 5, [
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
        ])
    _check_filter(topology_st_f, '(|(uid>=user5)(uid=user0))', 6, [
            USER0_DN,
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
        ])


def test_and_and_eq(topology_st_f):
    """Test filter logic with "AND" and "equal to" operators

    :id: 5c66eb38-d01f-459e-81e4-d335f97211c7
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(&(&(uid=user0)(sn=0))(cn=user0))``
         2. Search for test users with filter ``(&(&(uid=user1)(sn=0))(cn=user0))``
         3. Search for test users with filter ``(&(&(uid=user0)(sn=1))(cn=user0))``
         4. Search for test users with filter ``(&(&(uid=user0)(sn=0))(cn=user1))``
    :expectedresults:
         1. There should be 1 user listed i.e. user0
         2. There should be no users listed
         3. There should be no users listed
         4. There should be no users listed
    """
    _check_filter(topology_st_f, '(&(&(uid=user0)(sn=0))(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(&(uid=user1)(sn=0))(cn=user0))', 0, [])
    _check_filter(topology_st_f, '(&(&(uid=user0)(sn=1))(cn=user0))', 0, [])
    _check_filter(topology_st_f, '(&(&(uid=user0)(sn=0))(cn=user1))', 0, [])


def test_or_or_eq(topology_st_f):
    """Test filter logic with "AND" and "equal to" operators

    :id: 0cab4bbd-637c-419d-8069-ad5463ecaa75
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(|(|(uid=user0)(sn=0))(cn=user0))``
         2. Search for test users with filter ``(|(|(uid=user1)(sn=0))(cn=user0))``
         3. Search for test users with filter ``(|(|(uid=user0)(sn=1))(cn=user0))``
         4. Search for test users with filter ``(|(|(uid=user0)(sn=0))(cn=user1))``
         5. Search for test users with filter ``(|(|(uid=user0)(sn=1))(cn=user2))``
    :expectedresults:
         1. There should be 1 user listed i.e. user0
         2. There should be 2 users listed i.e. user0, user1
         3. There should be 2 users listed i.e. user0, user1
         4. There should be 2 users listed i.e. user0, user1
         5. There should be 3 users listed i.e. user0, user1 and user2
    """
    _check_filter(topology_st_f, '(|(|(uid=user0)(sn=0))(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(|(uid=user1)(sn=0))(cn=user0))', 2, [USER0_DN, USER1_DN])
    _check_filter(topology_st_f, '(|(|(uid=user0)(sn=1))(cn=user0))', 2, [USER0_DN, USER1_DN])
    _check_filter(topology_st_f, '(|(|(uid=user0)(sn=0))(cn=user1))', 2, [USER0_DN, USER1_DN])
    _check_filter(topology_st_f, '(|(|(uid=user0)(sn=1))(cn=user2))', 3, [USER0_DN, USER1_DN, USER2_DN])


def test_and_or_eq(topology_st_f):
    """Test filter logic with "AND" and "equal to" operators

    :id: 2ce7cc2e-6058-422d-ac3e-e678decf1cc4
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(&(|(uid=user0)(sn=0))(cn=user0))``
         2. Search for test users with filter ``(&(|(uid=user1)(sn=0))(cn=user0))``
         3. Search for test users with filter ``(&(|(uid=user0)(sn=1))(cn=user0))``
         4. Search for test users with filter ``(&(|(uid=user0)(sn=0))(cn=user1))``
         5. Search for test users with filter ``(&(|(uid=user0)(sn=1))(cn=*))``
    :expectedresults:
         1. There should be 1 user listed i.e. user0
         2. There should be 1 user listed i.e. user0
         3. There should be 1 user listed i.e. user0
         4. There should be no users listed
         5. There should be 2 users listed i.e. user0 and user1
    """
    _check_filter(topology_st_f, '(&(|(uid=user0)(sn=0))(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(|(uid=user1)(sn=0))(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(|(uid=user0)(sn=1))(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(|(uid=user0)(sn=0))(cn=user1))', 0, [])
    _check_filter(topology_st_f, '(&(|(uid=user0)(sn=1))(cn=*))', 2, [USER0_DN, USER1_DN])


def test_or_and_eq(topology_st_f):
    """Test filter logic with "AND" and "equal to" operators

    :id: ee9fb400-451a-479e-852c-f59b4c937a8d
    :setup: Standalone instance with 20 test users added
            from uid=user0 to uid=user20
    :steps:
         1. Search for test users with filter ``(|(&(uid=user0)(sn=0))(uid=user0))``
         2. Search for test users with filter ``(|(&(uid=user1)(sn=2))(uid=user0))``
         3. Search for test users with filter ``(|(&(uid=user0)(sn=1))(uid=user0))``
         4. Search for test users with filter ``(|(&(uid=user1)(sn=1))(uid=user0))``
    :expectedresults:
         1. There should be 1 user listed i.e. user0
         2. There should be 1 user listed i.e. user0
         3. There should be 1 user listed i.e. user0
         4. There should be 2 user listed i.e. user0 and user1
    """
    _check_filter(topology_st_f, '(|(&(uid=user0)(sn=0))(uid=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(&(uid=user1)(sn=2))(uid=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(&(uid=user0)(sn=1))(uid=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(&(uid=user1)(sn=1))(uid=user0))', 2, [USER0_DN, USER1_DN])


