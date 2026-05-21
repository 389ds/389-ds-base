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
from ldap.controls import RequestControl
from ldap.controls.readentry import PostReadControl
from lib389.idm.user import UserAccounts, UserAccount
from test389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

MANAGE_DSAIT_OID = "2.16.840.1.113730.3.4.2"
MAX_CONTROLS_PER_OP_ATTR = "nsslapd-maxcontrolsperop"

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


def _make_request_controls(count):
    return [
        RequestControl(controlType=MANAGE_DSAIT_OID, criticality=False)
        for _ in range(count)
    ]


def test_bind_excessive_controls(topology_st):
    """Bind request control count is limited by nsslapd-maxcontrolsperop

    :id: c3888f02-2107-4682-a50a-2189d1436233
    :setup: Standalone instance
    :steps:
        1. Read nsslapd-maxcontrolsperop from cn=config (default 10)
        2. Bind with one fewer control than the limit
        3. Bind with one more control than the limit
        4. Set nsslapd-maxcontrolsperop to 5
        5. Bind with 4 controls (new limit minus one)
        6. Bind with 6 controls (over new limit)
        7. Restore nsslapd-maxcontrolsperop and re-bind as Directory Manager
    :expectedresults:
        1. Config value is 10
        2. Bind succeeds
        3. Bind fails with ldap.UNWILLING_TO_PERFORM
        4. Success
        5. Bind succeeds
        6. Bind fails with ldap.UNWILLING_TO_PERFORM
        7. Success
    """
    inst = topology_st.standalone
    original_max = inst.config.get_attr_val_utf8(MAX_CONTROLS_PER_OP_ATTR)

    try:
        max_controls = int(inst.config.get_attr_val_utf8(MAX_CONTROLS_PER_OP_ATTR))
        assert max_controls == 10

        log.info("Bind with %d controls (limit %d, limit minus one)",
                 max_controls - 1, max_controls)
        inst.simple_bind_s(DN_DM, PASSWORD,
                           serverctrls=_make_request_controls(max_controls - 1))

        log.info("Bind with %d controls (limit %d plus one)",
                 max_controls + 1, max_controls)
        with pytest.raises(ldap.UNWILLING_TO_PERFORM):
            inst.simple_bind_s(DN_DM, PASSWORD,
                               serverctrls=_make_request_controls(max_controls + 1))
        inst.simple_bind_s(DN_DM, PASSWORD)

        lowered_max = 5
        log.info("Set %s to %d", MAX_CONTROLS_PER_OP_ATTR, lowered_max)
        inst.config.set(MAX_CONTROLS_PER_OP_ATTR, str(lowered_max))
        assert int(inst.config.get_attr_val_utf8(MAX_CONTROLS_PER_OP_ATTR)) == lowered_max

        log.info("Bind with %d controls (lowered limit %d, limit minus one)",
                 lowered_max - 1, lowered_max)
        inst.simple_bind_s(DN_DM, PASSWORD,
                           serverctrls=_make_request_controls(lowered_max - 1))

        log.info("Bind with %d controls (lowered limit %d plus one)",
                 lowered_max + 1, lowered_max)
        with pytest.raises(ldap.UNWILLING_TO_PERFORM):
            inst.simple_bind_s(DN_DM, PASSWORD,
                               serverctrls=_make_request_controls(lowered_max + 1))
    finally:
        log.info("Restore %s to %s", MAX_CONTROLS_PER_OP_ATTR, original_max)
        inst.config.set(MAX_CONTROLS_PER_OP_ATTR, original_max)
        inst.simple_bind_s(DN_DM, PASSWORD)


if __name__ == '__main__':
    CURRENT_FILE = __file__
    pytest.main(["-s", "-v", CURRENT_FILE])

