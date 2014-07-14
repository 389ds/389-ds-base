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

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX

STAGING_CN     = "staged user"
PRODUCTION_CN  = "accounts"
EXCEPT_CN      = "excepts"

STAGING_DN    = "cn=%s,%s" % (STAGING_CN, SUFFIX)
PRODUCTION_DN = "cn=%s,%s" % (PRODUCTION_CN, SUFFIX)
PROD_EXCEPT_DN = "cn=%s,%s" % (EXCEPT_CN, PRODUCTION_DN)

STAGING_PATTERN    = "cn=%s*,%s" % (STAGING_CN[:2],    SUFFIX)
PRODUCTION_PATTERN = "cn=%s*,%s" % (PRODUCTION_CN[:2], SUFFIX)
BAD_STAGING_PATTERN    = "cn=bad*,%s" % (SUFFIX)
BAD_PRODUCTION_PATTERN = "cn=bad*,%s" % (SUFFIX)

BIND_CN        = "bind_entry"
BIND_DN        = "cn=%s,%s" % (BIND_CN, SUFFIX)
BIND_PW        = "password"

NEW_ACCOUNT    = "new_account"
MAX_ACCOUNTS   = 20

CONFIG_MODDN_ACI_ATTR = "nsslapd-moddn-aci"

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

    # clear the tmp directory
    master1.clearTmpDir(__file__)
    
    # 
    # Here we have two instances master and consumer
    # with replication working. Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyMaster1Master2(master1, master2)



def _bind_manager(topology):
    topology.master1.log.info("Bind as %s " % DN_DM)
    topology.master1.simple_bind_s(DN_DM, PASSWORD)
    
def _bind_normal(topology):
    # bind as bind_entry
    topology.master1.log.info("Bind as %s" % BIND_DN)
    topology.master1.simple_bind_s(BIND_DN, BIND_PW)
    
def _moddn_aci_deny_tree(topology, mod_type=None, target_from=STAGING_DN, target_to=PROD_EXCEPT_DN):
    '''
    It denies the access moddn_to in cn=except,cn=accounts,SUFFIX
    '''
    assert mod_type != None
    
    ACI_TARGET_FROM = ""
    ACI_TARGET_TO   = ""
    if target_from:
        ACI_TARGET_FROM = "(target_from = \"ldap:///%s\")" % (target_from)
    if target_to:
        ACI_TARGET_TO   = "(target_to   = \"ldap:///%s\")" % (target_to)
        
    ACI_ALLOW        = "(version 3.0; acl \"Deny MODDN to prod_except\"; deny (moddn)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_TARGET_TO + ACI_TARGET_FROM + ACI_ALLOW + ACI_SUBJECT
    mod = [(mod_type, 'aci', ACI_BODY)]
    #topology.master1.modify_s(SUFFIX, mod)
    topology.master1.log.info("Add a DENY aci under %s " % PROD_EXCEPT_DN)
    topology.master1.modify_s(PROD_EXCEPT_DN, mod)
    
def _moddn_aci_staging_to_production(topology, mod_type=None, target_from=STAGING_DN, target_to=PRODUCTION_DN):
    assert mod_type != None


    ACI_TARGET_FROM = ""
    ACI_TARGET_TO   = ""
    if target_from:
        ACI_TARGET_FROM = "(target_from = \"ldap:///%s\")" % (target_from)
    if target_to:
        ACI_TARGET_TO   = "(target_to   = \"ldap:///%s\")" % (target_to)

    ACI_ALLOW        = "(version 3.0; acl \"MODDN from staging to production\"; allow (moddn)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_TARGET_FROM + ACI_TARGET_TO + ACI_ALLOW + ACI_SUBJECT
    mod = [(mod_type, 'aci', ACI_BODY)]
    topology.master1.modify_s(SUFFIX, mod)

