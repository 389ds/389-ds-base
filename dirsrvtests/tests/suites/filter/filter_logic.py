# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
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

from lib389.idm.user import UserAccounts

"""
This test case asserts that various logical filters apply correctly and as expected.
This is to assert that we have correct and working search operations, especially related
to indexed content from filterindex.c and idl_sets.

important to note, some tests check greater than 10 elements to assert that k-way intersect
works, where as most of these actually hit the filtertest threshold so they early return.
"""

USER0_DN = 'uid=user0,ou=People,%s' % DEFAULT_SUFFIX
USER1_DN = 'uid=user1,ou=People,%s' % DEFAULT_SUFFIX
USER2_DN = 'uid=user2,ou=People,%s' % DEFAULT_SUFFIX
USER3_DN = 'uid=user3,ou=People,%s' % DEFAULT_SUFFIX
USER4_DN = 'uid=user4,ou=People,%s' % DEFAULT_SUFFIX
USER5_DN = 'uid=user5,ou=People,%s' % DEFAULT_SUFFIX
USER6_DN = 'uid=user6,ou=People,%s' % DEFAULT_SUFFIX
USER7_DN = 'uid=user7,ou=People,%s' % DEFAULT_SUFFIX
USER8_DN = 'uid=user8,ou=People,%s' % DEFAULT_SUFFIX
USER9_DN = 'uid=user9,ou=People,%s' % DEFAULT_SUFFIX
USER10_DN = 'uid=user10,ou=People,%s' % DEFAULT_SUFFIX
USER11_DN = 'uid=user11,ou=People,%s' % DEFAULT_SUFFIX
USER12_DN = 'uid=user12,ou=People,%s' % DEFAULT_SUFFIX
USER13_DN = 'uid=user13,ou=People,%s' % DEFAULT_SUFFIX
USER14_DN = 'uid=user14,ou=People,%s' % DEFAULT_SUFFIX
USER15_DN = 'uid=user15,ou=People,%s' % DEFAULT_SUFFIX
USER16_DN = 'uid=user16,ou=People,%s' % DEFAULT_SUFFIX
USER17_DN = 'uid=user17,ou=People,%s' % DEFAULT_SUFFIX
USER18_DN = 'uid=user18,ou=People,%s' % DEFAULT_SUFFIX
USER19_DN = 'uid=user19,ou=People,%s' % DEFAULT_SUFFIX

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
    # return it
    # print("ATTACH NOW")
    # import time
    # time.sleep(30)
    return topology_st.standalone

def _check_filter(topology_st_f, filt, expect_len, expect_dns):
    # print("checking %s" % filt)
    results = topology_st_f.search_s("ou=People,%s" % DEFAULT_SUFFIX, ldap.SCOPE_ONELEVEL, filt, ['uid',])
    assert len(results) == expect_len
    result_dns = [result.dn for result in results]
    assert set(expect_dns) == set(result_dns)

def test_filter_logic_eq(topology_st_f):
    _check_filter(topology_st_f, '(uid=user0)', 1, [USER0_DN])

def test_filter_logic_sub(topology_st_f):
    _check_filter(topology_st_f, '(uid=user*)', 20, [
            USER0_DN, USER1_DN, USER2_DN, USER3_DN, USER4_DN,
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])

def test_filter_logic_not_eq(topology_st_f):
    _check_filter(topology_st_f, '(!(uid=user0))', 19, [
            USER1_DN, USER2_DN, USER3_DN, USER4_DN, USER5_DN,
            USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])

# More not cases?

def test_filter_logic_range(topology_st_f):
    _check_filter(topology_st_f, '(uid>=user5)', 15, [
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])
    _check_filter(topology_st_f, '(uid<=user4)', 5, [
            USER0_DN, USER1_DN, USER2_DN, USER3_DN, USER4_DN
        ])
    _check_filter(topology_st_f, '(uid>=ZZZZ)', 0, [])
    _check_filter(topology_st_f, '(uid<=aaaa)', 0, [])

def test_filter_logic_and_eq(topology_st_f):
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user1))', 0, [])
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user0)(sn=0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user1)(sn=0))', 0, [])
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user0)(sn=1))', 0, [])

def test_filter_logic_and_range(topology_st_f):
    _check_filter(topology_st_f, '(&(uid>=user5)(cn<=user7))', 3, [
        USER5_DN, USER6_DN, USER7_DN
        ])

def test_filter_logic_and_allid_shortcut(topology_st_f):
    _check_filter(topology_st_f, '(&(objectClass=*)(uid=user0)(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(uid=user0)(cn=user0)(objectClass=*))', 1, [USER0_DN])

