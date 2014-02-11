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
from lib389._constants import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

#
# important part. We can deploy Master1 and Master2 on different versions
#
installation1_prefix = None
installation2_prefix = None

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
OC_NAME = 'OCticket47653'
MUST = "(postalAddress $ postalCode)"
MAY  = "(member $ street)"

OTHER_NAME = 'other_entry'
MAX_OTHERS = 10

BIND_NAME  = 'bind_entry'
BIND_DN    = 'cn=%s, %s' % (BIND_NAME, SUFFIX)
BIND_PW    = 'password'

ENTRY_NAME = 'test_entry'
ENTRY_DN   = 'cn=%s, %s' % (ENTRY_NAME, SUFFIX)
ENTRY_OC   = "top person %s" % OC_NAME
    
def _oc_definition(oid_ext, name, must=None, may=None):
    oid  = "1.2.3.4.5.6.7.8.9.10.%d" % oid_ext
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


def test_ticket47653_init(topology):
    """
        It adds
           - Objectclass with MAY 'member'
           - an entry ('bind_entry') with which we bind to test the 'SELFDN' operation
        It deletes the anonymous aci
        
    """
        
    
    topology.master1.log.info("Add %s that allows 'member' attribute" % OC_NAME)
    new_oc = _oc_definition(2, OC_NAME, must = MUST, may  = MAY) 
    topology.master1.schema.add_schema('objectClasses', new_oc)
    
    
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
    
    # get read of anonymous ACI for use 'read-search' aci in SEARCH test
    ACI_ANONYMOUS = "(targetattr!=\"userPassword\")(version 3.0; acl \"Enable anonymous access\"; allow (read, search, compare) userdn=\"ldap:///anyone\";)"
    mod = [(ldap.MOD_DELETE, 'aci', ACI_ANONYMOUS)]
    topology.master1.modify_s(SUFFIX, mod)
    topology.master2.modify_s(SUFFIX, mod)
    
    # add dummy entries
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology.master1.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
                                            'objectclass': "top person".split(),
                                            'sn': name,
                                            'cn': name})))