def _moddn_aci_from_production_to_staging(topology, mod_type=None):
    assert mod_type != None
    
    ACI_TARGET       = "(target_from = \"ldap:///%s\") (target_to = \"ldap:///%s\")" % (PRODUCTION_DN, STAGING_DN)
    ACI_ALLOW        = "(version 3.0; acl \"MODDN from production to staging\"; allow (moddn)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    mod = [(mod_type, 'aci', ACI_BODY)]
    topology.master1.modify_s(SUFFIX, mod)


def test_ticket47553_init(topology):
    """
        Creates
            - a staging DIT
            - a production DIT
            - add accounts in staging DIT
            - enable ACL logging (commented for performance reason)
        
    """
    
    topology.master1.log.info("\n\n######################### INITIALIZATION ######################\n")
    
    # entry used to bind with
    topology.master1.log.info("Add %s" % BIND_DN)
    topology.master1.add_s(Entry((BIND_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           BIND_CN,
                                            'cn':           BIND_CN,
                                            'userpassword': BIND_PW})))
    
    # DIT for staging
    topology.master1.log.info("Add %s" % STAGING_DN)
    topology.master1.add_s(Entry((STAGING_DN, {
                                            'objectclass': "top organizationalRole".split(),
                                            'cn':           STAGING_CN,
                                            'description': "staging DIT"})))
    
    # DIT for production
    topology.master1.log.info("Add %s" % PRODUCTION_DN)
    topology.master1.add_s(Entry((PRODUCTION_DN, {
                                            'objectclass': "top organizationalRole".split(),
                                            'cn':           PRODUCTION_CN,
                                            'description': "production DIT"})))
    
    # DIT for production/except
    topology.master1.log.info("Add %s" % PROD_EXCEPT_DN)
    topology.master1.add_s(Entry((PROD_EXCEPT_DN, {
                                            'objectclass': "top organizationalRole".split(),
                                            'cn':           EXCEPT_CN,
                                            'description': "production except DIT"})))
    
    # enable acl error logging
    #mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '128')]
    #topology.master1.modify_s(DN_CONFIG, mod)
    #topology.master2.modify_s(DN_CONFIG, mod)
    

    
    
    
    # add dummy entries in the staging DIT
    for cpt in range(MAX_ACCOUNTS):
        name = "%s%d" % (NEW_ACCOUNT, cpt)
        topology.master1.add_s(Entry(("cn=%s,%s" % (name, STAGING_DN), {
                                            'objectclass': "top person".split(),
                                            'sn': name,
                                            'cn': name})))


def test_ticket47553_add(topology):
    '''
    This test case checks that the ADD operation fails (no ADD aci on production)
    '''
    
    topology.master1.log.info("\n\n######################### ADD (should fail) ######################\n")
    
    _bind_normal(topology)
    
    #
    # First try to add an entry in production => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to add %s" % PRODUCTION_DN)
        name = "%s%d" % (NEW_ACCOUNT, 0)
        topology.master1.add_s(Entry(("cn=%s,%s" % (name, PRODUCTION_DN), {
                                                'objectclass': "top person".split(),
                                                'sn': name,
                                                'cn': name})))
        assert 0  # this is an error, we should not be allowed to add an entry in production
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

