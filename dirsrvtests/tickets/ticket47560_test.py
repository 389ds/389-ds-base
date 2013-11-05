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
from constants import *

log = logging.getLogger(__name__)

installation_prefix = None

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

class TopologyStandalone(object):
    def __init__(self, standalone):
        self.standalone = _ds_rebind_instance(standalone)


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
                create or rebind to standalone

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
        args_instance['prefix'] = installation_prefix
    
    # Args for the standalone instance
    args_instance['newhost'] = HOST_STANDALONE
    args_instance['newport'] = PORT_STANDALONE
    args_instance['newinstance'] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    
    # Get the status of the backups
    backup_standalone   = DirSrvTools.existsBackup(args_standalone)
    
    # Get the status of the instance and restart it if it exists
    instance_standalone   = DirSrvTools.existsInstance(args_standalone)
    if instance_standalone:
        # assuming the instance is already stopped, just wait 5 sec max
        DirSrvTools.stop(instance_standalone, timeout=5)
        DirSrvTools.start(instance_standalone, timeout=10)
    
    if backup_standalone:
        # The backup exist, assuming it is correct 
        # we just re-init the instance with it
        standalone   = _ds_create_instance(args_standalone)
        
        # restore standalone instance from backup
        DirSrvTools.stop(standalone, timeout=10)
        DirSrvTools.instanceRestoreFS(standalone, backup_standalone)
        DirSrvTools.start(standalone, timeout=10)
        
    else:
        # We should be here only in two conditions
        #      - This is the first time a test involve standalone instance
        #      - Something weird happened (instance/backup destroyed)
        #        so we discard everything and recreate all
        
        # Remove the backup. So even if we have a specific backup file
        # (e.g backup_standalone) we clear backup that an instance may have created
        if backup_standalone:
            DirSrvTools.clearInstanceBackupFS(dirsrv=instance_standalone)
        
        # Remove the instance
        if instance_standalone:
            DirSrvTools.removeInstance(instance_standalone)
            
        # Create the instance
        standalone   = _ds_create_instance(args_standalone)
                
        # Time to create the backups
        DirSrvTools.stop(standalone, timeout=10)
        standalone.backupfile = DirSrvTools.instanceBackupFS(standalone)
        DirSrvTools.start(standalone, timeout=10)
    
    # 
    # Here we have standalone instance up and running
    # Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyStandalone(standalone)


