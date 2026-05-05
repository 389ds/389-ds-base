# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import logging
import os
import pytest

from lib389._constants import DEFAULT_BENAME, DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts
from lib389.index import Index
from lib389.properties import TASK_WAIT
from lib389.tasks import Tasks
from test389.topologies import topology_st

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

UID_INDEX_DN = f'cn=uid,cn=index,cn={DEFAULT_BENAME},cn=ldbm database,cn=plugins,cn=config'
UID_PREFIX = 'zzsubtest'


@pytest.fixture
def uid_index(topology_st):
    """Get the uid index object and ensure clean state before/after each test."""
    inst = topology_st.standalone
    if not inst.status():
        inst.start()
    index = Index(inst, UID_INDEX_DN)

    def _cleanup():
        """Remove substring index config, ignore errors if attrs not present."""
        if not inst.status():
            inst.start()
        for attr in ['nsSubStrBegin', 'nsSubStrMiddle', 'nsSubStrEnd']:
            try:
                index.remove_all(attr)
            except (ldap.NO_SUCH_ATTRIBUTE, ldap.SERVER_DOWN):
                pass

        # Remove extensibleObject if present
        try:
            index.set('objectClass', 'extensibleObject', action=ldap.MOD_DELETE)
        except (ldap.NO_SUCH_ATTRIBUTE, ldap.OBJECT_CLASS_VIOLATION,
                ldap.SERVER_DOWN):
            pass

        # Remove nssubstr matching rules from nsMatchingRule
        for rule in ['nssubstrbegin', 'nssubstrmiddle', 'nssubstrend']:
            try:
                mrs = index.get_attr_vals_utf8('nsMatchingRule')
                for mr in mrs:
                    if mr.lower().startswith(rule):
                        index.set('nsMatchingRule', mr, action=ldap.MOD_DELETE)
            except (ldap.NO_SUCH_ATTRIBUTE, ldap.OBJECT_CLASS_VIOLATION,
                    ldap.SERVER_DOWN):
                pass

        # Remove 'sub' from nsIndexType
        try:
            index.set('nsIndexType', 'sub', action=ldap.MOD_DELETE)
        except (ldap.NO_SUCH_ATTRIBUTE, ldap.SERVER_DOWN):
            pass

    yield index
    _cleanup()


@pytest.fixture
def create_user(topology_st):
    """Create test users."""
    inst = topology_st.standalone
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    created = []

    def _create_user(uid, cn, sn):
        user = users.create(properties={
            'uid': uid,
            'cn': cn,
            'sn': sn,
            'uidNumber': str(1000 + len(created)),
            'gidNumber': '1000',
            'homeDirectory': f'/home/{uid}',
        })
        created.append(user)
        return user

    yield _create_user


def _reindex_uid(topology_st):
    """Reindex the uid attribute and wait for completion."""
    tasks = Tasks(topology_st.standalone)
    assert tasks.reindex(
        suffix=DEFAULT_SUFFIX,
        attrname='uid',
        args={TASK_WAIT: True}
    ) == 0


def _search_and_assert(topology_st, filt, expected_count, msg=""):
    """Search with filter and assert expected number of results."""
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    entries = users.filter(filt)
    assert len(entries) == expected_count, \
        f'{msg}: filter "{filt}" returned {len(entries)} entries, expected {expected_count}'
    return entries


