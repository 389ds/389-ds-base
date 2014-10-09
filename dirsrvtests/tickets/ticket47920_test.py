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
from ldap.controls.readentry import PreReadControl,PostReadControl


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

INITIAL_DESC="inital description"
FINAL_DESC  ="final description"



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
                                                'cn': ACTIVE_USER_CN,
                                                'description': INITIAL_DESC})))
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

def test_ticket47920_init(topology):
    topology.standalone.add_s(Entry((SCOPE_IN_DN, {
                                                        'objectclass': "top nscontainer".split(),
                                                        'cn': SCOPE_IN_DN})))
    topology.standalone.add_s(Entry((ACTIVE_DN, {
                                                        'objectclass': "top nscontainer".split(),
                                                        'cn': ACTIVE_CN})))

    # add users
    _add_user(topology, 'active')


def test_ticket47920_mod_readentry_ctrl(topology):
    _header(topology, 'MOD: with a readentry control')
    
    topology.standalone.log.info("Check the initial value of the entry")
    ent = topology.standalone.getEntry(ACTIVE_USER_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
    assert ent.hasAttr('description')
    assert ent.getValue('description') == INITIAL_DESC

    pr = PostReadControl(criticality=True,attrList=['cn', 'description'])
    _,_,_,resp_ctrls = topology.standalone.modify_ext_s(ACTIVE_USER_DN, [(ldap.MOD_REPLACE, 'description', [FINAL_DESC])], serverctrls= [pr])

    assert resp_ctrls[0].dn == ACTIVE_USER_DN
    assert resp_ctrls[0].entry.has_key('description')
    assert resp_ctrls[0].entry.has_key('cn')
    print resp_ctrls[0].entry['description']

    ent = topology.standalone.getEntry(ACTIVE_USER_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
    assert ent.hasAttr('description')
    assert ent.getValue('description') == FINAL_DESC
    
def test_ticket47920_final(topology):
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
    test_ticket47920_init(topo)

    test_ticket47920_mod_readentry_ctrl(topo)
    
    test_ticket47920_final(topo)

if __name__ == '__main__':
    run_isolated()

