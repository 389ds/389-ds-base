# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import pytest
import ldap
from ldap.controls.readentry import PostReadControl
from lib389.idm.user import UserAccounts, UserAccount
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


def test_postread_ctrl_modify(topology_st):
    """Test PostReadControl with LDAP modify operations.

    :id: 47920dc1-7a9b-4e8d-9f3a-6c5d4e3f2a1b
    :setup: Standalone instance
    :steps:
        1. Create test user entry with initial description
        2. Verify initial description value
        3. Modify description with PostReadControl requesting 'cn' and 'description'
        4. Verify PostReadControl response contains both requested attributes
        5. Verify the entry was actually modified in the database
    :expectedresults:
        1. User entry created successfully
        2. Initial description matches expected value
        3. Modify operation with control succeeds
        4. Control response contains both 'cn' and 'description' attributes
        5. Database entry reflects the modification
    """
    inst = topology_st.standalone
    INITIAL_DESC = "initial description"
    FINAL_DESC = "final description"

    log.info("Create test user with initial description")
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'postread_user',
        'cn': 'postread_user',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/postread_user',
        'description': INITIAL_DESC
    })

    log.info("Verify initial description value")
    assert user.present('description', INITIAL_DESC)

    log.info("Modify description with PostReadControl")
    pr_ctrl = PostReadControl(criticality=True, attrList=['cn', 'description'])
    msg_id = inst.modify_ext(
        user.dn,
        [(ldap.MOD_REPLACE, 'description', FINAL_DESC.encode('utf-8'))],
        serverctrls=[pr_ctrl]
    )

    log.info("Get result with response controls")
    _, _, _, resp_ctrls = inst.result3(msg_id)

    log.info("Verify PostReadControl response is properly encoded")
    assert resp_ctrls, "Server should return PostReadControl"
    assert resp_ctrls[0].dn == user.dn, "Control should return correct DN"
    assert 'description' in resp_ctrls[0].entry, "Control should return description attribute"
    assert 'cn' in resp_ctrls[0].entry, "Control should return cn attribute"

    log.info("Verify entry was modified with correct value")
    user = UserAccount(inst, user.dn)
    assert user.get_attr_val_utf8('description') == FINAL_DESC
    user.delete()


if __name__ == '__main__':
    CURRENT_FILE = __file__
    pytest.main(["-s", "-v", CURRENT_FILE])

