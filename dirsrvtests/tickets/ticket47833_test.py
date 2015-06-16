import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

SCOPE_IN_CN  = 'in'
SCOPE_OUT_CN = 'out'
SCOPE_IN_DN  = 'cn=%s,%s' % (SCOPE_IN_CN, SUFFIX)
SCOPE_OUT_DN = 'cn=%s,%s' % (SCOPE_OUT_CN, SUFFIX)

PROVISIONING_CN = "provisioning"
PROVISIONING_DN = "cn=%s,%s" % (PROVISIONING_CN, SCOPE_IN_DN)

ACTIVE_CN = "accounts"
STAGE_CN  = "staged users"
DELETE_CN = "deleted users"
ACTIVE_DN = "cn=%s,%s" % (ACTIVE_CN, SCOPE_IN_DN)
STAGE_DN  = "cn=%s,%s" % (STAGE_CN, PROVISIONING_DN)
DELETE_DN  = "cn=%s,%s" % (DELETE_CN, PROVISIONING_DN)

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

installation1_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def _header(topology, label):
    topology.standalone.log.info("\n\n###############################################")
    topology.standalone.log.info("#######")
    topology.standalone.log.info("####### %s" % label)
    topology.standalone.log.info("#######")
    topology.standalone.log.info("###############################################")
    
def _add_user(topology, type='active'):
    if type == 'active':
        topology.standalone.add_s(Entry((ACTIVE_USER_DN, {
                                                'objectclass': "top person inetuser".split(),
                                                'sn': ACTIVE_USER_CN,
                                                'cn': ACTIVE_USER_CN})))
    elif type == 'stage':
        topology.standalone.add_s(Entry((STAGE_USER_DN, {
                                                'objectclass': "top person inetuser".split(),
                                                'sn': STAGE_USER_CN,
                                                'cn': STAGE_USER_CN})))
    else:
        topology.standalone.add_s(Entry((OUT_USER_DN, {
                                        'objectclass': "top person inetuser".split(),
                                        'sn': OUT_USER_CN,
                                        'cn': OUT_USER_CN})))

def _find_memberof(topology, user_dn=None, group_dn=None, find_result=True):
    assert(topology)
    assert(user_dn)
    assert(group_dn)
    ent = topology.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
    found = False
    if ent.hasAttr('memberof'):

        for val in ent.getValues('memberof'):
            topology.standalone.log.info("!!!!!!! %s: memberof->%s" % (user_dn, val))
            if val == group_dn:
                found = True
                break
            
    if find_result:
        assert(found) 
    else:
        assert(not found)

def _find_member(topology, user_dn=None, group_dn=None, find_result=True):
    assert(topology)
    assert(user_dn)
    assert(group_dn)
    ent = topology.standalone.getEntry(group_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['member'])
    found = False
    if ent.hasAttr('member'):

        for val in ent.getValues('member'):
            topology.standalone.log.info("!!!!!!! %s: member ->%s" % (group_dn, val))
            if val == user_dn:
                found = True
                break
            
    if find_result:
        assert(found) 
    else:
        assert(not found)
        
def _modrdn_entry(topology=None, entry_dn=None, new_rdn=None, del_old=0, new_superior=None):
    assert topology != None
    assert entry_dn != None
    assert new_rdn != None
    
        
    topology.standalone.log.info("\n\n######################### MODRDN %s ######################\n" % new_rdn)
    if new_superior:
        topology.standalone.rename_s(entry_dn, new_rdn, newsuperior=new_superior, delold=del_old)
    else:
        topology.standalone.rename_s(entry_dn, new_rdn, delold=del_old)

def _check_memberof(topology=None, action=None, user_dn=None, group_dn=None, find_result=None):
    assert(topology)
    assert(user_dn)
    assert(group_dn)
    if action == ldap.MOD_ADD:
        txt = 'add'
    elif action == ldap.MOD_DELETE:
        txt = 'delete'
    else:
        txt = 'replace'
    topology.standalone.log.info('\n%s entry %s' % (txt, user_dn))
    topology.standalone.log.info('to group %s' % group_dn)
    
    topology.standalone.modify_s(group_dn, [(action, 'member', user_dn)])
    time.sleep(1)
    _find_memberof(topology, user_dn=user_dn, group_dn=group_dn, find_result=find_result)
    
    


def test_ticket47829_init(topology):
    topology.standalone.add_s(Entry((SCOPE_IN_DN, {
                                                        'objectclass': "top nscontainer".split(),
                                                        'cn': SCOPE_IN_DN})))
    topology.standalone.add_s(Entry((SCOPE_OUT_DN, {
                                                        'objectclass': "top nscontainer".split(),
                                                        'cn': SCOPE_OUT_DN})))
    topology.standalone.add_s(Entry((PROVISIONING_DN, {
                                                        'objectclass': "top nscontainer".split(),
                                                        'cn': PROVISIONING_CN})))
    topology.standalone.add_s(Entry((ACTIVE_DN, {
                                                        'objectclass': "top nscontainer".split(),
                                                        'cn': ACTIVE_CN})))
    topology.standalone.add_s(Entry((STAGE_DN, {
                                                        'objectclass': "top nscontainer".split(),
                                                        'cn': STAGE_DN})))
    topology.standalone.add_s(Entry((DELETE_DN, {
                                                        'objectclass': "top nscontainer".split(),
                                                        'cn': DELETE_CN})))
    
    # add groups
    topology.standalone.add_s(Entry((ACTIVE_GROUP_DN, {
                                                'objectclass': "top groupOfNames".split(),
                                                'cn': ACTIVE_GROUP_CN})))
    topology.standalone.add_s(Entry((STAGE_GROUP_DN, {
                                                'objectclass': "top groupOfNames".split(),
                                                'cn': STAGE_GROUP_CN})))
    topology.standalone.add_s(Entry((OUT_GROUP_DN, {
                                                'objectclass': "top groupOfNames".split(),
                                                'cn': OUT_GROUP_CN})))
    
    # add users
    _add_user(topology, 'active')
    _add_user(topology, 'stage')
    _add_user(topology, 'out')
    
    

    # enable memberof of with scope account
    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    dn = "cn=%s,%s" % (PLUGIN_MEMBER_OF, DN_PLUGIN)
    topology.standalone.modify_s(dn, [(ldap.MOD_REPLACE, 'memberOfEntryScope', ACTIVE_DN)])
    
    
    
    topology.standalone.restart(timeout=10)


    
        
def test_ticket47829_mod_stage_user_modrdn_stage_user_1(topology):
    _header(topology, 'add an Stage user to a Active group. Then move Stage user to Stage')
    
    old_stage_user_dn  = STAGE_USER_DN
    old_stage_user_rdn = "cn=%s" % STAGE_USER_CN
    new_stage_user_rdn = "cn=x%s" % STAGE_USER_CN
    new_stage_user_dn = "%s,%s" % (new_stage_user_rdn, STAGE_DN)
    
    # add Stage user to active group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=old_stage_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=old_stage_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move the Stage entry to Stage, expect  no 'member' and 'memberof'
    _modrdn_entry (topology, entry_dn=old_stage_user_dn, new_rdn=new_stage_user_rdn, new_superior=STAGE_DN)
    _find_memberof(topology, user_dn=new_stage_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=new_stage_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=False)


def test_ticket47833_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_ticket47829_init(topo)
    test_ticket47829_mod_stage_user_modrdn_stage_user_1(topo)
    test_ticket47833_final(topo)

if __name__ == '__main__':
    run_isolated()