def test_substr_direct_attrs(topology_st, uid_index, create_user):
    """Test substring index with nsSubStrBegin/nsSubStrEnd direct attributes.

    :id: c670a123-71f6-4577-b3f0-7c938153903f
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Configure uid index with nsSubStrBegin=2, nsSubStrEnd=2
           using extensibleObject direct attributes
        2. Reindex uid attribute
        3. Add a test user
        4. Search with short initial substring
        5. Search with 2-char initial
        6. Search with 2-char final
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Returns the user
        5. Returns the user
        6. Returns the user
    """
    uid_index.add_many(
        ('objectClass', 'extensibleObject'),
        ('nsIndexType', 'sub'),
        ('nsSubStrBegin', '2'),
        ('nsSubStrEnd', '2'),
    )

    uid = f'{UID_PREFIX}a0'
    create_user(uid, 'a user0', 'user0')

    _reindex_uid(topology_st)

    _search_and_assert(topology_st, f'(uid={uid[:len(UID_PREFIX)+1]}*)', 1,
                       'begin=2 initial search')
    _search_and_assert(topology_st, f'(uid={uid[:len(UID_PREFIX)+2]}*)', 1,
                       'begin=2 two-char initial')
    _search_and_assert(topology_st, f'(uid=*{uid[-2:]})', 1,
                       'end=2 final search')


def test_substr_matching_rule(topology_st, uid_index, create_user):
    """Test substring index with nsMatchingRule format for substr lengths.

    :id: 58eebbc5-2571-43ef-9b97-151b11c02eb5
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Configure uid index with nsMatchingRule: nssubstrbegin=2
           and nsMatchingRule: nssubstrend=2
        2. Reindex uid attribute
        3. Add a test user
        4. Search with short initial substring
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Returns the user
    """
    uid_index.add_many(
        ('nsIndexType', 'sub'),
        ('nsMatchingRule', 'nssubstrbegin=2'),
        ('nsMatchingRule', 'nssubstrend=2'),
    )

    uid = f'{UID_PREFIX}b1'
    create_user(uid, 'b user1', 'user1')

    _reindex_uid(topology_st)

    _search_and_assert(topology_st, f'(uid={uid[:len(UID_PREFIX)+1]}*)', 1,
                       'nsMatchingRule begin=2')
    _search_and_assert(topology_st, f'(uid={uid[:len(UID_PREFIX)+2]}*)', 1,
                       'nsMatchingRule begin=2 two-char')


def test_substr_direct_attrs_override_matching_rule(topology_st, uid_index, create_user):
    """Test that nsSubStr direct attributes take precedence over nsMatchingRule.

    :id: 649051f7-97d6-4f85-bfeb-c94e4be49ede
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Configure uid index with BOTH formats:
           - nsMatchingRule: nssubstrbegin=3 (would require 3-char initial)
           - nsMatchingRule: nssubstrend=3
           - nsSubStrBegin: 2 (direct attr, should override to 2)
           - nsSubStrEnd: 2
        2. Reindex uid attribute
        3. Add a test user
        4. Search with 1-char initial - only works if begin=2 (direct) is used
        5. Search with 1-char final - only works if end=2 (direct) is used
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Returns the user - proves direct attr (2) overrides matching rule (3)
        5. Returns the user
    """
    uid_index.add_many(
        ('nsIndexType', 'sub'),
        ('nsMatchingRule', 'nssubstrbegin=3'),
        ('nsMatchingRule', 'nssubstrend=3'),
        ('objectClass', 'extensibleObject'),
        ('nsSubStrBegin', '2'),
        ('nsSubStrEnd', '2'),
    )

    uid = f'{UID_PREFIX}c2'
    create_user(uid, 'c user2', 'user2')

    _reindex_uid(topology_st)

    _search_and_assert(topology_st, f'(uid={uid[:len(UID_PREFIX)+1]}*)', 1,
                       'direct attr begin=2 overrides matching rule begin=3')
    _search_and_assert(topology_st, f'(uid=*{uid[-1:]})', 1,
                       'direct attr end=2 overrides matching rule end=3')


