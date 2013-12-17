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

class Test_Backend(object):


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
        ents = self.instance.backend.list()
        nb_backend = len(ents)
        for ent in ents:
            self.instance.log.info("List(%d): backend %s" % (nb_backend, ent.dn))
            
        # Create a first backend and check list all backends
        backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
        ents = self.instance.backend.list()
        for ent in ents:
            self.instance.log.info("List(%d): backend %s" % (nb_backend + 1, ent.dn))
        assert len(ents) == (nb_backend + 1)
        
        # Create a second backend and check list all backends
        backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_2, properties={BACKEND_NAME: NEW_BACKEND_2})
        ents = self.instance.backend.list()
        for ent in ents:
            self.instance.log.info("List(%d): backend %s" % (nb_backend + 2, ent.dn))
        assert len(ents) == (nb_backend + 2)
     
        # Check list a backend per suffix
        ents = self.instance.backend.list(suffix=NEW_SUFFIX_1)
        for ent in ents:
            self.instance.log.info("List suffix (%d): backend %s" % (1, ent.dn))
        assert len(ents) == 1
 
        # Check list a backend by its name
        ents = self.instance.backend.list(bename=NEW_BACKEND_2)
        for ent in ents:
            self.instance.log.info("List name (%d): backend %s" % (1, ent.dn))
        assert len(ents) == 1
         
        # Check list backends by their DN
        all = self.instance.backend.list()
        for ent in all:
            ents = self.instance.backend.list(backend_dn=ent.dn)
            for bck in ents:
                self.instance.log.info("List DN (%d): backend %s" % (1, bck.dn))
            assert len(ents) == 1
             
        # Check list with valid backend DN but invalid suffix/bename
        all = self.instance.backend.list()
        for ent in all:
            ents = self.instance.backend.list(suffix="o=dummy", backend_dn=ent.dn, bename="dummydb")
            for bck in ents:
                self.instance.log.info("List invalid suffix+bename (%d): backend %s" % (1, bck.dn))
            assert len(ents) == 1
    
    def test_create(self):
        # just to make it clean before starting
        self.instance.backend.delete(suffix=NEW_SUFFIX_1)
        self.instance.backend.delete(suffix=NEW_SUFFIX_2)
         
        # create a backend
        backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
         
        # check missing suffix
        try:
            backendEntry = self.instance.backend.create()
            assert backendEntry == None
        except Exception as e:
            self.instance.log.info('Fail to create (expected)%s: %s' % (type(e).__name__,e.args))
            pass
         
        # check already existing backend for that suffix
        try:
            backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_1)
            assert backendEntry == None
        except Exception as e:
            self.instance.log.info('Fail to create (expected)%s: %s' % (type(e).__name__, e.args))
            pass
        
        # check already existing backend DN, create for a different suffix but same backend name
        try:
            backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_2, properties={BACKEND_NAME: NEW_BACKEND_1})
            assert backendEntry == None
        except Exception as e:
            self.instance.log.info('Fail to create (expected)%s: %s' % (type(e).__name__, e.args))
            pass
        
        # create a backend without properties
        backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_2)
        ents = self.instance.backend.list()
        for ent in ents:
            self.instance.log.info("List: backend %s" % (ent.dn))
            
            
    def test_delete(self):
        # just to make it clean before starting
        self.instance.backend.delete(suffix=NEW_SUFFIX_1)
        self.instance.backend.delete(suffix=NEW_SUFFIX_2)
        ents = self.instance.backend.list()
        nb_backend = len(ents)
        
        # Check the various possibility to delete a backend
        # First with suffix
        backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
        self.instance.backend.delete(suffix=NEW_SUFFIX_1)
        ents = self.instance.backend.list()
        assert len(ents) == nb_backend
        
        # Second with backend name
        backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
        self.instance.backend.delete(bename=NEW_BACKEND_1)
        ents = self.instance.backend.list()
        assert len(ents) == nb_backend
        
        # Third with backend DN
        backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
        ents = self.instance.backend.list(suffix=NEW_SUFFIX_1)
        assert len(ents) == 1
        self.instance.backend.delete(backend_dn=ents[0].dn)
        ents = self.instance.backend.list()
        assert len(ents) == nb_backend
        
        # Now check the failure cases
        # First no argument -> InvalidArgumentError
        try:
            backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
            self.instance.backend.delete()
        except Exception as e:
            self.instance.log.info('Fail to delete (expected)%s: %s' % (type(e).__name__,e.args))
            self.instance.backend.delete(suffix=NEW_SUFFIX_1)
            pass
        
        # second invalid suffix -> InvalidArgumentError
        try:
            self.instance.backend.delete(suffix=NEW_SUFFIX_2)
        except Exception as e:
            self.instance.log.info('Fail to delete (expected)%s: %s' % (type(e).__name__,e.args))
            pass
        
        # existing a mapping tree -> UnwillingToPerformError
        backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
        self.instance.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
        try:
            self.instance.backend.delete(suffix=NEW_SUFFIX_1)
        except Exception as e:
            self.instance.log.info('Fail to delete (expected)%s: %s' % (type(e).__name__,e.args))
            self.instance.mappingtree.delete(suffix=NEW_SUFFIX_1,bename=NEW_BACKEND_1)
            self.instance.backend.delete(suffix=NEW_SUFFIX_1)
            
        # backend name differs -> UnwillingToPerformError
        backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
        try:
            self.instance.backend.delete(suffix=NEW_SUFFIX_1, bename='dummydb')
        except Exception as e:
            self.instance.log.info('Fail to delete (expected)%s: %s' % (type(e).__name__,e.args))
            self.instance.backend.delete(suffix=NEW_SUFFIX_1)
        
    def test_toSuffix(self):
        backendEntry = self.instance.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
        self.instance.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
        
        ents = self.instance.backend.list()
        for ent in ents:
            suffix = ent.getValues(BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX])
            values = self.instance.backend.toSuffix(name=ent.dn)
            if not suffix[0] in values:
                self.instance.log.info("Fail we do not retrieve the suffix %s in %s" % (suffix[0], values))
            else:
                self.instance.log.info("%s retrieved" % suffix)
            
        
if __name__ == "__main__":
    test = Test_Backend()
    test.setUp()
    
    test.test_list()
    test.test_create()
    test.test_delete()
    test.test_toSuffix()
    
    test.tearDown()
    