def test_ticket47553_delete(topology):
    '''
    This test case checks that the DEL operation fails (no 'delete' aci on production)
    '''
    
    topology.master1.log.info("\n\n######################### DELETE (should fail) ######################\n")
    
    _bind_normal(topology)
    #
    # Second try to delete an entry in staging => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to delete %s" % STAGING_DN)
        name = "%s%d" % (NEW_ACCOUNT, 0)
        topology.master1.delete_s("cn=%s,%s" % (name, STAGING_DN))
        assert 0  # this is an error, we should not be allowed to add an entry in production
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
        
    
def test_ticket47553_moddn_staging_prod_0(topology):
    '''
        This test case MOVE entry NEW_ACCOUNT0 from staging to prod
        target_to/target_from: equality filter
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (0) ######################\n")
    _bind_normal(topology)
    
    old_rdn = "cn=%s0" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to and from equality filter ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
    
def test_ticket47553_moddn_staging_prod_1(topology):
    '''
        This test case MOVE entry NEW_ACCOUNT1 from staging to prod
        target_to/target_from: substring/equality filter
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (1) ######################\n")
    _bind_normal(topology)
    
    old_rdn = "cn=%s1" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to substring/ from equality filter ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=STAGING_DN, target_to=PRODUCTION_PATTERN)
    _bind_normal(topology)
    
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=STAGING_DN, target_to=PRODUCTION_PATTERN)
    _bind_normal(topology)

def test_ticket47553_moddn_staging_prod_2(topology):
    '''
        This test case fails to MOVE entry NEW_ACCOUNT2 from staging to prod
        because of bad pattern
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (2) ######################\n")
    _bind_normal(topology)
    
    old_rdn = "cn=%s2" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to substring (BAD)/ from equality filter ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=STAGING_DN, target_to=BAD_PRODUCTION_PATTERN)
    _bind_normal(topology)
    
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=STAGING_DN, target_to=BAD_PRODUCTION_PATTERN)
    _bind_normal(topology)
    
def test_ticket47553_moddn_staging_prod_3(topology):
    '''
        This test case MOVE entry NEW_ACCOUNT3 from staging to prod
        target_to/target_from: equality/substring filter
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (3) ######################\n")
    _bind_normal(topology)
    
    old_rdn = "cn=%s3" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to:equality filter / from substring filter ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=STAGING_PATTERN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=STAGING_PATTERN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
def test_ticket47553_moddn_staging_prod_4(topology):
    '''
        This test case fails to MOVE entry NEW_ACCOUNT4 from staging to prod
        because of bad pattern
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (4) ######################\n")
    _bind_normal(topology)
    
    old_rdn = "cn=%s4" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to: equality filter/ from: substring (BAD) ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=BAD_STAGING_PATTERN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=BAD_STAGING_PATTERN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
def test_ticket47553_moddn_staging_prod_5(topology):
    '''
        This test case MOVE entry NEW_ACCOUNT5 from staging to prod
        target_to/target_from: substring/substring filter
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (5) ######################\n")
    _bind_normal(topology)
    
    old_rdn = "cn=%s5" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to:substring filter / from: substring filter ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=STAGING_PATTERN, target_to=PRODUCTION_PATTERN)
    _bind_normal(topology)
    
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=STAGING_PATTERN, target_to=PRODUCTION_PATTERN)
    _bind_normal(topology)
    
def test_ticket47553_moddn_staging_prod_6(topology):
    '''
        This test case MOVE entry NEW_ACCOUNT6 from staging to prod
        target_to/target_from: substring/<enmpty> filter
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (6) ######################\n")
    _bind_normal(topology)
    
    old_rdn = "cn=%s6" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to:substring filter / from: empty ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=None, target_to=PRODUCTION_PATTERN)
    _bind_normal(topology)
    
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=None, target_to=PRODUCTION_PATTERN)
    _bind_normal(topology)
    
def test_ticket47553_moddn_staging_prod_7(topology):
    '''
        This test case MOVE entry NEW_ACCOUNT7 from staging to prod
        target_to/target_from: <empty>/substring filter
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (7) ######################\n")
    _bind_normal(topology)
    
    old_rdn = "cn=%s7" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to: empty/ from: substring filter ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=STAGING_PATTERN, target_to=None)
    _bind_normal(topology)
    
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=STAGING_PATTERN, target_to=None)
    _bind_normal(topology)
    
    