def test_substr_mixed_values(topology_st, uid_index, create_user):
    """Test substring index with different values for begin, middle and end.

    :id: 6c0bb8a2-234b-4daa-bf9c-07e8d03b08b0
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Configure uid index with begin=2, middle=3, end=2
        2. Reindex uid attribute
        3. Add two test users
        4. Search with short initial
        5. Search with 3-char any
        6. Search with 2-char final
        7. Search with longer any (unique to one user)
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Returns 1 user
        5. Returns 2 users
        6. Returns 2 users
        7. Returns 1 user
    """
    uid_index.add_many(
        ('objectClass', 'extensibleObject'),
        ('nsIndexType', 'sub'),
        ('nsSubStrBegin', '2'),
        ('nsSubStrMiddle', '3'),
        ('nsSubStrEnd', '2'),
    )

    uid1 = f'{UID_PREFIX}d3mixed'
    uid2 = f'{UID_PREFIX}e4mixed'
    create_user(uid1, 'd user3mixed', 'user3')
    create_user(uid2, 'e user4mixed', 'user4')

    _reindex_uid(topology_st)

    _search_and_assert(topology_st, f'(uid={uid1})', 1, 'exact match sanity check')
    _search_and_assert(topology_st, f'(uid={uid1[:len(UID_PREFIX)+1]}*)', 1,
                       'mixed: begin=2 initial search')
    _search_and_assert(topology_st, '(uid=*mix*)', 2, 'mixed: middle=3 any search')
    _search_and_assert(topology_st, '(uid=*ed)', 2, 'mixed: end=2 final search')
    _search_and_assert(topology_st, f'(uid=*d3m*)', 1, 'mixed: longer any search unique')


def test_substr_low_values(topology_st, uid_index, create_user):
    """Test substring index with all values set to 2.

    :id: db9dd1cc-f76d-4ea6-8c56-4238d3f323a7
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Configure uid index with all substr lengths set to 2
        2. Reindex uid attribute
        3. Add test users
        4. Search with short initial
        5. Search with 2-char any
        6. Search with multi-char final
        7. Search with longer any
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Returns 1 user
        5. Returns 2 users
        6. Returns 1 user
        7. Returns 1 user
    """
    uid_index.add_many(
        ('objectClass', 'extensibleObject'),
        ('nsIndexType', 'sub'),
        ('nsSubStrBegin', '2'),
        ('nsSubStrMiddle', '2'),
        ('nsSubStrEnd', '2'),
    )

    uid1 = f'{UID_PREFIX}f5low'
    uid2 = f'{UID_PREFIX}g6low'
    create_user(uid1, 'f user5low', 'user5')
    create_user(uid2, 'g user6low', 'user6')

    _reindex_uid(topology_st)

    _search_and_assert(topology_st, f'(uid={uid1[:len(UID_PREFIX)+1]}*)', 1,
                       'low: begin=2 single char initial')
    _search_and_assert(topology_st, '(uid=*5lo*)', 1, 'low: middle=2 any search unique')
    _search_and_assert(topology_st, f'(uid=*{uid1[-4:]})', 1, 'low: end=2 final search')
    _search_and_assert(topology_st, '(uid=*low)', 2, 'low: final matching both')


def test_substr_begin_1_middle_3_end_3(topology_st, uid_index, create_user):
    """Test substring index with nsSubStrBegin=1, nsSubStrMiddle=3, nsSubStrEnd=3.

    :id: c28166e9-41aa-44ee-ba66-3300ea3ea4d1
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Configure uid index with begin=1, middle=3, end=3
        2. Reindex uid attribute
        3. Add two test users
        4. Search with 1-char initial
        5. Search with 3-char initial
        6. Search with 3-char any
        7. Search with 3-char final
        8. Search with 2-char any (shorter than middle=3)
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Returns 1 user
        5. Returns 1 user
        6. Returns 2 users
        7. Returns 2 users
        8. Returns 1 user
    """
    uid_index.add_many(
        ('objectClass', 'extensibleObject'),
        ('nsIndexType', 'sub'),
        ('nsSubStrBegin', '1'),
        ('nsSubStrMiddle', '3'),
        ('nsSubStrEnd', '3'),
    )

    uid1 = f'{UID_PREFIX}h7degen'
    uid2 = f'{UID_PREFIX}i8degen'
    create_user(uid1, 'h user7', 'user7')
    create_user(uid2, 'i user8', 'user8')

    _reindex_uid(topology_st)

    _search_and_assert(topology_st, f'(uid={uid1})', 1, 'exact match sanity check')
    _search_and_assert(topology_st, f'(uid={uid1[:len(UID_PREFIX)+2]}*)', 1,
                       'begin=1 two-char initial')
    _search_and_assert(topology_st, '(uid=*deg*)', 2, 'middle=3 any search')
    _search_and_assert(topology_st, '(uid=*gen)', 2, 'end=3 final search')
    _search_and_assert(topology_st, '(uid=*7d*)', 1, 'short any, post-filtered')


