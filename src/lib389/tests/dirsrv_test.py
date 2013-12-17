'''
Created on Dec 9, 2013

@author: tbordaz
'''

import os
import pwd
import ldap
from random import randint
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389 import DirSrv,Entry

TEST_REPL_DN = "uid=test,%s" % DEFAULT_SUFFIX
INSTANCE_PORT     = 54321
INSTANCE_SERVERID = 'dirsrv'
#INSTANCE_PREFIX   = os.environ.get('PREFIX', None)
INSTANCE_PREFIX   = None
INSTANCE_BACKUP   = os.environ.get('BACKUPDIR', DEFAULT_BACKUPDIR)

class Test_dirsrv():
    def _add_user(self, success=True):
        try:
            self.instance.add_s(Entry((TEST_REPL_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                      'uid': 'test',
                                                      'sn': 'test',
                                                      'cn': 'test'})))
        except Exception as e:
            if success:
                raise
            else:
                self.instance.log.info('Fail to add (expected): %s' % e.args)
                return
            
        self.instance.log.info('%s added' % TEST_REPL_DN)

    def _mod_user(self, success=True):
        try:
            replace = [(ldap.MOD_REPLACE, 'description', str(randint(1, 100)))]
            self.instance.modify_s(TEST_REPL_DN, replace)
        except Exception as e:
            if success:
                raise
            else:
                self.instance.log.info('Fail to modify (expected): %s' % e.args)
                return
            
        self.instance.log.info('%s modified' % TEST_REPL_DN)

    def setUp(self):
        pass


    def tearDown(self):
        pass


    def test_allocate(self, verbose=False):
        instance = DirSrv(verbose=verbose)
        instance.log.debug("Instance allocated")
        assert instance.state == DIRSRV_STATE_INIT

        # Check that SER_SERVERID_PROP is a mandatory parameter
        args = {SER_HOST:         LOCALHOST,
                SER_PORT:         INSTANCE_PORT,
                SER_DEPLOYED_DIR: INSTANCE_PREFIX
                }
        try:
            instance.allocate(args)
        except Exception as e:
            instance.log.info('Allocate fails (normal): %s' % e.args)
            assert type(e) == ValueError
            assert e.args[0].find("%s is a mandatory parameter" % SER_SERVERID_PROP) >= 0
            pass
        
        # Check the state
        assert instance.state == DIRSRV_STATE_INIT
        
        # Now do a successful allocate
        args[SER_SERVERID_PROP] = INSTANCE_SERVERID
        instance.allocate(args)
        
        userid = pwd.getpwuid( os.getuid() )[ 0 ]

        
        # Now verify the settings 
        assert instance.state     == DIRSRV_STATE_ALLOCATED
        assert instance.host      == LOCALHOST
        assert instance.port      == INSTANCE_PORT
        assert instance.sslport   == None
        assert instance.binddn    == DN_DM
        assert instance.bindpw    == PW_DM
        assert instance.creation_suffix == DEFAULT_SUFFIX
        assert instance.userid    == userid
        assert instance.serverid  == INSTANCE_SERVERID
        assert instance.groupid   == instance.userid
        assert instance.prefix    == INSTANCE_PREFIX
        assert instance.backupdir == INSTANCE_BACKUP
        
        # Now check we can change the settings of an allocated DirSrv
        args = {SER_SERVERID_PROP:INSTANCE_SERVERID,
                SER_HOST:         LOCALHOST,
                SER_PORT:         INSTANCE_PORT,
                SER_DEPLOYED_DIR: INSTANCE_PREFIX,
                SER_ROOT_DN: "uid=foo"}
        instance.allocate(args)
        assert instance.state     == DIRSRV_STATE_ALLOCATED
        assert instance.host      == LOCALHOST
        assert instance.port      == INSTANCE_PORT
        assert instance.sslport   == None
        assert instance.binddn    == "uid=foo"
        assert instance.bindpw    == PW_DM
        assert instance.creation_suffix == DEFAULT_SUFFIX
        assert instance.userid    == userid
        assert instance.serverid  == INSTANCE_SERVERID
        assert instance.groupid   == instance.userid
        assert instance.prefix    == INSTANCE_PREFIX
        assert instance.backupdir == INSTANCE_BACKUP
        
        # OK restore back the valid parameters
        args = {SER_SERVERID_PROP:INSTANCE_SERVERID,
                SER_HOST:         LOCALHOST,
                SER_PORT:         INSTANCE_PORT,
                SER_DEPLOYED_DIR: INSTANCE_PREFIX}
        instance.allocate(args)
        assert instance.state     == DIRSRV_STATE_ALLOCATED
        assert instance.host      == LOCALHOST
        assert instance.port      == INSTANCE_PORT
        assert instance.sslport   == None
        assert instance.binddn    == DN_DM
        assert instance.bindpw    == PW_DM
        assert instance.creation_suffix == DEFAULT_SUFFIX
        assert instance.userid    == userid
        assert instance.serverid  == INSTANCE_SERVERID
        assert instance.groupid   == instance.userid
        assert instance.prefix    == INSTANCE_PREFIX
        assert instance.backupdir == INSTANCE_BACKUP
        
        self.instance = instance
        
    def test_list_init(self):
        '''
            Lists the instances on the file system
        '''
        for properties in self.instance.list():
            self.instance.log.info("properties: %r" % properties)
            
        for properties in self.instance.list(all=True):
            self.instance.log.info("properties (all): %r" % properties)
        
    def test_allocated_to_offline(self):
        self.instance.create()

        
    def test_offline_to_online(self):
        self.instance.open()
        
    def test_online_to_offline(self):
        self.instance.close()
        
    def test_offline_to_allocated(self):
        self.instance.delete()
        

if __name__ == "__main__":
    #import sys;sys.argv = ['', 'Test.testName']
    test = Test_dirsrv()
    test.setUp()
    
    # Allocated the instance, except preparing the instance 
    test.test_allocate(False)
    
    
    if test.instance.exists():
        # force a cleanup before starting
        test.instance.state = DIRSRV_STATE_OFFLINE
        test.test_offline_to_allocated()
        

    # Do a listing of the instances
    test.test_list_init()
    test._add_user(success=False)
    
    # Create the instance
    test.test_allocated_to_offline()
    test._add_user(success=False)
    
    # bind to the instance
    test.test_offline_to_online()
    test._add_user(success=True)
    
    test._mod_user(success=True)
    
    #Unbind to the instance
    test.test_online_to_offline()
    test._mod_user(success=False)
    
    test.test_list_init()
    
    test.test_offline_to_allocated()
    
    test.tearDown()


