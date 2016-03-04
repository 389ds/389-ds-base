import os
import sys
import time
import ldap
import logging
import socket
import time
import logging
import pytest
import re
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from constants import *


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

INDIRECT_ACTIVE_GROUP_CN = "indirect active group"
INDIRECT_ACTIVE_GROUP_DN = "cn=%s,%s" % (INDIRECT_ACTIVE_GROUP_CN, ACTIVE_DN)



log = logging.getLogger(__name__)

installation_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone



@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
        At the beginning, It may exists a standalone instance.
        It may also exists a backup for the standalone instance.
    
        Principle:
            If standalone instance exists:
                restart it
            If backup of standalone exists:
                create/rebind to standalone

                restore standalone instance from backup
            else:
                Cleanup everything
                    remove instance
                    remove backup
                Create instance
                Create backup
    '''
    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix
    
    standalone = DirSrv(verbose=False)
    
    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
        
    # Get the status of the backups
    backup_standalone = standalone.checkBackupFS()
    
    # Get the status of the instance and restart it if it exists
    instance_standalone   = standalone.exists()
    if instance_standalone:
        # assuming the instance is already stopped, just wait 5 sec max
        standalone.stop(timeout=5)
        standalone.start(timeout=10)
    
    if backup_standalone:
        # The backup exist, assuming it is correct 
        # we just re-init the instance with it
        if not instance_standalone:
            standalone.create()
            # Used to retrieve configuration information (dbdir, confdir...)
            standalone.open()
        
        # restore standalone instance from backup
        standalone.stop(timeout=10)
        standalone.restoreFS(backup_standalone)
        standalone.start(timeout=10)
        
    else:
        # We should be here only in two conditions
        #      - This is the first time a test involve standalone instance
        #      - Something weird happened (instance/backup destroyed)
        #        so we discard everything and recreate all
        
        # Remove the backup. So even if we have a specific backup file
        # (e.g backup_standalone) we clear backup that an instance may have created
        if backup_standalone:
            standalone.clearBackupFS()
        
        # Remove the instance
        if instance_standalone:
            standalone.delete()
            
        # Create the instance
        standalone.create()
        
        # Used to retrieve configuration information (dbdir, confdir...)
        standalone.open()
                
        # Time to create the backups
        standalone.stop(timeout=10)
        standalone.backupfile = standalone.backupFS()
        standalone.start(timeout=10)

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    # 
    # Here we have standalone instance up and running
    # Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
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
    try:
        if new_superior:
            topology.standalone.rename_s(entry_dn, new_rdn, newsuperior=new_superior, delold=del_old)
        else:
            topology.standalone.rename_s(entry_dn, new_rdn, delold=del_old)
    except ldap.NO_SUCH_ATTRIBUTE:
        topology.standalone.log.info("accepted failure due to 47833: modrdn reports error.. but succeeds")
        attempt = 0
        if new_superior:
            dn = "%s,%s" % (new_rdn, new_superior)
            base = new_superior
        else:
            base = ','.join(entry_dn.split(",")[1:])
            dn = "%s, %s" % (new_rdn, base)
        filter = entry_dn.split(',')[0]
            
        while attempt < 10:
            try:
                ent = topology.standalone.getEntry(dn, ldap.SCOPE_BASE, filter)
                break
            except ldap.NO_SUCH_OBJECT:
                topology.standalone.log.info("Accept failure due to 47833: unable to find (base) a modrdn entry")
                attempt += 1
                time.sleep(1)
        if attempt == 10:
            ent = topology.standalone.getEntry(base, ldap.SCOPE_SUBTREE, filter)
            ent = topology.standalone.getEntry(dn, ldap.SCOPE_BASE, filter)

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
                                                'objectclass': "top groupOfNames inetuser".split(),
                                                'cn': ACTIVE_GROUP_CN})))
    topology.standalone.add_s(Entry((STAGE_GROUP_DN, {
                                                'objectclass': "top groupOfNames inetuser".split(),
                                                'cn': STAGE_GROUP_CN})))
    topology.standalone.add_s(Entry((OUT_GROUP_DN, {
                                                'objectclass': "top groupOfNames inetuser".split(),
                                                'cn': OUT_GROUP_CN})))
    topology.standalone.add_s(Entry((INDIRECT_ACTIVE_GROUP_DN, {
                                                'objectclass': "top groupOfNames".split(),
                                                'cn': INDIRECT_ACTIVE_GROUP_CN})))
    
    # add users
    _add_user(topology, 'active')
    _add_user(topology, 'stage')
    _add_user(topology, 'out')
    
    

    # enable memberof of with scope IN except provisioning
    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    dn = "cn=%s,%s" % (PLUGIN_MEMBER_OF, DN_PLUGIN)
    topology.standalone.modify_s(dn, [(ldap.MOD_REPLACE, 'memberOfEntryScope', SCOPE_IN_DN)])
    topology.standalone.modify_s(dn, [(ldap.MOD_REPLACE, 'memberOfEntryScopeExcludeSubtree', PROVISIONING_DN)])
    
    # enable RI with scope IN except provisioning
    topology.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
    dn = "cn=%s,%s" % (PLUGIN_REFER_INTEGRITY, DN_PLUGIN)
    topology.standalone.modify_s(dn, [(ldap.MOD_REPLACE, 'nsslapd-pluginentryscope', SCOPE_IN_DN)])
    topology.standalone.modify_s(dn, [(ldap.MOD_REPLACE, 'nsslapd-plugincontainerscope', SCOPE_IN_DN)])
    topology.standalone.modify_s(dn, [(ldap.MOD_REPLACE, 'nsslapd-pluginExcludeEntryScope', PROVISIONING_DN)])
    
    
    topology.standalone.restart(timeout=10)

def test_ticket47829_mod_active_user_1(topology):
    _header(topology, 'MOD: add an active user to an active group')
    
    # add active user to active group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_member  (topology, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # remove active user to active group
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    
def test_ticket47829_mod_active_user_2(topology):
    _header(topology, 'MOD: add an Active user to a Stage group')
    
    # add active user to stage group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=ACTIVE_USER_DN, group_dn=STAGE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=ACTIVE_USER_DN, group_dn=STAGE_GROUP_DN, find_result=True)
    
    # remove active user to stage group
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=ACTIVE_USER_DN, group_dn=STAGE_GROUP_DN, find_result=False)
    
def test_ticket47829_mod_active_user_3(topology):
    _header(topology, 'MOD: add an Active user to a out of scope group')
    
    # add active user to out of scope group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=ACTIVE_USER_DN, group_dn=OUT_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=ACTIVE_USER_DN, group_dn=OUT_GROUP_DN, find_result=True)
    
    # remove active user to out of scope  group
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=ACTIVE_USER_DN, group_dn=OUT_GROUP_DN, find_result=False)
    

def test_ticket47829_mod_stage_user_1(topology):
    _header(topology, 'MOD: add an Stage user to a Active group')
    
    # add stage user to active group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=STAGE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=STAGE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # remove stage user to active group
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=STAGE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    
def test_ticket47829_mod_stage_user_2(topology):
    _header(topology, 'MOD: add an Stage user to a Stage group')
    
    # add stage user to stage group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=STAGE_USER_DN, group_dn=STAGE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=STAGE_USER_DN, group_dn=STAGE_GROUP_DN, find_result=True)
    
    # remove stage user to stage group
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=STAGE_USER_DN, group_dn=STAGE_GROUP_DN, find_result=False)
    

def test_ticket47829_mod_stage_user_3(topology):
    _header(topology, 'MOD: add an Stage user to a out of scope group')
    
    # add stage user to an out of scope group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=STAGE_USER_DN, group_dn=OUT_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=STAGE_USER_DN, group_dn=OUT_GROUP_DN, find_result=True)
    
    # remove stage user to out of scope group
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=STAGE_USER_DN, group_dn=OUT_GROUP_DN, find_result=False)
        


def test_ticket47829_mod_out_user_1(topology):
    _header(topology, 'MOD: add an out of scope user to an active group')
    
    # add out of scope user to active group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=OUT_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=OUT_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # remove out of scope user to active group
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=OUT_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    

def test_ticket47829_mod_out_user_2(topology):
    _header(topology, 'MOD: add an out of scope user to a Stage group')
    
    # add out of scope user to stage group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=OUT_USER_DN, group_dn=STAGE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=OUT_USER_DN, group_dn=STAGE_GROUP_DN, find_result=True)
    
    # remove out of scope user to stage group
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=OUT_USER_DN, group_dn=STAGE_GROUP_DN, find_result=False)
    
def test_ticket47829_mod_out_user_3(topology):
    _header(topology, 'MOD: add an out of scope user to an out of scope group')
    
    # add out of scope user to stage group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=OUT_USER_DN, group_dn=OUT_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=OUT_USER_DN, group_dn=OUT_GROUP_DN, find_result=True)
    
    # remove out of scope user to stage group
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=OUT_USER_DN, group_dn=OUT_GROUP_DN, find_result=False)


def test_ticket47829_mod_active_user_modrdn_active_user_1(topology):
    _header(topology, 'add an Active user to a Active group. Then move Active user to Active')
    
    # add Active user to active group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_member  (topology, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move the Active entry to active, expect 'member' and 'memberof'
    _modrdn_entry(topology, entry_dn=ACTIVE_USER_DN, new_rdn="cn=x%s" % ACTIVE_USER_CN, new_superior=ACTIVE_DN)
    _find_memberof(topology, user_dn="cn=x%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_member(topology, user_dn="cn=x%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move the Active entry to active, expect  'member' and no 'memberof'
    _modrdn_entry(topology, entry_dn="cn=x%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), new_rdn="cn=%s" % ACTIVE_USER_CN, new_superior=ACTIVE_DN)
    _find_memberof(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # remove active user to active group
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)






def test_ticket47829_mod_active_user_modrdn_stage_user_1(topology):
    _header(topology, 'add an Active user to a Active group. Then move Active user to Stage')
    
    # add Active user to active group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_member  (topology, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move the Active entry to stage, expect no 'member' and 'memberof'
    _modrdn_entry(topology, entry_dn=ACTIVE_USER_DN, new_rdn="cn=%s" % ACTIVE_USER_CN, new_superior=STAGE_DN)
    _find_memberof(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, STAGE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, STAGE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    
    # move the Active entry to Stage, expect  'member' and no 'memberof'
    _modrdn_entry(topology, entry_dn="cn=%s,%s" %(ACTIVE_USER_CN, STAGE_DN), new_rdn="cn=%s" % ACTIVE_USER_CN, new_superior=ACTIVE_DN)
    _find_memberof(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
        

def test_ticket47829_mod_active_user_modrdn_out_user_1(topology):
    _header(topology, 'add an Active user to a Active group. Then move Active user to out of scope')
    
    # add Active user to active group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_member  (topology, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move the Active entry to out of scope, expect no 'member' and no 'memberof'
    _modrdn_entry(topology, entry_dn=ACTIVE_USER_DN, new_rdn="cn=%s" % ACTIVE_USER_CN, new_superior=OUT_GROUP_DN)
    _find_memberof(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, OUT_GROUP_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, OUT_GROUP_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    
    # move the Active entry to out of scope, expect  no 'member' and no 'memberof'
    _modrdn_entry(topology, entry_dn="cn=%s,%s" %(ACTIVE_USER_CN, OUT_GROUP_DN), new_rdn="cn=%s" % ACTIVE_USER_CN, new_superior=ACTIVE_DN)
    _find_memberof(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)

    
def test_ticket47829_mod_modrdn_1(topology):
    _header(topology, 'add an Stage user to a Active group. Then move Stage user to Active')
    
    # add Stage user to active group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=STAGE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=STAGE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move the Stage entry to active, expect 'member' and 'memberof'
    _modrdn_entry(topology, entry_dn=STAGE_USER_DN, new_rdn="cn=%s" % STAGE_USER_CN, new_superior=ACTIVE_DN)
    _find_memberof(topology, user_dn="cn=%s,%s" %(STAGE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_member(topology, user_dn="cn=%s,%s" %(STAGE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move the Active entry to Stage, expect no 'member' and no 'memberof'
    _modrdn_entry(topology, entry_dn="cn=%s,%s" %(STAGE_USER_CN, ACTIVE_DN), new_rdn="cn=%s" % STAGE_USER_CN, new_superior=STAGE_DN)
    _find_memberof(topology, user_dn="cn=%s,%s" %(STAGE_USER_CN, STAGE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member(topology, user_dn="cn=%s,%s" %(STAGE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    
    
     
     
def test_ticket47829_mod_stage_user_modrdn_active_user_1(topology):
    _header(topology, 'add an Stage user to a Active group. Then move Stage user to Active')
    
    stage_user_dn  = STAGE_USER_DN
    stage_user_rdn = "cn=%s" % STAGE_USER_CN
    active_user_dn = "cn=%s,%s" % (STAGE_USER_CN, ACTIVE_DN)
    
    # add Stage user to active group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=stage_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=stage_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move the Stage entry to Actve, expect  'member' and 'memberof'
    _modrdn_entry (topology, entry_dn=stage_user_dn, new_rdn=stage_user_rdn, new_superior=ACTIVE_DN)
    _find_memberof(topology, user_dn=active_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_member  (topology, user_dn=active_user_dn, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move the Active entry to Stage, expect  no 'member' and no 'memberof'
    _modrdn_entry (topology, entry_dn=active_user_dn, new_rdn=stage_user_rdn, new_superior=STAGE_DN)
    _find_memberof(topology, user_dn=stage_user_dn,   group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=stage_user_dn , group_dn=ACTIVE_GROUP_DN, find_result=False)
    
        
def test_ticket47829_mod_stage_user_modrdn_stage_user_1(topology):
    _header(topology, 'add an Stage user to a Active group. Then move Stage user to Stage')
    
    _header(topology, 'Return because it requires a fix for 47833')
    return
    
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
    
    # move the Stage entry to Stage, expect  no 'member' and no 'memberof'
    _modrdn_entry (topology, entry_dn=new_stage_user_dn, new_rdn=old_stage_user_rdn, new_superior=STAGE_DN)
    _find_memberof(topology, user_dn=old_stage_user_dn,   group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=old_stage_user_dn , group_dn=ACTIVE_GROUP_DN, find_result=False)
    
def test_ticket47829_indirect_active_group_1(topology):
    _header(topology, 'add an Active group (G1) to an active group (G0). Then add active user to G1')
    
    topology.standalone.modify_s(INDIRECT_ACTIVE_GROUP_DN, [(ldap.MOD_ADD, 'member', ACTIVE_GROUP_DN)])
    
    # add an active user to G1. Checks that user is memberof G1
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_memberof(topology, user_dn=ACTIVE_USER_DN,   group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=True)
    
    # remove G1 from G0
    topology.standalone.modify_s(INDIRECT_ACTIVE_GROUP_DN, [(ldap.MOD_DELETE, 'member', ACTIVE_GROUP_DN)])
    _find_memberof(topology, user_dn=ACTIVE_USER_DN,   group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    _find_memberof(topology, user_dn=ACTIVE_USER_DN,   group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # remove active user from G1
    _check_memberof(topology, action=ldap.MOD_DELETE, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    
def test_ticket47829_indirect_active_group_2(topology):
    _header(topology, 'add an Active group (G1) to an active group (G0). Then add active user to G1. Then move active user to stage')
    
    topology.standalone.modify_s(INDIRECT_ACTIVE_GROUP_DN, [(ldap.MOD_ADD, 'member', ACTIVE_GROUP_DN)])
    
    # add an active user to G1. Checks that user is memberof G1
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_memberof(topology, user_dn=ACTIVE_USER_DN,   group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=True)
    
    # remove G1 from G0
    topology.standalone.modify_s(INDIRECT_ACTIVE_GROUP_DN, [(ldap.MOD_DELETE, 'member', ACTIVE_GROUP_DN)])
    _find_memberof(topology, user_dn=ACTIVE_USER_DN,   group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    _find_memberof(topology, user_dn=ACTIVE_USER_DN,   group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move active user to stage
    _modrdn_entry(topology, entry_dn=ACTIVE_USER_DN, new_rdn="cn=%s" % ACTIVE_USER_CN, new_superior=STAGE_DN)
    
    # stage user is no long member of active group and indirect active group
    _find_memberof(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, STAGE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_memberof(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, STAGE_DN), group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    
    # active group and indirect active group do no longer have stage user as member
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, STAGE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, STAGE_DN), group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    
    # return back the entry to active. It remains not member
    _modrdn_entry(topology, entry_dn="cn=%s,%s" %(ACTIVE_USER_CN, STAGE_DN), new_rdn="cn=%s" % ACTIVE_USER_CN, new_superior=ACTIVE_DN)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    
def test_ticket47829_indirect_active_group_3(topology):
    _header(topology, 'add an Active group (G1) to an active group (G0). Then add active user to G1. Then move active user to out of the scope')
    
    topology.standalone.modify_s(INDIRECT_ACTIVE_GROUP_DN, [(ldap.MOD_ADD, 'member', ACTIVE_GROUP_DN)])
    
    # add an active user to G1. Checks that user is memberof G1
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=ACTIVE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_memberof(topology, user_dn=ACTIVE_USER_DN,   group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=True)
    
    # remove G1 from G0
    topology.standalone.modify_s(INDIRECT_ACTIVE_GROUP_DN, [(ldap.MOD_DELETE, 'member', ACTIVE_GROUP_DN)])
    _find_memberof(topology, user_dn=ACTIVE_USER_DN,   group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    _find_memberof(topology, user_dn=ACTIVE_USER_DN,   group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move active user to out of the scope
    _modrdn_entry(topology, entry_dn=ACTIVE_USER_DN, new_rdn="cn=%s" % ACTIVE_USER_CN, new_superior=SCOPE_OUT_DN)
    
    # stage user is no long member of active group and indirect active group
    _find_memberof(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, SCOPE_OUT_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_memberof(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, SCOPE_OUT_DN), group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    
    # active group and indirect active group do no longer have stage user as member
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, SCOPE_OUT_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, SCOPE_OUT_DN), group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    
    # return back the entry to active. It remains not member
    _modrdn_entry(topology, entry_dn="cn=%s,%s" %(ACTIVE_USER_CN, SCOPE_OUT_DN), new_rdn="cn=%s" % ACTIVE_USER_CN, new_superior=ACTIVE_DN)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member(topology, user_dn="cn=%s,%s" %(ACTIVE_USER_CN, ACTIVE_DN), group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)

def test_ticket47829_indirect_active_group_4(topology):
    _header(topology, 'add an Active group (G1) to an active group (G0). Then add stage user to G1. Then move user to active. Then move it back')
    
    topology.standalone.modify_s(INDIRECT_ACTIVE_GROUP_DN, [(ldap.MOD_ADD, 'member', ACTIVE_GROUP_DN)])
    
    # add stage user to active group
    _check_memberof(topology, action=ldap.MOD_ADD, user_dn=STAGE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=STAGE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_member  (topology, user_dn=STAGE_USER_DN, group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    _find_memberof(topology, user_dn=STAGE_USER_DN, group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    _find_memberof(topology, user_dn=STAGE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    
    # move stage user to active
    _modrdn_entry(topology, entry_dn=STAGE_USER_DN, new_rdn="cn=%s" % STAGE_USER_CN, new_superior=ACTIVE_DN)
    renamed_stage_dn = "cn=%s,%s" %(STAGE_USER_CN, ACTIVE_DN)
    _find_member  (topology, user_dn=renamed_stage_dn, group_dn=ACTIVE_GROUP_DN, find_result=True)
    _find_member  (topology, user_dn=renamed_stage_dn, group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    _find_memberof(topology, user_dn=renamed_stage_dn, group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=True)
    _find_memberof(topology, user_dn=renamed_stage_dn, group_dn=ACTIVE_GROUP_DN, find_result=True)
    
    # move back active to stage
    _modrdn_entry(topology, entry_dn=renamed_stage_dn, new_rdn="cn=%s" % STAGE_USER_CN, new_superior=STAGE_DN)    
    _find_member  (topology, user_dn=STAGE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    _find_member  (topology, user_dn=STAGE_USER_DN, group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    _find_memberof(topology, user_dn=STAGE_USER_DN, group_dn=INDIRECT_ACTIVE_GROUP_DN, find_result=False)
    _find_memberof(topology, user_dn=STAGE_USER_DN, group_dn=ACTIVE_GROUP_DN, find_result=False)
    
def test_ticket47829_final(topology):
    topology.standalone.stop(timeout=10)
    


def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to 
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix =  None
        
    topo = topology(True)
    test_ticket47829_init(topo)

    test_ticket47829_mod_active_user_1(topo)
    test_ticket47829_mod_active_user_2(topo)
    test_ticket47829_mod_active_user_3(topo)
    test_ticket47829_mod_stage_user_1(topo)
    test_ticket47829_mod_stage_user_2(topo)
    test_ticket47829_mod_stage_user_3(topo)
    test_ticket47829_mod_out_user_1(topo)
    test_ticket47829_mod_out_user_2(topo)
    test_ticket47829_mod_out_user_3(topo)
    
    test_ticket47829_mod_active_user_modrdn_active_user_1(topo)
    test_ticket47829_mod_active_user_modrdn_stage_user_1(topo)
    test_ticket47829_mod_active_user_modrdn_out_user_1(topo)
    
    test_ticket47829_mod_stage_user_modrdn_active_user_1(topo)
    test_ticket47829_mod_stage_user_modrdn_stage_user_1(topo)
    
    test_ticket47829_indirect_active_group_1(topo)
    test_ticket47829_indirect_active_group_2(topo)
    test_ticket47829_indirect_active_group_3(topo)
    test_ticket47829_indirect_active_group_4(topo)
    
    test_ticket47829_final(topo)

if __name__ == '__main__':
    run_isolated()

