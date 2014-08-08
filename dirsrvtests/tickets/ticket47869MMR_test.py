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
ENTRY_NAME = 'test_entry'
MAX_ENTRIES = 10

BIND_NAME  = 'bind_entry'
BIND_DN    = 'cn=%s, %s' % (BIND_NAME, SUFFIX)
BIND_PW    = 'password'

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
    master1 = DirSrv(verbose=False)
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
    instance_master1 = master1.exists()
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
        master1.add_s(Entry((TEST_REPL_DN, {'objectclass': "top person".split(),
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

    # clear the tmp directory
    master1.clearTmpDir(__file__)

    # 
    # Here we have two instances master and consumer
    # with replication working. Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyMaster1Master2(master1, master2)

def test_ticket47869_init(topology):
    """
        It adds an entry ('bind_entry') and 10 test entries
        It sets the anonymous aci
        
    """
    # enable acl error logging
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', str(8192))] # REPL
    topology.master1.modify_s(DN_CONFIG, mod)
    topology.master2.modify_s(DN_CONFIG, mod)
    
    # entry used to bind with
    topology.master1.log.info("Add %s" % BIND_DN)
    topology.master1.add_s(Entry((BIND_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           BIND_NAME,
                                            'cn':           BIND_NAME,
                                            'userpassword': BIND_PW})))
    loop = 0
    while loop <= 10:
        try:
            ent = topology.master2.getEntry(BIND_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    
    # keep anonymous ACI for use 'read-search' aci in SEARCH test
    ACI_ANONYMOUS = "(targetattr!=\"userPassword\")(version 3.0; acl \"Enable anonymous access\"; allow (read, search, compare) userdn=\"ldap:///anyone\";)"
    mod = [(ldap.MOD_REPLACE, 'aci', ACI_ANONYMOUS)]
    topology.master1.modify_s(SUFFIX, mod)
    topology.master2.modify_s(SUFFIX, mod)
    
    # add entries
    for cpt in range(MAX_ENTRIES):
        name = "%s%d" % (ENTRY_NAME, cpt)
        mydn = "cn=%s,%s" % (name, SUFFIX)
        topology.master1.add_s(Entry((mydn,
                                      {'objectclass': "top person".split(),
                                       'sn': name,
                                       'cn': name})))
        loop = 0
        while loop <= 10:
            try:
                ent = topology.master2.getEntry(mydn, ldap.SCOPE_BASE, "(objectclass=*)")
                break
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                loop += 1

def test_ticket47869_check(topology):
    '''
    On Master 1 and 2:
      Bind as Directory Manager.
      Search all specifying nscpEntryWsi in the attribute list.
      Check nscpEntryWsi is returned.
    On Master 1 and 2:
      Bind as Bind Entry.
      Search all specifying nscpEntryWsi in the attribute list.
      Check nscpEntryWsi is not returned.
    On Master 1 and 2:
      Bind as anonymous.
      Search all specifying nscpEntryWsi in the attribute list.
      Check nscpEntryWsi is not returned.
    '''
    topology.master1.log.info("\n\n######################### CHECK nscpentrywsi ######################\n")
    
    topology.master1.log.info("##### Master1: Bind as %s #####" % DN_DM)
    topology.master1.simple_bind_s(DN_DM, PASSWORD)

    topology.master1.log.info("Master1: Calling search_ext...")
    msgid = topology.master1.search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology.master1.result2(msgid)
    topology.master1.log.info("%d results" % len(rdata))
     
    topology.master1.log.info("Results:")
    for dn, attrs in rdata:
        topology.master1.log.info("dn: %s" % dn)
        if attrs.has_key('nscpentrywsi'):
            nscpentrywsicnt += 1
    topology.master1.log.info("Master1: count of nscpentrywsi: %d" % nscpentrywsicnt)

    topology.master2.log.info("##### Master2: Bind as %s #####" % DN_DM)
    topology.master2.simple_bind_s(DN_DM, PASSWORD)

    topology.master2.log.info("Master2: Calling search_ext...")
    msgid = topology.master2.search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology.master2.result2(msgid)
    topology.master2.log.info("%d results" % len(rdata))
     
    topology.master2.log.info("Results:")
    for dn, attrs in rdata:
        topology.master2.log.info("dn: %s" % dn)
        if attrs.has_key('nscpentrywsi'):
            nscpentrywsicnt += 1
    topology.master2.log.info("Master2: count of nscpentrywsi: %d" % nscpentrywsicnt)

    # bind as bind_entry
    topology.master1.log.info("##### Master1: Bind as %s #####" % BIND_DN)
    topology.master1.simple_bind_s(BIND_DN, BIND_PW)
    
    topology.master1.log.info("Master1: Calling search_ext...")
    msgid = topology.master1.search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology.master1.result2(msgid)
    topology.master1.log.info("%d results" % len(rdata))
     
    for dn, attrs in rdata:
        if attrs.has_key('nscpentrywsi'):
            nscpentrywsicnt += 1
    assert nscpentrywsicnt == 0
    topology.master1.log.info("Master1: count of nscpentrywsi: %d" % nscpentrywsicnt)

    # bind as bind_entry
    topology.master2.log.info("##### Master2: Bind as %s #####" % BIND_DN)
    topology.master2.simple_bind_s(BIND_DN, BIND_PW)

    topology.master2.log.info("Master2: Calling search_ext...")
    msgid = topology.master2.search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology.master2.result2(msgid)
    topology.master2.log.info("%d results" % len(rdata))
     
    for dn, attrs in rdata:
        if attrs.has_key('nscpentrywsi'):
            nscpentrywsicnt += 1
    assert nscpentrywsicnt == 0
    topology.master2.log.info("Master2: count of nscpentrywsi: %d" % nscpentrywsicnt)

    # bind as anonymous
    topology.master1.log.info("##### Master1: Bind as anonymous #####")
    topology.master1.simple_bind_s("", "")
    
    topology.master1.log.info("Master1: Calling search_ext...")
    msgid = topology.master1.search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology.master1.result2(msgid)
    topology.master1.log.info("%d results" % len(rdata))
     
    for dn, attrs in rdata:
        if attrs.has_key('nscpentrywsi'):
            nscpentrywsicnt += 1
    assert nscpentrywsicnt == 0
    topology.master1.log.info("Master1: count of nscpentrywsi: %d" % nscpentrywsicnt)

    # bind as bind_entry
    topology.master2.log.info("##### Master2: Bind as anonymous #####")
    topology.master2.simple_bind_s("", "")

    topology.master2.log.info("Master2: Calling search_ext...")
    msgid = topology.master2.search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology.master2.result2(msgid)
    topology.master2.log.info("%d results" % len(rdata))
     
    for dn, attrs in rdata:
        if attrs.has_key('nscpentrywsi'):
            nscpentrywsicnt += 1
    assert nscpentrywsicnt == 0
    topology.master2.log.info("Master2: count of nscpentrywsi: %d" % nscpentrywsicnt)

    topology.master1.log.info("##### ticket47869 was successfully verified. #####");

def test_ticket47869_final(topology):
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
    test_ticket47869_init(topo)
    
    test_ticket47869_check(topo)
    
    test_ticket47869_final(topo)

if __name__ == '__main__':
    run_isolated()

