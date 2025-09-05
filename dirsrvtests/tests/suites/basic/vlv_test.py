# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import ldap
import logging
import os
from ldap.controls.vlv import VLVRequestControl
from ldap.controls.sss import SSSRequestControl
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.backend import Backends
from lib389.index import VLVSearch, VLVIndex
from lib389.idm.user import nsUserAccounts
from lib389.tasks import Tasks
from lib389.properties import TASK_WAIT

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

def create_vlv_search_and_index(inst, basedn=DEFAULT_SUFFIX, bename='userRoot',
                                scope=ldap.SCOPE_SUBTREE, prefix="vlv", vlvsort="cn"):
    """Create VLV search and index"""
    backends = Backends(inst)
    backend = backends.get(bename)

    vlv_props = {
        'cn': f'{prefix}Srch',
        'vlvbase': basedn,
        'vlvfilter': '(uid=*)',
        'vlvscope': str(scope)
    }
    backend.add_vlv_search(f'{prefix}Srch', vlv_props)

    vlv_search = VLVSearch(inst, dn=f'cn={prefix}Srch,cn={bename},'
                           'cn=ldbm database,cn=plugins,cn=config')

    vlv_search.add_sort(f'{prefix}Idx', vlvsort)

    vlv_index = VLVIndex(inst, dn=f'cn={prefix}Idx,cn={prefix}Srch,'
                         f'cn={bename},cn=ldbm database,cn=plugins,cn=config')

    log.info('VLV search and index created')
    return vlv_index

def add_users(inst, users_num, suffix=DEFAULT_SUFFIX):
    """Add test users for VLV testing."""
    users = nsUserAccounts(inst, suffix)
    log.info(f'Creating {users_num} test users')
    starting_uid = 1000

    for i in range(users_num):
        uid = starting_uid + i
        users.create_test_user(uid=uid, gid=uid)

def check_vlv_search(inst):
    """Test VLV search functionality."""
    before_count = 1
    after_count = 3
    offset = 3501

    vlv_control = VLVRequestControl(
        criticality=True,
        before_count=before_count,
        after_count=after_count,
        offset=offset,
        content_count=0
    )
    sss_control = SSSRequestControl(criticality=True, ordering_rules=['cn'])

    result = inst.search_ext_s(
        base=DEFAULT_SUFFIX,
        scope=ldap.SCOPE_SUBTREE,
        filterstr='(uid=*)',
        serverctrls=[vlv_control, sss_control],
        escapehatch='i am sure'
    )

    log.info(f'VLV search returned {len(result)} entries')

    vlv_search_offset = 1000 - 2
    imin = offset + vlv_search_offset - before_count

    for i, entry in enumerate(result, start=imin):
        expected_uid = f'uid=test_user_{i},'
        log.info(f'VLV entry: found {entry.dn}, expected to start with {expected_uid}')
        assert entry.dn.lower().startswith(expected_uid.lower()), \
            f"VLV order incorrect: {entry.dn}"

def test_vlv(topology_st):
    """
    Testing bulk import when the backend with VLV was recreated.
    If the test passes without the server crash, 47966 is verified.

    :id: 512963fa-fe02-11e8-b1d3-8c16451d917b
    :setup: Standalone instance
    :steps:
        1. Add 5K users
        2. Generate vlvSearch entry
        3. Generate vlvIndex entry
        4. Reindex VLV with existing data
        5. Test VLV search functionality
    :expectedresults:
        1. Should succeed.
        2. Should succeed.
        3. Should succeed.
        4. Should succeed.
        5. Should succeed.
    """
    inst = topology_st.standalone

    add_users(inst, 5000)

    vlv_index = create_vlv_search_and_index(inst)

    reindex_task = Tasks(inst)
    assert reindex_task.reindex(suffix=DEFAULT_SUFFIX, attrname=vlv_index.rdn,
                                args={TASK_WAIT: True}, vlv=True) == 0

    check_vlv_search(inst)


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
