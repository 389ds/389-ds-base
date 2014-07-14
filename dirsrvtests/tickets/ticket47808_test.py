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

ATTRIBUTE_UNIQUENESS_PLUGIN = 'cn=attribute uniqueness,cn=plugins,cn=config'
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
    
    standalone = DirSrv(verbose=True)
    
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


def test_ticket47808_run(topology):
    """
        It enables attribute uniqueness plugin with sn as a unique attribute
        Add an entry 1 with sn = ENTRY_NAME
        Add an entry 2 with sn = ENTRY_NAME
        If the second add does not crash the server and the following search found none,
        the bug is fixed.
    """
        
    # bind as directory manager
    topology.standalone.log.info("Bind as %s" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    
    topology.standalone.log.info("\n\n######################### SETUP ATTR UNIQ PLUGIN ######################\n")

    # enable attribute uniqueness plugin
    mod = [(ldap.MOD_REPLACE, 'nsslapd-pluginEnabled', 'on'), (ldap.MOD_REPLACE, 'nsslapd-pluginarg0', 'sn'), (ldap.MOD_REPLACE, 'nsslapd-pluginarg1', SUFFIX)]
    topology.standalone.modify_s(ATTRIBUTE_UNIQUENESS_PLUGIN, mod)
    
    topology.standalone.log.info("\n\n######################### ADD USER 1 ######################\n")
    
    # Prepare entry 1
    entry_name = '%s 1' % (ENTRY_NAME)
    entry_dn_1 = 'cn=%s, %s' % (entry_name, SUFFIX)
    entry_1 = Entry(entry_dn_1)
    entry_1.setValues('objectclass', 'top', 'person')
    entry_1.setValues('sn', ENTRY_NAME)
    entry_1.setValues('cn', entry_name)
    topology.standalone.log.info("Try to add Add %s: %r" % (entry_1, entry_1))
    topology.standalone.add_s(entry_1)

    topology.standalone.log.info("\n\n######################### Restart Server ######################\n")
    topology.standalone.stop(timeout=10)
    topology.standalone.start(timeout=10)
    
    topology.standalone.log.info("\n\n######################### ADD USER 2 ######################\n")
    
    # Prepare entry 2 having the same sn, which crashes the server 
    entry_name = '%s 2' % (ENTRY_NAME)
    entry_dn_2 = 'cn=%s, %s' % (entry_name, SUFFIX)
    entry_2 = Entry(entry_dn_2)
    entry_2.setValues('objectclass', 'top', 'person')
    entry_2.setValues('sn', ENTRY_NAME)
    entry_2.setValues('cn', entry_name)
    topology.standalone.log.info("Try to add Add %s: %r" % (entry_2, entry_2))
    try:
      topology.standalone.add_s(entry_2)
    except:
      topology.standalone.log.warn("Adding %s failed" % entry_dn_2)
      pass

    topology.standalone.log.info("\n\n######################### IS SERVER UP? ######################\n")
    ents = topology.standalone.search_s(entry_dn_1, ldap.SCOPE_BASE, '(objectclass=*)')
    assert len(ents) == 1
    topology.standalone.log.info("Yes, it's up.")

    topology.standalone.log.info("\n\n######################### CHECK USER 2 NOT ADDED ######################\n")
    topology.standalone.log.info("Try to search %s" % entry_dn_2)
    try:
        ents = topology.standalone.search_s(entry_dn_2, ldap.SCOPE_BASE, '(objectclass=*)')
    except ldap.NO_SUCH_OBJECT:
        topology.standalone.log.info("Found none")
    
    topology.standalone.log.info("\n\n######################### DELETE USER 1 ######################\n")
    
    topology.standalone.log.info("Try to delete  %s " % entry_dn_1)
    topology.standalone.delete_s(entry_dn_1)
    
def test_ticket47808_final(topology):
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
    installation_prefix = None
        
    topo = topology(True)
    test_ticket47808_run(topo)
    
    test_ticket47808_final(topo)


if __name__ == '__main__':
    run_isolated()