def test_ticket47653_add(topology):
    '''
        This test ADD an entry on MASTER1 where 47653 is fixed. Then it checks that entry is replicated
        on MASTER2 (even if on MASTER2 47653 is NOT fixed). Then update on MASTER2 and check the update on MASTER1
         
        It checks that, bound as bind_entry, 
            - we can not ADD an entry without the proper SELFDN aci.
            - with the proper ACI we can not ADD with 'member' attribute
            - with the proper ACI and 'member' it succeeds to ADD
    '''
    topology.master1.log.info("\n\n######################### ADD ######################\n")
    
    # bind as bind_entry
    topology.master1.log.info("Bind as %s" % BIND_DN)
    topology.master1.simple_bind_s(BIND_DN, BIND_PW)
    
    # Prepare the entry with multivalued members
    entry_with_members = Entry(ENTRY_DN)
    entry_with_members.setValues('objectclass', 'top', 'person', 'OCticket47653')
    entry_with_members.setValues('sn', ENTRY_NAME)
    entry_with_members.setValues('cn', ENTRY_NAME)
    entry_with_members.setValues('postalAddress', 'here')
    entry_with_members.setValues('postalCode', '1234')
    members = []
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        members.append("cn=%s,%s" % (name, SUFFIX))
    members.append(BIND_DN)
    entry_with_members.setValues('member', members)
    
    # Prepare the entry with only one member value
    entry_with_member = Entry(ENTRY_DN)
    entry_with_member.setValues('objectclass', 'top', 'person', 'OCticket47653')
    entry_with_member.setValues('sn', ENTRY_NAME)
    entry_with_member.setValues('cn', ENTRY_NAME)
    entry_with_member.setValues('postalAddress', 'here')
    entry_with_member.setValues('postalCode', '1234')
    member = []
    member.append(BIND_DN)
    entry_with_member.setValues('member', member)
    
    # entry to add WITH member being BIND_DN but WITHOUT the ACI -> ldap.INSUFFICIENT_ACCESS
    try:
        topology.master1.log.info("Try to add Add  %s (aci is missing): %r" % (ENTRY_DN, entry_with_member))
        
        topology.master1.add_s(entry_with_member)
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # Ok Now add the proper ACI
    topology.master1.log.info("Bind as %s and add the ADD SELFDN aci" % DN_DM)
    topology.master1.simple_bind_s(DN_DM, PASSWORD)
    
    ACI_TARGET       = "(target = \"ldap:///cn=*,%s\")" % SUFFIX
    ACI_TARGETFILTER = "(targetfilter =\"(objectClass=%s)\")" % OC_NAME
    ACI_ALLOW        = "(version 3.0; acl \"SelfDN add\"; allow (add)"
    ACI_SUBJECT      = " userattr = \"member#selfDN\";)"
    ACI_BODY         = ACI_TARGET + ACI_TARGETFILTER + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    topology.master1.modify_s(SUFFIX, mod)
    
    # bind as bind_entry
    topology.master1.log.info("Bind as %s" % BIND_DN)
    topology.master1.simple_bind_s(BIND_DN, BIND_PW)
    
    # entry to add WITHOUT member and WITH the ACI -> ldap.INSUFFICIENT_ACCESS
    try:
        topology.master1.log.info("Try to add Add  %s (member is missing)" % ENTRY_DN)
        topology.master1.add_s(Entry((ENTRY_DN, {
                                            'objectclass':      ENTRY_OC.split(),
                                            'sn':               ENTRY_NAME,
                                            'cn':               ENTRY_NAME,
                                            'postalAddress':    'here',
                                            'postalCode':       '1234'})))
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
        
    # entry to add WITH memberS and WITH the ACI -> ldap.INSUFFICIENT_ACCESS
    # member should contain only one value
    try:
        topology.master1.log.info("Try to add Add  %s (with several member values)" % ENTRY_DN)
        topology.master1.add_s(entry_with_members)
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    topology.master1.log.info("Try to add Add  %s should be successful" % ENTRY_DN)
    topology.master1.add_s(entry_with_member)
    
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
            time.sleep(1)
            loop += 1
    assert loop <= 10
    
    # Now update the entry on Master2 (as DM because 47653 is possibly not fixed on M2)
    topology.master1.log.info("Update  %s on M2" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'description', 'test_add')]
    topology.master2.modify_s(ENTRY_DN, mod)
    
    topology.master1.simple_bind_s(DN_DM, PASSWORD)
    loop = 0
    while loop <= 10:
        try:
            ent = topology.master1.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            if ent.hasAttr('description') and (ent.getValue('description') == 'test_add'):
                break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    
    assert ent.getValue('description') == 'test_add'
    
