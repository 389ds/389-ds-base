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
from constants import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation_prefix = None

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
ENTRY_DN = "cn=test_entry, %s" % SUFFIX
MUST_OLD = "(postalAddress $ preferredLocale)"
MUST_NEW = "(postalAddress $ preferredLocale $ telexNumber)"
MAY_OLD  = "(postalCode $ street)"
MAY_NEW  = "(postalCode $ street $ postOfficeBox)"

def _ds_create_instance(args):
    # create the standalone instance
    return tools.DirSrvTools.createInstance(args, verbose=False)

def _ds_rebind_instance(dirsrv):
    args_instance['prefix']      = dirsrv.prefix
    args_instance['backupdir']   = dirsrv.backupdir
    args_instance['newrootdn']   = dirsrv.binddn
    args_instance['newrootpw']   = dirsrv.bindpw
    args_instance['newhost']     = dirsrv.host
    args_instance['newport']     = dirsrv.port
    args_instance['newinstance'] = dirsrv.serverId
    args_instance['newsuffix']   = SUFFIX
    args_instance['no_admin']    = True

    return tools.DirSrvTools.createInstance(args_instance)

class TopologyMasterConsumer(object):
    def __init__(self, master, consumer):
        self.master = _ds_rebind_instance(master)
        self.consumer = _ds_rebind_instance(consumer)

def pattern_errorlog(file, log_pattern):
    try:
        pattern_errorlog.last_pos += 1
    except AttributeError:
        pattern_errorlog.last_pos = 0
    
    found = None
    log.debug("_pattern_errorlog: start at offset %d" % pattern_errorlog.last_pos)
    file.seek(pattern_errorlog.last_pos)
    
    # Use a while true iteration because 'for line in file: hit a
    # python bug that break file.tell()
    while True:
        line = file.readline()
        log.debug("_pattern_errorlog: [%d] %s" % (file.tell(), line))
        found = log_pattern.search(line)
        if ((line == '') or (found)):
            break
        
    log.debug("_pattern_errorlog: end at offset %d" % file.tell())
    pattern_errorlog.last_pos = file.tell()
    return found

def _oc_definition(oid_ext, name, must=None, may=None):
    oid  = "1.2.3.4.5.6.7.8.9.10.%d" % oid_ext
    desc = 'To test ticket 47490'
    sup  = 'person'
    if not must:
        must = MUST_OLD
    if not may:
        may = MAY_OLD
    
    new_oc = "( %s  NAME '%s' DESC '%s' SUP %s AUXILIARY MUST %s MAY %s )" % (oid, name, desc, sup, must, may)
    return new_oc

def add_OC(instance, oid_ext, name):
    new_oc = _oc_definition(oid_ext, name)
    instance.addSchema('objectClasses', new_oc)

def mod_OC(instance, oid_ext, name, old_must=None, old_may=None, new_must=None, new_may=None):
    old_oc = _oc_definition(oid_ext, name, old_must, old_may)
    new_oc = _oc_definition(oid_ext, name, new_must, new_may)
    instance.delSchema('objectClasses', old_oc)
    instance.addSchema('objectClasses', new_oc)
    
def trigger_schema_push(topology):
    """
        It triggers an update on the supplier. This will start a replication
        session and a schema push
    """
    try:
        trigger_schema_push.value += 1
    except AttributeError:
        trigger_schema_push.value = 1
    replace = [(ldap.MOD_REPLACE, 'telephonenumber', str(trigger_schema_push.value))]
    topology.master.modify_s(ENTRY_DN, replace)
    
    # wait 10 seconds that the update is replicated
    loop = 0
    while loop <= 10:
        try:
            ent = topology.consumer.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['telephonenumber'])
            val = ent.telephonenumber or "0"
            if int(val) == trigger_schema_push.value:
                return
            # the expected value is not yet replicated. try again
            time.sleep(1)
            loop += 1
            log.debug("trigger_schema_push: receive %s (expected %d)" % (val, trigger_schema_push.value))
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1