def test_ticket47553_moddn_staging_prod_8(topology):
    '''
        This test case MOVE entry NEW_ACCOUNT8 from staging to prod
        target_to/target_from: <empty>/<empty> filter
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (8) ######################\n")
    _bind_normal(topology)
    
    old_rdn = "cn=%s8" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to: empty/ from: empty ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=None, target_to=None)
    _bind_normal(topology)
    
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=None, target_to=None)
    _bind_normal(topology)
    
def test_ticket47553_moddn_staging_prod_9(topology):
    '''
        This test case disable the 'moddn' right so a MODDN requires a 'add' right
        to be successfull.
        It fails to MOVE entry NEW_ACCOUNT9 from staging to prod.
        Add a 'add' right to prod.
        Then it succeeds to MOVE NEW_ACCOUNT9 from staging to prod.
        
        Then enable the 'moddn' right so a MODDN requires a 'moddn' right
        It fails to MOVE entry NEW_ACCOUNT10 from staging to prod.
        Add a 'moddn' right to prod.
        Then it succeeds to MOVE NEW_ACCOUNT10 from staging to prod.
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (9) ######################\n")
    
    _bind_normal(topology)   
    old_rdn = "cn=%s9" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
        
    ############################################
    # Now do tests with no support of moddn aci
    ############################################
    topology.master1.log.info("Disable the moddn right" )
    _bind_manager(topology)
    mod = [(ldap.MOD_REPLACE, CONFIG_MODDN_ACI_ATTR, 'off')]
    topology.master1.modify_s(DN_CONFIG, mod)
        
    # Add the moddn aci that will not be evaluated because of the config flag
    topology.master1.log.info("\n\n######################### MOVE to and from equality filter ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
    # It will fail because it will test the ADD right
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    # remove the moddn aci
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
    # 
    # add the 'add' right to the production DN
    # Then do a successfull moddn
    #
    ACI_ALLOW        = "(version 3.0; acl \"ADD rights to allow moddn\"; allow (add)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_ALLOW + ACI_SUBJECT
    
    _bind_manager(topology)
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    topology.master1.modify_s(PRODUCTION_DN, mod)
    _bind_normal(topology)
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    _bind_manager(topology)
    mod = [(ldap.MOD_DELETE, 'aci', ACI_BODY)]
    topology.master1.modify_s(PRODUCTION_DN, mod)
    _bind_normal(topology)
    
    
    ############################################
    # Now do tests with support of moddn aci
    ############################################
    topology.master1.log.info("Enable the moddn right" )
    _bind_manager(topology)
    mod = [(ldap.MOD_REPLACE, CONFIG_MODDN_ACI_ATTR, 'on')]
    topology.master1.modify_s(DN_CONFIG, mod)
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (10) ######################\n")
    
    _bind_normal(topology)   
    old_rdn = "cn=%s10" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    # 
    # add the 'add' right to the production DN
    # Then do a failing moddn
    #
    ACI_ALLOW        = "(version 3.0; acl \"ADD rights to allow moddn\"; allow (add)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_ALLOW + ACI_SUBJECT
    
    _bind_manager(topology)
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    topology.master1.modify_s(PRODUCTION_DN, mod)
    _bind_normal(topology)
    
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    _bind_manager(topology)
    mod = [(ldap.MOD_DELETE, 'aci', ACI_BODY)]
    topology.master1.modify_s(PRODUCTION_DN, mod)
    _bind_normal(topology)
    
    # Add the moddn aci that will be evaluated because of the config flag
    topology.master1.log.info("\n\n######################### MOVE to and from equality filter ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    # remove the moddn aci
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
def test_ticket47553_moddn_prod_staging(topology):
    '''
        This test checks that we can move ACCOUNT11 from staging to prod
        but not move back ACCOUNT11 from prod to staging
    '''
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (11) ######################\n")
    
    _bind_normal(topology)
    
    old_rdn = "cn=%s11" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to and from equality filter ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    
    #
    # Now check we can not move back the entry to staging
    old_rdn = "cn=%s11" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, PRODUCTION_DN)
    new_rdn = old_rdn
    new_superior = STAGING_DN
    
    try:
        topology.master1.log.info("Try to move back MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)


def test_ticket47553_check_repl_M2_to_M1(topology):
    '''
        Checks that replication is still working M2->M1, using ACCOUNT12
    '''
    
    topology.master1.log.info("Bind as %s (M2)" % DN_DM)
    topology.master2.simple_bind_s(DN_DM, PASSWORD)
    
    rdn = "cn=%s12" % NEW_ACCOUNT
    dn  = "%s,%s" % (rdn, STAGING_DN)
    
        # First wait for the ACCOUNT19 entry being replicated on M2
    loop = 0
    while loop <= 10:
        try:
            ent = topology.master2.getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    assert loop <= 10
    
    
    attribute = 'description'
    tested_value = 'Hello world'
    mod = [(ldap.MOD_ADD, attribute, tested_value)]
    topology.master1.log.info("Update (M2) %s (%s)" % (dn, attribute))
    topology.master2.modify_s(dn, mod)
    
    loop = 0
    while loop <= 10:
        ent = topology.master1.getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
        assert ent != None
        if ent.hasAttr(attribute) and (ent.getValue(attribute) == tested_value):
            break
        
        time.sleep(1)
        loop += 1
    assert loop < 10
    topology.master1.log.info("Update %s (%s) replicated on M1" % (dn, attribute))

def test_ticket47553_moddn_staging_prod_except(topology):
    '''
        This test case MOVE entry NEW_ACCOUNT13 from staging to prod
        but fails to move entry NEW_ACCOUNT14 from staging to prod_except
    '''
    
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod (13) ######################\n")
    _bind_normal(topology)
    
    old_rdn = "cn=%s13" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    
    # 
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    
    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######################### MOVE to and from equality filter ######################\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD, target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _moddn_aci_deny_tree(topology, mod_type=ldap.MOD_ADD)
    _bind_normal(topology)
    
    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    
    #
    # Now try to move an entry  under except
    #
    topology.master1.log.info("\n\n######################### MOVE staging -> Prod/Except (14) ######################\n")
    old_rdn = "cn=%s14" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PROD_EXCEPT_DN
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    
    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE, target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _moddn_aci_deny_tree(topology, mod_type=ldap.MOD_DELETE)
    _bind_normal(topology)
        
def test_ticket47553_final(topology):
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
    topo.master1.log.info("\n\n######################### Ticket 47553 ######################\n")
    test_ticket47553_init(topo)

    
    # Check that without appropriate aci we are not allowed to add/delete
    test_ticket47553_add(topo)
    test_ticket47553_delete(topo)

    # tests the ACI as equality/substring filter
    test_ticket47553_moddn_staging_prod_0(topo)
    test_ticket47553_moddn_staging_prod_1(topo)
    test_ticket47553_moddn_staging_prod_2(topo)
    test_ticket47553_moddn_staging_prod_3(topo)
    test_ticket47553_moddn_staging_prod_4(topo)
    test_ticket47553_moddn_staging_prod_5(topo)
      
    # tests the ACI with undefined 'target_to'/'target_from'
    test_ticket47553_moddn_staging_prod_6(topo)
    test_ticket47553_moddn_staging_prod_7(topo)
    test_ticket47553_moddn_staging_prod_8(topo)
      
    # Check we can control the behavior with nsslapd-moddn-aci
    test_ticket47553_moddn_staging_prod_9(topo)
      
    # Check we can move entry 'from' -> 'to' but not 'to' -> 'from'
    test_ticket47553_moddn_prod_staging(topo)
    
    # check replication is still working
    test_ticket47553_check_repl_M2_to_M1(topo)
    
    # check DENY rule is working
    test_ticket47553_moddn_staging_prod_except(topo)
    
    test_ticket47553_final(topo)
    



if __name__ == '__main__':
    run_isolated()