def test_ticket47653_modify(topology):
    '''
        This test MOD an entry on MASTER1 where 47653 is fixed. Then it checks that update is replicated
        on MASTER2 (even if on MASTER2 47653 is NOT fixed). Then update on MASTER2 (bound as BIND_DN).
        This update may fail whether or not 47653 is fixed on MASTER2

        It checks that, bound as bind_entry, 
            - we can not modify an entry without the proper SELFDN aci.
            - adding the ACI, we can modify the entry
    '''
    # bind as bind_entry
    topology.master1.log.info("Bind as %s" % BIND_DN)
    topology.master1.simple_bind_s(BIND_DN, BIND_PW)
    
    topology.master1.log.info("\n\n######################### MODIFY ######################\n")
    
    # entry to modify WITH member being BIND_DN but WITHOUT the ACI -> ldap.INSUFFICIENT_ACCESS
    try:
        topology.master1.log.info("Try to modify  %s (aci is missing)" % ENTRY_DN)
        mod = [(ldap.MOD_REPLACE, 'postalCode', '9876')]
        topology.master1.modify_s(ENTRY_DN, mod)
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
        
    
    # Ok Now add the proper ACI
    topology.master1.log.info("Bind as %s and add the WRITE SELFDN aci" % DN_DM)
    topology.master1.simple_bind_s(DN_DM, PASSWORD)
    
    ACI_TARGET       = "(target = \"ldap:///cn=*,%s\")" % SUFFIX
    ACI_TARGETATTR   = "(targetattr = *)"
    ACI_TARGETFILTER = "(targetfilter =\"(objectClass=%s)\")" % OC_NAME
    ACI_ALLOW        = "(version 3.0; acl \"SelfDN write\"; allow (write)"
    ACI_SUBJECT      = " userattr = \"member#selfDN\";)"
    ACI_BODY         = ACI_TARGET + ACI_TARGETATTR + ACI_TARGETFILTER + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    topology.master1.modify_s(SUFFIX, mod)
    
    # bind as bind_entry
    topology.master1.log.info("M1: Bind as %s" % BIND_DN)
    topology.master1.simple_bind_s(BIND_DN, BIND_PW)
    
    # modify the entry and checks the value
    topology.master1.log.info("M1: Try to modify  %s. It should succeeds" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'postalCode', '1928')]
    topology.master1.modify_s(ENTRY_DN, mod)
    
    topology.master1.log.info("M1: Bind as %s" % DN_DM)
    topology.master1.simple_bind_s(DN_DM, PASSWORD)
    
    topology.master1.log.info("M1: Check the update of %s" % ENTRY_DN)
    ents = topology.master1.search_s(ENTRY_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 1
    assert ents[0].postalCode == '1928'
    
    
    #
    # Now check the update has been replicated on M2
    topology.master1.log.info("M2: Bind as %s" % DN_DM)
    topology.master2.simple_bind_s(DN_DM, PASSWORD)
    topology.master1.log.info("M2: Try to retrieve %s" % ENTRY_DN)
    loop = 0
    while loop <= 10:
        try:
            ent = topology.master2.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            if ent.hasAttr('postalCode') and (ent.getValue('postalCode') == '1928'):
                break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    assert loop <= 10
    assert ent.getValue('postalCode') == '1928'
    
    
    # Now update the entry on Master2 bound as BIND_DN (update may fail if  47653 is  not fixed on M2)
    topology.master1.log.info("M2: Update  %s (bound as %s)" % (ENTRY_DN, BIND_DN))
    topology.master2.simple_bind_s(BIND_DN, PASSWORD)
    fail = False
    try:
        mod = [(ldap.MOD_REPLACE, 'postalCode', '1929')]
        topology.master2.modify_s(ENTRY_DN, mod)
        fail = False
    except ldap.INSUFFICIENT_ACCESS:
        topology.master1.log.info("M2: Exception (INSUFFICIENT_ACCESS): that is fine the bug is possibly not fixed on M2")
        fail = True
    except Exception as e:
        topology.master1.log.info("M2: Exception (not expected): %s" % type(e).__name__)
        assert 0
    
    if not fail:
        # Check the update has been replicaed on M1
        topology.master1.log.info("M1: Bind as %s" % DN_DM)
        topology.master1.simple_bind_s(DN_DM, PASSWORD)
        topology.master1.log.info("M1: Check %s.postalCode=1929)" % (ENTRY_DN))
        loop = 0
        while loop <= 10:
            try:
                ent = topology.master1.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
                if ent.hasAttr('postalCode') and (ent.getValue('postalCode') == '1929'):
                    break
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                loop += 1
        assert ent.getValue('postalCode') == '1929'
        
def test_ticket47653_final(topology):
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
    test_ticket47653_init(topo)
    
    test_ticket47653_add(topo)
    test_ticket47653_modify(topo)
    
    test_ticket47653_final(topo)
    



if __name__ == '__main__':
    run_isolated()

