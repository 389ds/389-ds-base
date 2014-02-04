'''
Created on Nov 7, 2013

@author: tbordaz
'''
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
from lib389._constants import REPLICAROLE_MASTER

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

#
# important part. We can deploy Master1 and Master2 on different versions
#
installation1_prefix = None
installation2_prefix = None

SCHEMA_DN    = "cn=schema"
TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
OC_NAME      = 'OCticket47676'
OC_OID_EXT   = 2
MUST = "(postalAddress $ postalCode)"
MAY  = "(member $ street)"

OC2_NAME    = 'OC2ticket47676'
OC2_OID_EXT = 3
MUST_2 = "(postalAddress $ postalCode)"
MAY_2  = "(member $ street)"

REPL_SCHEMA_POLICY_CONSUMER = "cn=consumerUpdatePolicy,cn=replSchema,cn=config"
REPL_SCHEMA_POLICY_SUPPLIER = "cn=supplierUpdatePolicy,cn=replSchema,cn=config"

OTHER_NAME = 'other_entry'
MAX_OTHERS = 10

BIND_NAME  = 'bind_entry'
BIND_DN    = 'cn=%s, %s' % (BIND_NAME, SUFFIX)
BIND_PW    = 'password'

ENTRY_NAME = 'test_entry'
ENTRY_DN   = 'cn=%s, %s' % (ENTRY_NAME, SUFFIX)
ENTRY_OC   = "top person %s" % OC_NAME

BASE_OID = "1.2.3.4.5.6.7.8.9.10"
    
def _oc_definition(oid_ext, name, must=None, may=None):
    oid  = "%s.%d" % (BASE_OID, oid_ext)
    desc = 'To test ticket 47490'
    sup  = 'person'
    if not must:
        must = MUST
    if not may:
        may = MAY
    
    new_oc = "( %s  NAME '%s' DESC '%s' SUP %s AUXILIARY MUST %s MAY %s )" % (oid, name, desc, sup, must, may)
    return new_oc
