# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


"""
Exact result sets for OR filters across idl_set_union's consumers: the
k-way union, the 2-list fast path, the paged ALLIDS replacement, and a
union inside an AND.  The live-singleton case is a 100-branch scale model
of issue #6275's shape.
"""

import ldap
import logging
import os
import pytest

from ldap.controls import SimplePagedResultsControl
from lib389._constants import DEFAULT_SUFFIX
from lib389.backend import DatabaseConfig
from lib389.utils import ensure_str
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

TOTAL_USERS = 500
GROUPS = 20  # sn=OrSn<i % GROUPS>: 20 equality lists of 25 IDs each


@pytest.fixture(scope="module")
def create_users(topo):
    """Import TOTAL_USERS users; sn spreads them over GROUPS equality lists."""
    inst = topo.standalone
    ldif_dir = inst.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'filter_or_union.ldif')
    with open(ldif_file, 'w') as f:
        # offline import replaces the backend contents, so the LDIF must
        # carry the suffix root and container too
        f.write(f'dn: {DEFAULT_SUFFIX}\n'
                'objectClass: top\n'
                'objectClass: domain\n'
                'dc: example\n\n'
                f'dn: ou=People,{DEFAULT_SUFFIX}\n'
                'objectClass: top\n'
                'objectClass: organizationalUnit\n'
                'ou: People\n\n')
        for i in range(TOTAL_USERS):
            uid = f'oruser{i:05d}'
            f.write(f'dn: uid={uid},ou=People,{DEFAULT_SUFFIX}\n'
                    'objectClass: top\n'
                    'objectClass: person\n'
                    'objectClass: organizationalPerson\n'
                    'objectClass: inetOrgPerson\n'
                    f'uid: {uid}\n'
                    f'cn: Or Union Test {i:05d}\n'
                    f'sn: OrSn{i % GROUPS}\n\n')
    os.chmod(ldif_file, 0o644)
    inst.stop()
    assert inst.ldif2db('userRoot', None, None, None, ldif_file)
    inst.start()
    return [f'oruser{i:05d}' for i in range(TOTAL_USERS)]


def search_uids(topo, filterstr):
    """Return the sorted uid list the filter matches."""
    entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                       filterstr, ['uid'])
    return sorted(ensure_str(e.getValue('uid')) for e in entries)


def or_of(attr, values):
    return '(|%s)' % ''.join(f'({attr}={v})' for v in values)


def test_or_many_live_singletons(topo, create_users):
    """Verify an OR of many existing uid equalities returns exactly
    those entries

    :id: 3e5f92c1-8a47-4d20-b6e9-15c8a0d7f342
    :setup: Standalone instance with 500 users over 20 sn groups
    :steps:
        1. Search an OR of 100 distinct existing uids
    :expectedresults:
        1. Exactly the 100 named users are returned
    """
    picked = create_users[::5]  # 100 users, every 5th
    assert len(picked) == 100
    assert search_uids(topo, or_of('uid', picked)) == sorted(picked)


def test_or_many_absent(topo, create_users):
    """Verify an OR dominated by absent values still returns exactly
    the entries of its live values

    :id: 9b04d7e6-2c58-4f13-a8d0-67e3b9c1f425
    :setup: Standalone instance with 500 users over 20 sn groups
    :steps:
        1. Search an OR of 150 absent uids plus 1 existing uid
    :expectedresults:
        1. Exactly the 1 existing user is returned
    """
    absent = [f'absentuser{i:05d}' for i in range(150)]
    live = create_users[7]
    assert search_uids(topo, or_of('uid', absent + [live])) == [live]


def test_or_mixed_live_absent_duplicates(topo, create_users):
    """Verify an OR mixing live values, absent values, and verbatim
    duplicate components returns the exact de-duplicated result set

    :id: c7a1f0b3-5d92-4e68-9134-fb20d6a8e571
    :setup: Standalone instance with 500 users over 20 sn groups
    :steps:
        1. Search an OR of 40 live uids (20 repeated twice) and 60 absent
    :expectedresults:
        1. Exactly the 40 distinct live users are returned, each once
    """
    live = create_users[100:140]
    absent = [f'absentuser{i:05d}' for i in range(60)]
    values = live + live[:20] + absent
    assert search_uids(topo, or_of('uid', values)) == sorted(live)


def test_or_two_lists_fastpath(topo, create_users):
    """Verify the 2-list union fast path (the issue #5170 SSSD shape)
    returns the exact result set for an OR of two equality lists

    :id: 6d82c4a9-1f35-47e0-bc76-084a9d5e213f
    :setup: Standalone instance with 500 users over 20 sn groups
    :steps:
        1. Search (|(sn=OrSn0)(sn=OrSn1))
    :expectedresults:
        1. Exactly the 50 users of the two groups are returned
    """
    expected = sorted(uid for i, uid in enumerate(create_users)
                      if i % GROUPS in (0, 1))
    assert search_uids(topo, '(|(sn=OrSn0)(sn=OrSn1))') == expected


def test_or_paged_allidslimit(topo, create_users):
    """Verify a paged OR whose union crosses a lowered
    nsslapd-idlistscanlimit still returns the exact result set: paged
    operations replace the over-limit union with ALLIDS, which forces the
    filter test instead of skipping it

    :id: f45b8d10-93c7-4a2e-8f61-27d0c3b9e684
    :setup: Standalone instance with 500 users over 20 sn groups
    :steps:
        1. Lower nsslapd-idlistscanlimit to 50
        2. Run an OR of 120 existing uids as a paged search, page size 30
    :expectedresults:
        1. Limit is set
        2. The union of the pages is exactly the 120 named users
    """
    inst = topo.standalone
    db_cfg = DatabaseConfig(inst)
    scanlimit = db_cfg.get_attr_val_utf8('nsslapd-idlistscanlimit')
    picked = create_users[200:320]
    filt = or_of('uid', picked)
    try:
        db_cfg.set([('nsslapd-idlistscanlimit', '50')])
        req = SimplePagedResultsControl(True, size=30, cookie='')
        collected = []
        while True:
            msgid = inst.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                    filt, ['uid'], serverctrls=[req])
            result_type, rdata, _, rctrls = inst.result3(msgid)
            assert result_type == ldap.RES_SEARCH_RESULT
            for _, attrs in rdata:
                collected.append(ensure_str(attrs['uid'][0]))
            pctrls = [c for c in rctrls
                      if c.controlType == SimplePagedResultsControl.controlType]
            assert pctrls
            if not pctrls[0].cookie:
                break
            req.cookie = pctrls[0].cookie
        assert sorted(collected) == sorted(picked)
    finally:
        db_cfg.set([('nsslapd-idlistscanlimit', scanlimit)])


def test_or_inside_and(topo, create_users):
    """Verify an OR union consumed by an enclosing AND intersection
    returns the exact result set

    :id: 82c6e3f7-0b94-45d1-a7c8-d19e5f04b263
    :setup: Standalone instance with 500 users over 20 sn groups
    :steps:
        1. Search (&(sn=OrSn0)(|(uid=...)(uid=...)...)) where only some of
           the OR's uids are in group 0
    :expectedresults:
        1. Exactly the OR's group-0 members are returned
    """
    in_group = [uid for i, uid in enumerate(create_users)
                if i % GROUPS == 0][:3]
    other = [uid for i, uid in enumerate(create_users)
             if i % GROUPS == 5][:4]
    filt = f'(&(sn=OrSn0){or_of("uid", in_group + other)})'
    assert search_uids(topo, filt) == sorted(in_group)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
