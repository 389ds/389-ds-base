# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import SUFFIX, DEFAULT_SUFFIX, PLUGIN_MEMBER_OF, DN_PLUGIN

SCOPE_IN_CN = 'in'
SCOPE_OUT_CN = 'out'
SCOPE_IN_DN = 'cn=%s,%s' % (SCOPE_IN_CN, SUFFIX)
SCOPE_OUT_DN = 'cn=%s,%s' % (SCOPE_OUT_CN, SUFFIX)

PROVISIONING_CN = "provisioning"
PROVISIONING_DN = "cn=%s,%s" % (PROVISIONING_CN, SCOPE_IN_DN)

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.3'), reason="Not implemented")]

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

logging.getLogger(__name__).setLevel(logging.DEBUG)
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
            'cn': ACTIVE_USER_CN})))
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


def _find_memberof(topology_st, user_dn=None, group_dn=None, find_result=True):
    assert (topology_st)
    assert (user_dn)
    assert (group_dn)
    ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
    found = False
    if ent.hasAttr('memberof'):

        for val in ent.getValues('memberof'):
            topology_st.standalone.log.info("!!!!!!! %s: memberof->%s" % (user_dn, val))
            if val == group_dn:
                found = True
                break

    if find_result:
        assert (found)
    else:
        assert (not found)


def _find_member(topology_st, user_dn=None, group_dn=None, find_result=True):
    assert (topology_st)
    assert (user_dn)
    assert (group_dn)
    ent = topology_st.standalone.getEntry(group_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['member'])
    found = False
    if ent.hasAttr('member'):

        for val in ent.getValues('member'):
            topology_st.standalone.log.info("!!!!!!! %s: member ->%s" % (group_dn, val))
            if ensure_str(val) == user_dn:
                found = True
                break

    if find_result:
        assert (found)
    else:
        assert (not found)


def _modrdn_entry(topology_st=None, entry_dn=None, new_rdn=None, del_old=0, new_superior=None):
    assert topology_st != None
    assert entry_dn != None
    assert new_rdn != None

    topology_st.standalone.log.info("\n\n######################### MODRDN %s ######################\n" % new_rdn)
    if new_superior:
        topology_st.standalone.rename_s(entry_dn, new_rdn, newsuperior=new_superior, delold=del_old)
    else:
        topology_st.standalone.rename_s(entry_dn, new_rdn, delold=del_old)


def _check_memberof(topology_st=None, action=None, user_dn=None, group_dn=None, find_result=None):
    assert (topology_st)
    assert (user_dn)
    assert (group_dn)
    if action == ldap.MOD_ADD:
        txt = 'add'
    elif action == ldap.MOD_DELETE:
        txt = 'delete'
    else:
        txt = 'replace'
    topology_st.standalone.log.info('\n%s entry %s' % (txt, user_dn))
    topology_st.standalone.log.info('to group %s' % group_dn)

    topology_st.standalone.modify_s(group_dn, [(action, 'member', ensure_bytes(user_dn))])
    time.sleep(1)
    _find_memberof(topology_st, user_dn=user_dn, group_dn=group_dn, find_result=find_result)


def test_ticket47829_init(topology_st):
    topology_st.standalone.add_s(Entry((SCOPE_IN_DN, {
        'objectclass': "top nscontainer".split(),
        'cn': SCOPE_IN_DN})))
    topology_st.standalone.add_s(Entry((SCOPE_OUT_DN, {
        'objectclass': "top nscontainer".split(),
        'cn': SCOPE_OUT_DN})))
    topology_st.standalone.add_s(Entry((PROVISIONING_DN, {
        'objectclass': "top nscontainer".split(),
        'cn': PROVISIONING_CN})))
    topology_st.standalone.add_s(Entry((ACTIVE_DN, {
        'objectclass': "top nscontainer".split(),
        'cn': ACTIVE_CN})))
    topology_st.standalone.add_s(Entry((STAGE_DN, {
        'objectclass': "top nscontainer".split(),
        'cn': STAGE_DN})))
    topology_st.standalone.add_s(Entry((DELETE_DN, {
        'objectclass': "top nscontainer".split(),
        'cn': DELETE_CN})))

    # add groups
    topology_st.standalone.add_s(Entry((ACTIVE_GROUP_DN, {
        'objectclass': "top groupOfNames".split(),
        'cn': ACTIVE_GROUP_CN})))
    topology_st.standalone.add_s(Entry((STAGE_GROUP_DN, {
        'objectclass': "top groupOfNames".split(),
        'cn': STAGE_GROUP_CN})))
    topology_st.standalone.add_s(Entry((OUT_GROUP_DN, {
        'objectclass': "top groupOfNames".split(),
        'cn': OUT_GROUP_CN})))

    # add users
    _add_user(topology_st, 'active')
    _add_user(topology_st, 'stage')
    _add_user(topology_st, 'out')

    # enable memberof of with scope account
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    dn = "cn=%s,%s" % (PLUGIN_MEMBER_OF, DN_PLUGIN)
    topology_st.standalone.modify_s(dn, [(ldap.MOD_REPLACE, 'memberOfEntryScope', ensure_bytes(ACTIVE_DN))])

    topology_st.standalone.restart(timeout=10)


def test_ticket47829_mod_stage_user_modrdn_stage_user_1(topology_st):
    _header(topology_st, 'add an Stage user to a Active group. Then move Stage user to Stage')

    old_stage_user_dn = STAGE_USER_DN
    old_stage_user_rdn = "cn=%s" % STAGE_USER_CN
    new_stage_user_rdn = "cn=x%s" % STAGE_USER_CN
    new_stage_user_dn = "%s,%s" % (new_stage_user_rdn, STAGE_DN)

    # add Stage user to active group
    _check_memberof(topology_st, action=ldap.MOD_ADD, user_dn=old_stage_user_dn, group_dn=ACTIVE_GROUP_DN,
                    find_result=False)
    _find_member(topology_st, user_dn=old_stage_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=True)

    # move the Stage entry to Stage, expect  no 'member' and 'memberof'
    _modrdn_entry(topology_st, entry_dn=old_stage_user_dn, new_rdn=new_stage_user_rdn, new_superior=STAGE_DN)
    _find_memberof(topology_st, user_dn=new_stage_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member(topology_st, user_dn=new_stage_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=False)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
