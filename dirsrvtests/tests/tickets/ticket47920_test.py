# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import ldap
import pytest
from ldap.controls.readentry import PostReadControl
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st

SCOPE_IN_CN = 'in'
SCOPE_OUT_CN = 'out'
SCOPE_IN_DN = 'cn=%s,%s' % (SCOPE_IN_CN, SUFFIX)
SCOPE_OUT_DN = 'cn=%s,%s' % (SCOPE_OUT_CN, SUFFIX)
from lib389.utils import *

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.2'), reason="Not implemented")]
PROVISIONING_CN = "provisioning"
PROVISIONING_DN = "cn=%s,%s" % (PROVISIONING_CN, SCOPE_IN_DN)

ACTIVE_CN = "accounts"
STAGE_CN = "staged users"
DELETE_CN = "deleted users"
ACTIVE_DN = "cn=%s,%s" % (ACTIVE_CN, SCOPE_IN_DN)
STAGE_DN = "cn=%s,%s" % (STAGE_CN, PROVISIONING_DN)
DELETE_DN = "cn=%s,%s" % (DELETE_CN, PROVISIONING_DN)

STAGE_USER_CN = "stage guy"
STAGE_USER_DN = "cn=%s,%s" % (STAGE_USER_CN, STAGE_DN)

ACTIVE_USER_CN = "active guy"
ACTIVE_USER_DN = "cn=%s,%s" % (ACTIVE_USER_CN, ACTIVE_DN)

OUT_USER_CN = "out guy"
OUT_USER_DN = "cn=%s,%s" % (OUT_USER_CN, SCOPE_OUT_DN)

STAGE_GROUP_CN = "stage group"
STAGE_GROUP_DN = "cn=%s,%s" % (STAGE_GROUP_CN, STAGE_DN)

ACTIVE_GROUP_CN = "active group"
ACTIVE_GROUP_DN = "cn=%s,%s" % (ACTIVE_GROUP_CN, ACTIVE_DN)

OUT_GROUP_CN = "out group"
OUT_GROUP_DN = "cn=%s,%s" % (OUT_GROUP_CN, SCOPE_OUT_DN)

INDIRECT_ACTIVE_GROUP_CN = "indirect active group"
INDIRECT_ACTIVE_GROUP_DN = "cn=%s,%s" % (INDIRECT_ACTIVE_GROUP_CN, ACTIVE_DN)

INITIAL_DESC = "inital description"
FINAL_DESC = "final description"

log = logging.getLogger(__name__)


def _header(topology_st, label):
    topology_st.standalone.log.info("\n\n###############################################")
    topology_st.standalone.log.info("#######")
    topology_st.standalone.log.info("####### %s" % label)
    topology_st.standalone.log.info("#######")
    topology_st.standalone.log.info("###############################################")


def _add_user(topology_st, type='active'):
    if type == 'active':
        topology_st.standalone.add_s(Entry((ACTIVE_USER_DN, {
            'objectclass': "top person inetuser".split(),
            'sn': ACTIVE_USER_CN,
            'cn': ACTIVE_USER_CN,
            'description': INITIAL_DESC})))
    elif type == 'stage':
        topology_st.standalone.add_s(Entry((STAGE_USER_DN, {
            'objectclass': "top person inetuser".split(),
            'sn': STAGE_USER_CN,
            'cn': STAGE_USER_CN})))
    else:
        topology_st.standalone.add_s(Entry((OUT_USER_DN, {
            'objectclass': "top person inetuser".split(),
            'sn': OUT_USER_CN,
            'cn': OUT_USER_CN})))


def test_ticket47920_init(topology_st):
    topology_st.standalone.add_s(Entry((SCOPE_IN_DN, {
        'objectclass': "top nscontainer".split(),
        'cn': SCOPE_IN_DN})))
    topology_st.standalone.add_s(Entry((ACTIVE_DN, {
        'objectclass': "top nscontainer".split(),
        'cn': ACTIVE_CN})))

    # add users
    _add_user(topology_st, 'active')


def test_ticket47920_mod_readentry_ctrl(topology_st):
    _header(topology_st, 'MOD: with a readentry control')

    topology_st.standalone.log.info("Check the initial value of the entry")
    ent = topology_st.standalone.getEntry(ACTIVE_USER_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
    assert ent.hasAttr('description')
    assert ensure_str(ent.getValue('description')) == INITIAL_DESC

    pr = PostReadControl(criticality=True, attrList=['cn', 'description'])
    _, _, _, resp_ctrls = topology_st.standalone.modify_ext_s(ACTIVE_USER_DN,
                                                              [(ldap.MOD_REPLACE, 'description', [ensure_bytes(FINAL_DESC)])],
                                                              serverctrls=[pr])

    assert resp_ctrls[0].dn == ACTIVE_USER_DN
    assert 'description' in resp_ctrls[0].entry
    assert 'cn' in resp_ctrls[0].entry
    print(resp_ctrls[0].entry['description'])

    ent = topology_st.standalone.getEntry(ACTIVE_USER_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
    assert ent.hasAttr('description')
    assert ensure_str(ent.getValue('description')) == FINAL_DESC


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
