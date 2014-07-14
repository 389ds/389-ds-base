import os
import sys
import time
import ldap
import logging
import socket
import time
import logging
import pytest
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from constants import *

log = logging.getLogger(__name__)

installation_prefix = None

ENTRY_NAME = 'test_entry'
    

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


def test_ticket47313_run(topology):
    """
        It adds 2 test entries
        Search with filters including subtype and !
		It deletes the added entries
    """
        
    # bind as directory manager
    topology.standalone.log.info("Bind as %s" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    
    # enable filter error logging
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '32')]
    topology.standalone.modify_s(DN_CONFIG, mod)
    
    topology.standalone.log.info("\n\n######################### ADD ######################\n")
    
    # Prepare the entry with cn;fr & cn;en
    entry_name_fr = '%s fr' % (ENTRY_NAME)
    entry_name_en = '%s en' % (ENTRY_NAME)
    entry_name_both = '%s both' % (ENTRY_NAME)
    entry_dn_both = 'cn=%s, %s' % (entry_name_both, SUFFIX)
    entry_both = Entry(entry_dn_both)
    entry_both.setValues('objectclass', 'top', 'person')
    entry_both.setValues('sn', entry_name_both)
    entry_both.setValues('cn', entry_name_both)
    entry_both.setValues('cn;fr', entry_name_fr)
    entry_both.setValues('cn;en', entry_name_en)
    
    # Prepare the entry with one member
    entry_name_en_only = '%s en only' % (ENTRY_NAME)
    entry_dn_en_only = 'cn=%s, %s' % (entry_name_en_only, SUFFIX)
    entry_en_only = Entry(entry_dn_en_only)
    entry_en_only.setValues('objectclass', 'top', 'person')
    entry_en_only.setValues('sn', entry_name_en_only)
    entry_en_only.setValues('cn', entry_name_en_only)
    entry_en_only.setValues('cn;en', entry_name_en)
    
    topology.standalone.log.info("Try to add Add %s: %r" % (entry_dn_both, entry_both))
    topology.standalone.add_s(entry_both)

    topology.standalone.log.info("Try to add Add %s: %r" % (entry_dn_en_only, entry_en_only))
    topology.standalone.add_s(entry_en_only)
    
    topology.standalone.log.info("\n\n######################### SEARCH ######################\n")
    
    # filter: (&(cn=test_entry en only)(!(cn=test_entry fr)))
    myfilter = '(&(sn=%s)(!(cn=%s)))' % (entry_name_en_only, entry_name_fr)
    topology.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1
    assert ents[0].sn == entry_name_en_only
    topology.standalone.log.info("Found %s" % ents[0].dn)
    
    # filter: (&(cn=test_entry en only)(!(cn;fr=test_entry fr)))
    myfilter = '(&(sn=%s)(!(cn;fr=%s)))' % (entry_name_en_only, entry_name_fr)
    topology.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1
    assert ents[0].sn == entry_name_en_only
    topology.standalone.log.info("Found %s" % ents[0].dn)
    
    # filter: (&(cn=test_entry en only)(!(cn;en=test_entry en)))
    myfilter = '(&(sn=%s)(!(cn;en=%s)))' % (entry_name_en_only, entry_name_en)
    topology.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 0
    topology.standalone.log.info("Found none")
    
    topology.standalone.log.info("\n\n######################### DELETE ######################\n")
    
    topology.standalone.log.info("Try to delete  %s " % entry_dn_both)
    topology.standalone.delete_s(entry_dn_both)
    
    topology.standalone.log.info("Try to delete  %s " % entry_dn_en_only)
    topology.standalone.delete_s(entry_dn_en_only)

def test_ticket47313_final(topology):
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '0')]
    topology.standalone.modify_s(DN_CONFIG, mod)
    
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
    test_ticket47313_run(topo)
    
    test_ticket47313_final(topo)


if __name__ == '__main__':
    run_isolated()