def test_filter_logic_or_eq(topology_st_f):
    _check_filter(topology_st_f, '(|(uid=user0)(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(uid=user0)(uid=user1))', 2, [USER0_DN, USER1_DN])
    _check_filter(topology_st_f, '(|(uid=user0)(cn=user0)(sn=0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(uid=user0)(uid=user1)(sn=0))', 2, [USER0_DN, USER1_DN])
    _check_filter(topology_st_f, '(|(uid=user0)(uid=user1)(uid=user2))', 3, [USER0_DN, USER1_DN, USER2_DN])

def test_filter_logic_and_not_eq(topology_st_f):
    _check_filter(topology_st_f, '(&(uid=user0)(!(cn=user0)))', 0, [])
    _check_filter(topology_st_f, '(&(uid=*)(!(uid=user0)))', 19, [
            USER1_DN, USER2_DN, USER3_DN, USER4_DN, USER5_DN,
            USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])

def test_filter_logic_or_not_eq(topology_st_f):
    _check_filter(topology_st_f, '(|(!(uid=user0))(!(uid=user1)))', 20, [
            USER0_DN, USER1_DN, USER2_DN, USER3_DN, USER4_DN,
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])

def test_filter_logic_and_range(topology_st_f):
    # These all hit shortcut cases.
    _check_filter(topology_st_f, '(&(uid>=user5)(uid=user6))', 1, [USER6_DN])
    _check_filter(topology_st_f, '(&(uid>=user5)(uid=user0))', 0, [])
    _check_filter(topology_st_f, '(&(uid>=user5)(uid=user6)(sn=6))', 1, [USER6_DN])
    _check_filter(topology_st_f, '(&(uid>=user5)(uid=user0)(sn=0))', 0, [])
    _check_filter(topology_st_f, '(&(uid>=user5)(uid=user0)(sn=1))', 0, [])
    # These all take 2-way or k-way cases.
    _check_filter(topology_st_f, '(&(uid>=user5)(uid>=user6))', 15, [
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])
    _check_filter(topology_st_f, '(&(uid>=user5)(uid>=user6)(uid>=user7))', 15, [
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])


def test_filter_logic_or_range(topology_st_f):
    _check_filter(topology_st_f, '(|(uid>=user5)(uid=user6))', 15, [
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])
    _check_filter(topology_st_f, '(|(uid>=user5)(uid=user0))', 16, [
            USER0_DN,
            USER5_DN, USER6_DN, USER7_DN, USER8_DN, USER9_DN,
            USER10_DN, USER11_DN, USER12_DN, USER13_DN, USER14_DN,
            USER15_DN, USER16_DN, USER17_DN, USER18_DN, USER19_DN
        ])

def test_filter_logic_and_and_eq(topology_st_f):
    _check_filter(topology_st_f, '(&(&(uid=user0)(sn=0))(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(&(uid=user1)(sn=0))(cn=user0))', 0, [])
    _check_filter(topology_st_f, '(&(&(uid=user0)(sn=1))(cn=user0))', 0, [])
    _check_filter(topology_st_f, '(&(&(uid=user0)(sn=0))(cn=user1))', 0, [])

def test_filter_logic_or_or_eq(topology_st_f):
    _check_filter(topology_st_f, '(|(|(uid=user0)(sn=0))(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(|(uid=user1)(sn=0))(cn=user0))', 2, [USER0_DN, USER1_DN])
    _check_filter(topology_st_f, '(|(|(uid=user0)(sn=1))(cn=user0))', 2, [USER0_DN, USER1_DN])
    _check_filter(topology_st_f, '(|(|(uid=user0)(sn=0))(cn=user1))', 2, [USER0_DN, USER1_DN])
    _check_filter(topology_st_f, '(|(|(uid=user0)(sn=1))(cn=user2))', 3, [USER0_DN, USER1_DN, USER2_DN])

def test_filter_logic_and_or_eq(topology_st_f):
    _check_filter(topology_st_f, '(&(|(uid=user0)(sn=0))(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(|(uid=user1)(sn=0))(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(|(uid=user0)(sn=1))(cn=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(&(|(uid=user0)(sn=0))(cn=user1))', 0, [])
    _check_filter(topology_st_f, '(&(|(uid=user0)(sn=1))(cn=*))', 2, [USER0_DN, USER1_DN])

def test_filter_logic_or_and_eq(topology_st_f):
    _check_filter(topology_st_f, '(|(&(uid=user0)(sn=0))(uid=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(&(uid=user1)(sn=2))(uid=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(&(uid=user0)(sn=1))(uid=user0))', 1, [USER0_DN])
    _check_filter(topology_st_f, '(|(&(uid=user1)(sn=1))(uid=user0))', 2, [USER0_DN, USER1_DN])