def test_substr_matching_rule_mixed(topology_st, uid_index, create_user):
    """Test mixed substring lengths via nsMatchingRule format.

    Uses begin=1, middle=3, end=3 via nsMatchingRule format.

    :id: 0ef6d87e-5ce2-4ea7-a8b7-9cb09f0fcf53
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Configure uid index with nsMatchingRule format:
           nssubstrbegin=1, nssubstrmiddle=3, nssubstrend=3
        2. Reindex uid attribute
        3. Add test users
        4. Search with various filters to verify functionality
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. All searches return correct results
    """
    uid_index.add_many(
        ('nsIndexType', 'sub'),
        ('nsMatchingRule', 'nssubstrbegin=1'),
        ('nsMatchingRule', 'nssubstrmiddle=3'),
        ('nsMatchingRule', 'nssubstrend=3'),
    )

    uid1 = f'{UID_PREFIX}j9mrule'
    uid2 = f'{UID_PREFIX}k10mrule'
    create_user(uid1, 'j user9', 'user9')
    create_user(uid2, 'k user10', 'user10')

    _reindex_uid(topology_st)

    _search_and_assert(topology_st, f'(uid={uid1[:len(UID_PREFIX)+2]}*)', 1,
                       'matching rule begin=1')
    _search_and_assert(topology_st, '(uid=*rul*)', 2, 'matching rule middle=3')
    _search_and_assert(topology_st, f'(uid=*{uid1[-7:]})', 1, 'matching rule end=3')


def test_substr_mixed_end_gt_middle(topology_st, uid_index, create_user):
    """Test mixed values where begin+end > middle*2 works correctly.

    :id: 33deb10a-beb1-45c3-8a4a-89ad2166a7df
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Configure uid index with begin=2, middle=2, end=3
           (begin+end > middle*2)
        2. Add test users
        3. Reindex uid
        4. Search with various substring filters
    :expectedresults:
        1. Success
        2. Success
        3. Success - no crash
        4. All searches return correct results
    """
    inst = topology_st.standalone

    uid_index.add_many(
        ('objectClass', 'extensibleObject'),
        ('nsIndexType', 'sub'),
        ('nsSubStrBegin', '2'),
        ('nsSubStrMiddle', '2'),
        ('nsSubStrEnd', '3'),
    )

    uid1 = f'{UID_PREFIX}l11endgt'
    uid2 = f'{UID_PREFIX}m12endgt'
    create_user(uid1, 'l user11', 'user11')
    create_user(uid2, 'm user12', 'user12')

    _reindex_uid(topology_st)

    # Server must still be alive after reindex
    assert inst.status(), 'Server should not crash with begin+end > middle*2'

    _search_and_assert(topology_st, f'(uid={uid1})', 1, 'exact match sanity')
    _search_and_assert(topology_st, f'(uid={uid1[:len(UID_PREFIX)+1]}*)', 1,
                       'begin=2 initial search')
    _search_and_assert(topology_st, '(uid=*endgt)', 2, 'end=3 final search')
    _search_and_assert(topology_st, '(uid=*nd*)', 2, 'middle=2 any search')


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