@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to create a replicated topology for the 'module'.
        The replicated topology is MASTER -> Consumer.
        At the beginning, It may exists a master instance and/or a consumer instance.
        It may also exists a backup for the master and/or the consumer.
    
        Principle:
            If master instance exists:
                restart it
            If consumer instance exists:
                restart it
            If backup of master AND backup of consumer exists:
                create or rebind to consumer
                create or rebind to master

                restore master   from backup
                restore consumer from backup
            else:
                Cleanup everything
                    remove instances
                    remove backups
                Create instances
                Initialize replication
                Create backups
    '''
    global installation_prefix

    if installation_prefix:
        args_instance['prefix'] = installation_prefix
    
    # Args for the master instance
    args_instance['newhost'] = HOST_MASTER
    args_instance['newport'] = PORT_MASTER
    args_instance['newinstance'] = SERVERID_MASTER
    args_master = args_instance.copy()
    
    # Args for the consumer instance
    args_instance['newhost'] = HOST_CONSUMER
    args_instance['newport'] = PORT_CONSUMER
    args_instance['newinstance'] = SERVERID_CONSUMER
    args_consumer = args_instance.copy()

    
    # Get the status of the backups
    backup_master   = DirSrvTools.existsBackup(args_master)
    backup_consumer = DirSrvTools.existsBackup(args_consumer)
    
    # Get the status of the instance and restart it if it exists
    instance_master   = DirSrvTools.existsInstance(args_master)
    if instance_master:
        DirSrvTools.stop(instance_master, timeout=10)
        DirSrvTools.start(instance_master, timeout=10)
        
    instance_consumer = DirSrvTools.existsInstance(args_consumer)
    if instance_consumer:
        DirSrvTools.stop(instance_consumer, timeout=10)
        DirSrvTools.start(instance_consumer, timeout=10)
    
    if backup_master and backup_consumer:
        # The backups exist, assuming they are correct 
        # we just re-init the instances with them
        master   = _ds_create_instance(args_master)
        consumer = _ds_create_instance(args_consumer)
        
        # restore master from backup
        DirSrvTools.stop(master, timeout=10)
        DirSrvTools.instanceRestoreFS(master, backup_master)
        DirSrvTools.start(master, timeout=10)
        
        # restore consumer from backup
        DirSrvTools.stop(consumer, timeout=10)
        DirSrvTools.instanceRestoreFS(consumer, backup_consumer)
        DirSrvTools.start(consumer, timeout=10)
    else:
        # We should be here only in two conditions
        #      - This is the first time a test involve master-consumer
        #        so we need to create everything
        #      - Something weird happened (instance/backup destroyed)
        #        so we discard everything and recreate all
        
        # Remove all the backups. So even if we have a specific backup file
        # (e.g backup_master) we clear all backups that an instance my have created
        if backup_master:
            DirSrvTools.clearInstanceBackupFS(dirsrv=instance_master)
        if backup_consumer:
            DirSrvTools.clearInstanceBackupFS(dirsrv=instance_consumer)
        
        # Remove all the instances
        if instance_master:
            DirSrvTools.removeInstance(instance_master)
        if instance_consumer:
            DirSrvTools.removeInstance(instance_consumer)
            
        # Create the instance
        master   = _ds_create_instance(args_master)
        consumer = _ds_create_instance(args_consumer)
    
        # 
        # Now prepare the Master-Consumer topology
        #
        # First Enable replication
        master.enableReplication(suffix=SUFFIX, role="master", replicaId=REPLICAID_MASTER)
        consumer.enableReplication(suffix=SUFFIX, role="consumer")
        
        # Initialize the supplier->consumer
        
        repl_agreement = master.agreement.create(consumer, SUFFIX, binddn=defaultProperties[REPLICATION_BIND_DN], bindpw=defaultProperties[REPLICATION_BIND_PW])
    
        if not repl_agreement:
            log.fatal("Fail to create a replica agreement")
            sys.exit(1)
            
        log.debug("%s created" % repl_agreement)
        master.agreement.init(SUFFIX, HOST_CONSUMER, PORT_CONSUMER)
        master.waitForReplInit(repl_agreement)
        
        # Check replication is working fine
        master.add_s(Entry((TEST_REPL_DN, {
                                                'objectclass': "top person".split(),
                                                'sn': 'test_repl',
                                                'cn': 'test_repl'})))
        loop = 0
        while loop <= 10:
            try:
                ent = consumer.getEntry(TEST_REPL_DN, ldap.SCOPE_BASE, "(objectclass=*)")
                break
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                loop += 1
                
        # Time to create the backups
        DirSrvTools.stop(master, timeout=10)
        master.backupfile = DirSrvTools.instanceBackupFS(master)
        DirSrvTools.start(master, timeout=10)
        
        DirSrvTools.stop(consumer, timeout=10)
        consumer.backupfile = DirSrvTools.instanceBackupFS(consumer)
        DirSrvTools.start(consumer, timeout=10)
    
    # 
    # Here we have two instances master and consumer
    # with replication working. Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyMasterConsumer(master, consumer)


def test_ticket47490_init(topology):
    """ 
        Initialize the test environment
    """
    log.debug("test_ticket47490_init topology %r (master %r, consumer %r" % (topology, topology.master, topology.consumer))
    # the test case will check if a warning message is logged in the 
    # error log of the supplier
    topology.master.errorlog_file = open(topology.master.errlog, "r")
    
    # This entry will be used to trigger attempt of schema push
    topology.master.add_s(Entry((ENTRY_DN, {
                                            'objectclass': "top person".split(),
                                            'sn': 'test_entry',
                                            'cn': 'test_entry'})))
    
def test_ticket47490_one(topology):
    """
        Summary: Extra OC Schema is pushed - no error
        
        If supplier schema is a superset (one extra OC) of consumer schema, then
        schema is pushed and there is no message in the error log
        State at startup:
            - supplier default schema
            - consumer default schema
        Final state
            - supplier +masterNewOCA
            - consumer +masterNewOCA
        
    """
    log.debug("test_ticket47490_one topology %r (master %r, consumer %r" % (topology, topology.master, topology.consumer))
    # update the schema of the supplier so that it is a superset of 
    # consumer. Schema should be pushed
    add_OC(topology.master, 2, 'masterNewOCA')
    
    trigger_schema_push(topology)
    master_schema_csn = topology.master.getSchemaCSN()
    consumer_schema_csn = topology.consumer.getSchemaCSN()
    
    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_one master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_one onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn
    
    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res == None
    
def test_ticket47490_two(topology):
    """
        Summary: Extra OC Schema is NOT pushed - error
        
        If consumer schema is a superset (one extra OC) of supplier schema, then
        schema is not pushed and there is a message in the error log
        State at startup 
            - supplier +masterNewOCA
            - consumer +masterNewOCA
        Final state
            - supplier +masterNewOCA +masterNewOCB
            - consumer +masterNewOCA               +consumerNewOCA
        
    """
    
    # add this OC on consumer. Supplier will no push the schema
    add_OC(topology.consumer, 1, 'consumerNewOCA')
    
    # add a new OC on the supplier so that its nsSchemaCSN is larger than the consumer (wait 2s)
    time.sleep(2)
    add_OC(topology.master, 3, 'masterNewOCB')
    
    # now push the scheam
    trigger_schema_push(topology)
    master_schema_csn = topology.master.getSchemaCSN()
    consumer_schema_csn = topology.consumer.getSchemaCSN()
    
    # Check the schemaCSN was NOT updated on the consumer
    log.debug("test_ticket47490_two master_schema_csn=%s", master_schema_csn)
    log.debug("test_ticket47490_two consumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn != consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res


def test_ticket47490_three(topology):
    """
        Summary: Extra OC Schema is pushed - no error
        
        If supplier schema is again a superset (one extra OC), then
        schema is  pushed and there is no message in the error log
        State at startup 
            - supplier +masterNewOCA +masterNewOCB
            - consumer +masterNewOCA               +consumerNewOCA
        Final state
            - supplier +masterNewOCA +masterNewOCB +consumerNewOCA
            - consumer +masterNewOCA +masterNewOCB +consumerNewOCA

    """    
    # Do an upate to trigger the schema push attempt
    # add this OC on consumer. Supplier will no push the schema
    add_OC(topology.master, 1, 'consumerNewOCA')
    
    # now push the scheam
    trigger_schema_push(topology)
    master_schema_csn = topology.master.getSchemaCSN()
    consumer_schema_csn = topology.consumer.getSchemaCSN()
    
    # Check the schemaCSN was NOT updated on the consumer
    log.debug("test_ticket47490_three master_schema_csn=%s", master_schema_csn)
    log.debug("test_ticket47490_three consumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res == None
    
def test_ticket47490_four(topology):
    """
        Summary: Same OC - extra MUST: Schema is pushed - no error
        
        If supplier schema is again a superset (OC with more MUST), then
        schema is  pushed and there is no message in the error log
        State at startup 
            - supplier +masterNewOCA +masterNewOCB +consumerNewOCA
            - consumer +masterNewOCA +masterNewOCB +consumerNewOCA
        Final state
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA
                       +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA
                       +must=telexnumber
                        
    """    
    mod_OC(topology.master, 2, 'masterNewOCA', old_must=MUST_OLD, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_OLD)
    
        
    trigger_schema_push(topology)
    master_schema_csn = topology.master.getSchemaCSN()
    consumer_schema_csn = topology.consumer.getSchemaCSN()
    
    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_four master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_four onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn
    
    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res == None
    
def test_ticket47490_five(topology):
    """
        Summary: Same OC - extra MUST: Schema is NOT pushed - error
        
        If consumer schema is  a superset (OC with more MUST), then
        schema is  not pushed and there is a message in the error log
        State at startup 
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA
                       +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA
                        +must=telexnumber
        Final state
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA
                       +must=telexnumber                   +must=telexnumber
                        
        Note: replication log is enabled to get more details
    """    
    # get more detail why it fails
    topology.master.enableReplLogging()
    
    # add telenumber to 'consumerNewOCA' on the consumer
    mod_OC(topology.consumer, 1, 'consumerNewOCA', old_must=MUST_OLD, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_OLD)
    # add a new OC on the supplier so that its nsSchemaCSN is larger than the consumer (wait 2s)
    time.sleep(2)
    add_OC(topology.master, 4, 'masterNewOCC')
        
    trigger_schema_push(topology)
    master_schema_csn = topology.master.getSchemaCSN()
    consumer_schema_csn = topology.consumer.getSchemaCSN()
    
    # Check the schemaCSN was NOT updated on the consumer
    log.debug("test_ticket47490_five master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_five onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn != consumer_schema_csn
    
    #Check that replication logging display additional message about 'telexNumber' not being
    # required in the master schema
    # This message appears before 'must not be overwritten' so it should be check first
    regex = re.compile("Attribute telexNumber is not required in 'consumerNewOCA' of the local supplier schema")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res != None
    
    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res != None

def test_ticket47490_six(topology):
    """
        Summary: Same OC - extra MUST: Schema is pushed - no error
        
        If supplier schema is  again a superset (OC with more MUST), then
        schema is  pushed and there is no message in the error log
        State at startup 
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA
                       +must=telexnumber                   +must=telexnumber
        Final state
                       
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                        
        Note: replication log is enabled to get more details
    """    

    
    # add telenumber to 'consumerNewOCA' on the consumer
    mod_OC(topology.master, 1, 'consumerNewOCA', old_must=MUST_OLD, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_OLD)
        
    trigger_schema_push(topology)
    master_schema_csn = topology.master.getSchemaCSN()
    consumer_schema_csn = topology.consumer.getSchemaCSN()
    
    # Check the schemaCSN was NOT updated on the consumer
    log.debug("test_ticket47490_six master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_six onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn
    
    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res == None




def test_ticket47490_seven(topology):
    """
        Summary: Same OC - extra MAY: Schema is pushed - no error
        
        If supplier schema is again a superset (OC with more MAY), then
        schema is  pushed and there is no message in the error log
        State at startup
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
        Final stat
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                          
    """    
    mod_OC(topology.master, 2, 'masterNewOCA', old_must=MUST_NEW, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_NEW)
    
        
    trigger_schema_push(topology)
    master_schema_csn = topology.master.getSchemaCSN()
    consumer_schema_csn = topology.consumer.getSchemaCSN()
    
    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_seven master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_seven consumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn
    
    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res == None
    

def test_ticket47490_eight(topology):
    """
        Summary: Same OC - extra MAY: Schema is NOT pushed - error
        
        If consumer schema is a superset (OC with more MAY), then
        schema is  not pushed and there is  message in the error log
        State at startup
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox
        Final state
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                                     +may=postOfficeBox
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                  +may=postOfficeBox    
    """    
    mod_OC(topology.consumer, 1, 'consumerNewOCA', old_must=MUST_NEW, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_NEW)

    # modify OC on the supplier so that its nsSchemaCSN is larger than the consumer (wait 2s)
    time.sleep(2)
    mod_OC(topology.master, 4, 'masterNewOCC', old_must=MUST_OLD, new_must=MUST_OLD, old_may=MAY_OLD, new_may=MAY_NEW)
        
    trigger_schema_push(topology)
    master_schema_csn = topology.master.getSchemaCSN()
    consumer_schema_csn = topology.consumer.getSchemaCSN()
    
    # Check the schemaCSN was not updated on the consumer
    log.debug("test_ticket47490_eight master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_eight onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn != consumer_schema_csn
    
    #Check that replication logging display additional message about 'postOfficeBox' not being
    # allowed in the master schema
    # This message appears before 'must not be overwritten' so it should be check first
    regex = re.compile("Attribute postOfficeBox is not allowed in 'consumerNewOCA' of the local supplier schema")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res != None
    
    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res != None
    
def test_ticket47490_nine(topology):
    """
        Summary: Same OC - extra MAY: Schema is pushed - no error
        
        If consumer schema is a superset (OC with more MAY), then
        schema is  not pushed and there is  message in the error log
        State at startup
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                                     +may=postOfficeBox
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                  +may=postOfficeBox 

        Final state
   
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                  +may=postOfficeBox +may=postOfficeBox
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                  +may=postOfficeBox +may=postOfficeBox
    """    
    mod_OC(topology.master, 1, 'consumerNewOCA', old_must=MUST_NEW, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_NEW)
        
    trigger_schema_push(topology)
    master_schema_csn = topology.master.getSchemaCSN()
    consumer_schema_csn = topology.consumer.getSchemaCSN()
    
    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_nine master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_nine onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn
    
    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    assert res == None
    

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
    test_ticket47490_init(topo)
    test_ticket47490_one(topo)
    test_ticket47490_two(topo)
    test_ticket47490_three(topo)
    test_ticket47490_four(topo)
    test_ticket47490_five(topo)
    test_ticket47490_six(topo)
    test_ticket47490_seven(topo)
    test_ticket47490_eight(topo)
    test_ticket47490_nine(topo)


if __name__ == '__main__':
    run_isolated()

