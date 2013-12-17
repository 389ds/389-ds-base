'''
Created on Dec 17, 2013

@author: tbordaz
'''
import os
import pwd
import ldap
from random import randint
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389 import DirSrv,Entry, InvalidArgumentError


TEST_REPL_DN = "uid=test,%s" % DEFAULT_SUFFIX
INSTANCE_PORT     = 54321
INSTANCE_SERVERID = 'dirsrv'
#INSTANCE_PREFIX   = os.environ.get('PREFIX', None)
INSTANCE_PREFIX   = None
INSTANCE_BACKUP   = os.environ.get('BACKUPDIR', DEFAULT_BACKUPDIR)
NEW_SUFFIX_1       = 'o=test_create'
NEW_BACKEND_1      = 'test_createdb'
NEW_CHILDSUFFIX_1  = 'o=child1,o=test_create'
NEW_CHILDBACKEND_1 = 'test_createchilddb'
NEW_SUFFIX_2       = 'o=test_bis_create'
NEW_BACKEND_2      = 'test_bis_createdb'
NEW_CHILDSUFFIX_2  = 'o=child2,o=test_bis_create'
NEW_CHILDBACKEND_2 = 'test_bis_createchilddb'


class Test_suffix():


    def setUp(self):
        instance = DirSrv(verbose=False)
        instance.log.debug("Instance allocated")
        args = {SER_HOST:          LOCALHOST,
                SER_PORT:          INSTANCE_PORT,
                SER_DEPLOYED_DIR:  INSTANCE_PREFIX,
                SER_SERVERID_PROP: INSTANCE_SERVERID
                }
        instance.allocate(args)
        if instance.exists():
            instance.delete()
        instance.create()
        instance.open()
        self.instance = instance


    def tearDown(self):
        if self.instance.exists():
            self.instance.delete()


    def test_list(self):
        # before creating mapping trees, the backends must exists
        backendEntry = self.instance.backend.create(NEW_SUFFIX_1, {BACKEND_NAME: NEW_BACKEND_1})
        
        ents = self.instance.mappingtree.list()
        nb_mappingtree = len(ents)
        
        # create a first additional mapping tree
        self.instance.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree + 1)
        
        suffixes = self.instance.suffix.list()
        assert len(suffixes) == 2
        for suffix in suffixes:
            self.instance.log.info("suffix is %s" % suffix)
    
    def test_toBackend(self):
        backends = self.instance.suffix.toBackend(suffix=NEW_SUFFIX_1)
        assert len(backends) == 1
        backend = backends[0]
        self.instance.log.info("backend entry is %r" % backend)
        assert backend.getValue(BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX]) == NEW_SUFFIX_1
        assert backend.getValue(BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME]) == NEW_BACKEND_1

        
    def test_getParent(self):
        parent = self.instance.suffix.getParent(suffix=NEW_SUFFIX_1)
        assert parent == None
        
        backendEntry = self.instance.backend.create(NEW_CHILDSUFFIX_1, {BACKEND_NAME: NEW_CHILDBACKEND_1})
        # create a third additional mapping tree, that is child of NEW_SUFFIX_1
        self.instance.mappingtree.create(NEW_CHILDSUFFIX_1, bename=NEW_CHILDBACKEND_1, parent=NEW_SUFFIX_1)
        parent = self.instance.suffix.getParent(suffix=NEW_CHILDSUFFIX_1)
        self.instance.log.info("Retrieved parent of %s:  %s" % (NEW_CHILDSUFFIX_1, parent))
        assert parent == NEW_SUFFIX_1
        

if __name__ == "__main__":
    test = Test_suffix()
    test.setUp()
    
    test.test_list()
    test.test_toBackend()
    test.test_getParent()

    
    test.tearDown()