class TopologyMaster1Master2(object):
    def __init__(self, master1, master2):
        master1.open()
        self.master1 = master1
        
        master2.open()
        self.master2 = master2


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to create a replicated topology for the 'module'.
        The replicated topology is MASTER1 <-> Master2.
        At the beginning, It may exists a master2 instance and/or a master2 instance.
        It may also exists a backup for the master1 and/or the master2.
    
        Principle:
            If master1 instance exists:
                restart it
            If master2 instance exists:
                restart it
            If backup of master1 AND backup of master2 exists:
                create or rebind to master1
                create or rebind to master2

                restore master1 from backup
                restore master2 from backup
            else:
                Cleanup everything
                    remove instances
                    remove backups
                Create instances
                Initialize replication
                Create backups
    '''
    global installation1_prefix
    global installation2_prefix

    # allocate master1 on a given deployement
    master1   = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
        
    # Args for the master1 instance
    args_instance[SER_HOST] = HOST_MASTER_1
    args_instance[SER_PORT] = PORT_MASTER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_1
    args_master = args_instance.copy()
    master1.allocate(args_master)
    
    # allocate master1 on a given deployement
    master2 = DirSrv(verbose=False)
    if installation2_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation2_prefix
        
    # Args for the consumer instance
    args_instance[SER_HOST] = HOST_MASTER_2
    args_instance[SER_PORT] = PORT_MASTER_2
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_2
    args_master = args_instance.copy()
    master2.allocate(args_master)

    
    # Get the status of the backups
    backup_master1 = master1.checkBackupFS()
    backup_master2 = master2.checkBackupFS()
    
    # Get the status of the instance and restart it if it exists
    instance_master1   = master1.exists()
    if instance_master1:
        master1.stop(timeout=10)
        master1.start(timeout=10)
        
    instance_master2 = master2.exists()
    if instance_master2:
        master2.stop(timeout=10)
        master2.start(timeout=10)
    
    if backup_master1 and backup_master2:
        # The backups exist, assuming they are correct 
        # we just re-init the instances with them
        if not instance_master1:
            master1.create()
            # Used to retrieve configuration information (dbdir, confdir...)
            master1.open()
        
        if not instance_master2:
            master2.create()
            # Used to retrieve configuration information (dbdir, confdir...)
            master2.open()
        
        # restore master1 from backup
        master1.stop(timeout=10)
        master1.restoreFS(backup_master1)
        master1.start(timeout=10)
        
        # restore master2 from backup
        master2.stop(timeout=10)
        master2.restoreFS(backup_master2)
        master2.start(timeout=10)
    else:
        # We should be here only in two conditions
        #      - This is the first time a test involve master-consumer
        #        so we need to create everything
        #      - Something weird happened (instance/backup destroyed)
        #        so we discard everything and recreate all
        
        # Remove all the backups. So even if we have a specific backup file
        # (e.g backup_master) we clear all backups that an instance my have created
        if backup_master1:
            master1.clearBackupFS()
        if backup_master2:
            master2.clearBackupFS()
        
        # Remove all the instances
        if instance_master1:
            master1.delete()
        if instance_master2:
            master2.delete()
                        
        # Create the instances
        master1.create()
        master1.open()
        master2.create()
        master2.open()
    
        # 
        # Now prepare the Master-Consumer topology
        #
        # First Enable replication
        master1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_1)
        master2.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_2)
        
        # Initialize the supplier->consumer
        
        properties = {RA_NAME:      r'meTo_$host:$port',
                      RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                      RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                      RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                      RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
        repl_agreement = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    
        if not repl_agreement:
            log.fatal("Fail to create a replica agreement")
            sys.exit(1)
            
        log.debug("%s created" % repl_agreement)
        
        properties = {RA_NAME:      r'meTo_$host:$port',
                      RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                      RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                      RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                      RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
        master2.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)

        master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
        master1.waitForReplInit(repl_agreement)
        
        # Check replication is working fine
        master1.add_s(Entry((TEST_REPL_DN, {
                                                'objectclass': "top person".split(),
                                                'sn': 'test_repl',
                                                'cn': 'test_repl'})))
        loop = 0
        while loop <= 10:
            try:
                ent = master2.getEntry(TEST_REPL_DN, ldap.SCOPE_BASE, "(objectclass=*)")
                break
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                loop += 1
                
        # Time to create the backups
        master1.stop(timeout=10)
        master1.backupfile = master1.backupFS()
        master1.start(timeout=10)
        
        master2.stop(timeout=10)
        master2.backupfile = master2.backupFS()
        master2.start(timeout=10)
    
    # 
    # Here we have two instances master and consumer
    # with replication working. Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyMaster1Master2(master1, master2)


def test_ticket47676_init(topology):
    """
        It adds
           - Objectclass with MAY 'member'
           - an entry ('bind_entry') with which we bind to test the 'SELFDN' operation
        It deletes the anonymous aci
        
    """
        
    
    topology.master1.log.info("Add %s that allows 'member' attribute" % OC_NAME)
    new_oc = _oc_definition(OC_OID_EXT, OC_NAME, must = MUST, may  = MAY) 
    topology.master1.addSchema('objectClasses', new_oc)
    
    
    # entry used to bind with
    topology.master1.log.info("Add %s" % BIND_DN)
    topology.master1.add_s(Entry((BIND_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           BIND_NAME,
                                            'cn':           BIND_NAME,
                                            'userpassword': BIND_PW})))
    
    # enable acl error logging
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', str(128+8192))] # ACL + REPL
    topology.master1.modify_s(DN_CONFIG, mod)
    topology.master2.modify_s(DN_CONFIG, mod)
    
    # add dummy entries
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology.master1.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
                                            'objectclass': "top person".split(),
                                            'sn': name,
                                            'cn': name})))

def test_ticket47676_skip_oc_at(topology):
    '''
        This test ADD an entry on MASTER1 where 47676 is fixed. Then it checks that entry is replicated
        on MASTER2 (even if on MASTER2 47676 is NOT fixed). Then update on MASTER2.
        If the schema has successfully been pushed, updating Master2 should succeed
    '''
    topology.master1.log.info("\n\n######################### ADD ######################\n")
    
    # bind as 'cn=Directory manager'
    topology.master1.log.info("Bind as %s and add the add the entry with specific oc" % DN_DM)
    topology.master1.simple_bind_s(DN_DM, PASSWORD)
    
    # Prepare the entry with multivalued members
    entry = Entry(ENTRY_DN)
    entry.setValues('objectclass', 'top', 'person', 'OCticket47676')
    entry.setValues('sn', ENTRY_NAME)
    entry.setValues('cn', ENTRY_NAME)
    entry.setValues('postalAddress', 'here')
    entry.setValues('postalCode', '1234')
    members = []
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        members.append("cn=%s,%s" % (name, SUFFIX))
    members.append(BIND_DN)
    entry.setValues('member', members)
    
    topology.master1.log.info("Try to add Add  %s should be successful" % ENTRY_DN)
    topology.master1.add_s(entry)
    
    #
    # Now check the entry as been replicated
    #
    topology.master2.simple_bind_s(DN_DM, PASSWORD)
    topology.master1.log.info("Try to retrieve %s from Master2" % ENTRY_DN)
    loop = 0
    while loop <= 10:
        try:
            ent = topology.master2.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(2)
            loop += 1
    assert loop <= 10
    
    # Now update the entry on Master2 (as DM because 47676 is possibly not fixed on M2)
    topology.master1.log.info("Update  %s on M2" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'description', 'test_add')]
    topology.master2.modify_s(ENTRY_DN, mod)
    
    topology.master1.simple_bind_s(DN_DM, PASSWORD)
    loop = 0
    while loop <= 10:
        ent = topology.master1.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
        if ent.hasAttr('description') and (ent.getValue('description') == 'test_add'):
            break
        time.sleep(1)
        loop += 1
    
    assert ent.getValue('description') == 'test_add'

def test_ticket47676_reject_action(topology):
    
    topology.master1.log.info("\n\n######################### REJECT ACTION ######################\n")
    
    topology.master1.simple_bind_s(DN_DM, PASSWORD)
    topology.master2.simple_bind_s(DN_DM, PASSWORD)
    
    # make master1 to refuse to push the schema if OC_NAME is present in consumer schema
    mod = [(ldap.MOD_ADD, 'schemaUpdateObjectclassReject', '%s' % (OC_NAME) )] # ACL + REPL
    topology.master1.modify_s(REPL_SCHEMA_POLICY_SUPPLIER, mod)
    
    # Restart is required to take into account that policy
    topology.master1.stop(timeout=10)
    topology.master1.start(timeout=10)
    
    # Add a new OC on M1 so that schema CSN will change and M1 will try to push the schema
    topology.master1.log.info("Add %s on M1" % OC2_NAME)
    new_oc = _oc_definition(OC2_OID_EXT, OC2_NAME, must = MUST, may  = MAY) 
    topology.master1.addSchema('objectClasses', new_oc)
    
    # Safety checking that the schema has been updated on M1
    topology.master1.log.info("Check %s is in M1" % OC2_NAME)
    ent = topology.master1.getEntry(SCHEMA_DN, ldap.SCOPE_BASE, "(objectclass=*)", ["objectclasses"])
    assert ent.hasAttr('objectclasses')
    found = False
    for objectclass in ent.getValues('objectclasses'):
        if str(objectclass).find(OC2_NAME) >= 0:
            found = True
            break
    assert found
    
    # Do an update of M1 so that M1 will try to push the schema
    topology.master1.log.info("Update  %s on M1" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'description', 'test_reject')]
    topology.master1.modify_s(ENTRY_DN, mod)
    
    # Check the replication occured and so also M1 attempted to push the schema
    topology.master1.log.info("Check updated %s on M2" % ENTRY_DN)
    loop = 0
    while loop <= 10:
        ent = topology.master2.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
        if ent.hasAttr('description') and ent.getValue('description') == 'test_reject':
            # update was replicated
            break
        time.sleep(2)
        loop += 1
    assert loop <= 10
    
    # Check that the schema has not been pushed
    topology.master1.log.info("Check %s is not in M2" % OC2_NAME)
    ent = topology.master2.getEntry(SCHEMA_DN, ldap.SCOPE_BASE, "(objectclass=*)", ["objectclasses"])
    assert ent.hasAttr('objectclasses')
    found = False
    for objectclass in ent.getValues('objectclasses'):
        if str(objectclass).find(OC2_NAME) >= 0:
            found = True
            break
    assert not found
    
    topology.master1.log.info("\n\n######################### NO MORE REJECT ACTION ######################\n")
    
    # make master1 to do no specific action on OC_NAME
    mod = [(ldap.MOD_DELETE, 'schemaUpdateObjectclassReject', '%s' % (OC_NAME) )] # ACL + REPL
    topology.master1.modify_s(REPL_SCHEMA_POLICY_SUPPLIER, mod)
    
    # Restart is required to take into account that policy
    topology.master1.stop(timeout=10)
    topology.master1.start(timeout=10)
    
    # Do an update of M1 so that M1 will try to push the schema
    topology.master1.log.info("Update  %s on M1" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'description', 'test_no_more_reject')]
    topology.master1.modify_s(ENTRY_DN, mod)
    
    # Check the replication occured and so also M1 attempted to push the schema
    topology.master1.log.info("Check updated %s on M2" % ENTRY_DN)
    loop = 0
    while loop <= 10:
        ent = topology.master2.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
        if ent.hasAttr('description') and ent.getValue('description') == 'test_no_more_reject':
            # update was replicated
            break
        time.sleep(2)
        loop += 1
    assert loop <= 10
    
    # Check that the schema has been pushed
    topology.master1.log.info("Check %s is in M2" % OC2_NAME)
    ent = topology.master2.getEntry(SCHEMA_DN, ldap.SCOPE_BASE, "(objectclass=*)", ["objectclasses"])
    assert ent.hasAttr('objectclasses')
    found = False
    for objectclass in ent.getValues('objectclasses'):
        if str(objectclass).find(OC2_NAME) >= 0:
            found = True
            break
    assert  found
        
def test_ticket47676_final(topology):
    topology.master1.stop(timeout=10) 
    topology.master2.stop(timeout=10)   

def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to 
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation1_prefix
    global installation2_prefix
    installation1_prefix = None
    installation2_prefix = None
        
    topo = topology(True)
    topo.master1.log.info("\n\n######################### Ticket 47676 ######################\n")
    test_ticket47676_init(topo)
    
    test_ticket47676_skip_oc_at(topo)
    test_ticket47676_reject_action(topo)
    
    test_ticket47676_final(topo)
    



if __name__ == '__main__':
    run_isolated()

