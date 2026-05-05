# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import pytest
import itertools
import ldap
from ldap.controls import RequestControl
from ldap.controls.readentry import PostReadControl
from lib389.idm.user import UserAccounts, UserAccount
from test389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

MANAGE_DSAIT_OID = "2.16.840.1.113730.3.4.2"
TESTED_CTRLS = [ MANAGE_DSAIT_OID, "test", "test1", "test2" ]
SUPPORTED_CTRLS = [ MANAGE_DSAIT_OID, ]

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


def all_combinations(alist):
    for listlen in range(len(alist)+1):
        for val in itertools.combinations(alist, listlen):
            yield val


def try_search(inst, ignored_ctrls, sent_ctrls):
    ctrlreqs = [ RequestControl(controlType=oid, criticality=True) \
                       for oid in sent_ctrls ]
    expected = False;
    for ctrl in sent_ctrls:
        if ctrl not in SUPPORTED_CTRLS and ctrl not in ignored_ctrls:
            expected = True
    log.info(f"Ignoring: {ignored_ctrls} - Sending: {sent_ctrls} - Expected: {expected}")
    if expected:
        with pytest.raises(ldap.UNAVAILABLE_CRITICAL_EXTENSION):
            srch_res = inst.search_ext_s(DEFAULT_SUFFIX,
                        ldap.SCOPE_BASE, serverctrls=ctrlreqs)
    else:
        srch_res = inst.search_ext_s(DEFAULT_SUFFIX,
                    ldap.SCOPE_BASE, serverctrls=ctrlreqs)


def set_ignored_criticality(inst, oids):
    oids = list(oids)
    log.info(f'set_ignored_criticality({oids})')
    inst.config.set("ds-ignored-control-criticality", oids)


def test_ignored_criticality(topology_st):
    """Test ignored criticality feature

    :id: e2dc765a-329f-11f1-9d0f-c85309d5c3e3
    :setup: Standalone instance
    :steps:
        1. Define tested control set containing both supported and not supported controls
        2. Generate all subsets of tested controls
        3. For ignored_ctrls in subsets and every send_ctrls in subsets
        4. Set the ignored criticality config parameter to ignored_ctrls
        5. Perform a search with send_ctrls controls with critical flag
        6. Check if UNAVAILABLE_CRITICAL_EXTENSION is returned as expected
        7. Reset ignored criticality config parameter
    :expectedresults:
        1. Succes
        2. Succes
        3. Succes
        4. Succes
        5. Succes
        6. Expect UNAVAILABLE_CRITICAL_EXTENSION if any sent control is not supported or not ignored
        7. Succes
    """
    inst = topology_st.standalone
    ctrls = { oid: ldap.controls.RequestControl(controlType=oid, criticality=True) \
                for oid in TESTED_CTRLS }

    all_sets = list(all_combinations(TESTED_CTRLS))
    for ignored_ctrls in all_sets:
        for send_ctrls in all_sets:
            set_ignored_criticality(inst, ignored_ctrls)
            try_search(inst, ignored_ctrls, send_ctrls)
    set_ignored_criticality(inst, [])


if __name__ == '__main__':
    CURRENT_FILE = __file__
    pytest.main(["-s", "-v", CURRENT_FILE])