def test_ticket47560(topology):
    """
       This test case does the following:
          SETUP
            - Create entry cn=group,SUFFIX
            - Create entry cn=member,SUFFIX
            - Update 'cn=member,SUFFIX' to add "memberOf: cn=group,SUFFIX"
            - Enable Memberof Plugins
                
            # Here the cn=member entry has a 'memberOf' but 
            # cn=group entry does not contain 'cn=member' in its member
                
          TEST CASE
            - start the fixupmemberof task
            - read the cn=member entry
            - check 'memberOf is now empty
                
           TEARDOWN
            - Delete entry cn=group,SUFFIX
            - Delete entry cn=member,SUFFIX
            - Disable Memberof Plugins
    """
        
    def _enable_disable_mbo(value):
        """
            Enable or disable mbo plugin depending on 'value' ('on'/'off')
        """
        # enable/disable the mbo plugin
        if value != 'on':
           value = 'off'
        log.debug("-------------> _enable_disable_mbo(%s)" % value)
        MEMBEROF_PLUGIN_DN = 'cn=MemberOf Plugin,cn=plugins,cn=config'
        replace = [(ldap.MOD_REPLACE, 'nsslapd-pluginEnabled', value)]
        topology.standalone.modify_s(MEMBEROF_PLUGIN_DN, replace)
        DirSrvTools.stop(topology.standalone, verbose=False, timeout=120)
        time.sleep(1)
        DirSrvTools.start(topology.standalone, verbose=False, timeout=120)
        time.sleep(3)
            
        # need to reopen a connection toward the instance
        topology.standalone = _ds_rebind_instance(topology.standalone)
        
    def _test_ticket47560_setup():
        """
        - Create entry cn=group,SUFFIX
        - Create entry cn=member,SUFFIX
        - Update 'cn=member,SUFFIX' to add "memberOf: cn=group,SUFFIX"
        - Enable Memberof Plugins
        """
        log.debug( "-------- > _test_ticket47560_setup\n")
            
        #
        # By default the memberof plugin is disabled create 
        # - create a group entry
        # - create a member entry
        # - set the member entry as memberof the group entry
        #
        entry = Entry(group_DN)
        entry.setValues('objectclass', 'top', 'groupOfNames', 'inetUser')
        entry.setValues('cn', 'group')
        try:
            topology.standalone.add_s(entry)
        except ldap.ALREADY_EXISTS:
            log.debug( "Entry %s already exists" % (group_DN))
        

        entry = Entry(member_DN)
        entry.setValues('objectclass', 'top', 'person', 'organizationalPerson', 'inetorgperson', 'inetUser')
        entry.setValues('uid', 'member')
        entry.setValues('cn', 'member')
        entry.setValues('sn', 'member')
        try:
            topology.standalone.add_s(entry)
        except ldap.ALREADY_EXISTS:
            log.debug( "Entry %s already exists" % (member_DN))
            
        replace = [(ldap.MOD_REPLACE, 'memberof', group_DN)]
        topology.standalone.modify_s(member_DN, replace)
    
 
        #
        # enable the memberof plugin and restart the instance
        #
        _enable_disable_mbo('on')
           
              
        #
        # check memberof attribute is still present
        #
        filt = 'uid=member'
        ents = topology.standalone.search_s(member_DN, ldap.SCOPE_BASE, filt)
        assert len(ents) == 1
        ent = ents[0]
        #print ent
        value = ent.getValue('memberof')
        #print "memberof: %s" % (value)
        assert value == group_DN 
            
    def _test_ticket47560_teardown():
        """
            - Delete entry cn=group,SUFFIX
            - Delete entry cn=member,SUFFIX
            - Disable Memberof Plugins
        """
        log.debug( "-------- > _test_ticket47560_teardown\n")
        # remove the entries group_DN and member_DN
        try:
            topology.standalone.delete_s(group_DN)
        except:
            log.warning("Entry %s fail to delete" % (group_DN))
        try:
            topology.standalone.delete_s(member_DN)
        except:
            log.warning("Entry %s fail to delete" % (member_DN))
        #
        # disable the memberof plugin and restart the instance
        #
        _enable_disable_mbo('off')

        
    
    group_DN  = "cn=group,%s"   % (SUFFIX)
    member_DN = "uid=member,%s" % (SUFFIX)
        
    #
    # Initialize the test case
    #
    _test_ticket47560_setup()
        
    # 
    # start the test
    #   - start the fixup task
    #   - check the entry is fixed (no longer memberof the group)
    #
    log.debug( "-------- > Start ticket tests\n")
        
    filt = 'uid=member'
    ents = topology.standalone.search_s(member_DN, ldap.SCOPE_BASE, filt)
    assert len(ents) == 1
    ent = ents[0]
    log.debug( "Unfixed entry %r\n" % ent)
        
    # run the fixup task
    topology.standalone.fixupMemberOf(SUFFIX, verbose=False)
        
    ents = topology.standalone.search_s(member_DN, ldap.SCOPE_BASE, filt)
    assert len(ents) == 1
    ent = ents[0]
    log.debug( "Fixed entry %r\n" % ent)
        
    if ent.getValue('memberof') == group_DN:
        log.warning("Error the fixupMemberOf did not fix %s" % (member_DN))
        result_successful = False
    else:
        result_successful = True
                  
    #
    # cleanup up the test case
    #
    _test_ticket47560_teardown() 
        
    assert result_successful == True

def test_ticket47560_final(topology):
    DirSrvTools.stop(topology.standalone, timeout=10)
    


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
    test_ticket47560(topo)
    test_ticket47560_final(topo)


if __name__ == '__main__':
    run_isolated()

