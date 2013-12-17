'''
Created on Dec 13, 2013

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

TEST_REPL_DN       = "uid=test,%s" % DEFAULT_SUFFIX
INSTANCE_PORT      = 54321
INSTANCE_SERVERID  = 'dirsrv'
#INSTANCE_PREFIX   = os.environ.get('PREFIX', None)
INSTANCE_PREFIX    = None
INSTANCE_BACKUP    = os.environ.get('BACKUPDIR', DEFAULT_BACKUPDIR)
NEW_SUFFIX_1       = 'o=test_create'
NEW_BACKEND_1      = 'test_createdb'
NEW_CHILDSUFFIX_1  = 'o=child1,o=test_create'
NEW_CHILDBACKEND_1 = 'test_createchilddb'
NEW_SUFFIX_2       = 'o=test_bis_create'
NEW_BACKEND_2      = 'test_bis_createdb'
NEW_CHILDSUFFIX_2  = 'o=child2,o=test_bis_create'
NEW_CHILDBACKEND_2 = 'test_bis_createchilddb'

class Test_mappingTree():
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
        '''
            This test list with only no param, suffix, bename, suffix+bename (also with invalid values)
        '''
        ents = self.instance.mappingtree.list()
        for ent in ents:
                self.instance.log.info('test_list: %r' % ent)
                
        # get with a dedicated suffix
        ents = self.instance.mappingtree.list(suffix=DEFAULT_SUFFIX)
        assert len(ents) == 1
        self.instance.log.info('test_list(suffix): %r' % ent)
        
        ents = self.instance.mappingtree.list(suffix="dc=dummy")
        assert len(ents) == 0
        
        # get with a dedicated backend name
        ents = self.instance.mappingtree.list(bename=DEFAULT_BENAME)
        assert len(ents) == 1
        self.instance.log.info('test_list(bename): %r' % ent)
        
        ents = self.instance.mappingtree.list(bename="dummy")
        assert len(ents) == 0
        
        # check backend is taken first
        ents = self.instance.mappingtree.list(suffix="dc=dummy", bename=DEFAULT_BENAME)
        assert len(ents) == 1
        self.instance.log.info('test_list(suffix, bename): %r' % ent)
        
    def test_create(self):
        '''
            This test will create 2 backends/mapping trees, then 2 childs backend/mapping tree
        '''

        # before creating mapping trees, the backends must exists
        backendEntry = self.instance.backend.create(NEW_SUFFIX_1, {BACKEND_NAME: NEW_BACKEND_1})
        backendEntry = self.instance.backend.create(NEW_SUFFIX_2, {BACKEND_NAME: NEW_BACKEND_2})
        
        ents = self.instance.mappingtree.list()
        nb_mappingtree = len(ents)
        
        # create a first additional mapping tree
        self.instance.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree + 1)
        
        # create a second additional mapping tree
        self.instance.mappingtree.create(NEW_SUFFIX_2, bename=NEW_BACKEND_2)
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree + 2)
        
        # Creating a mapping tree that already exists => it just returns the existing MT
        self.instance.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree + 2)
        
         # Creating a mapping tree that already exists => it just returns the existing MT
        self.instance.mappingtree.create(NEW_SUFFIX_2, bename=NEW_BACKEND_2)
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree + 2)
        
        # before creating mapping trees, the backends must exists
        backendEntry = self.instance.backend.create(NEW_CHILDSUFFIX_1, {BACKEND_NAME: NEW_CHILDBACKEND_1})
        backendEntry = self.instance.backend.create(NEW_CHILDSUFFIX_2, {BACKEND_NAME: NEW_CHILDBACKEND_2})
        
        # create a third additional mapping tree, that is child of NEW_SUFFIX_1
        self.instance.mappingtree.create(NEW_CHILDSUFFIX_1, bename=NEW_CHILDBACKEND_1, parent=NEW_SUFFIX_1)
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree + 3)
        
        ents = self.instance.mappingtree.list(suffix=NEW_CHILDSUFFIX_1)
        assert len(ents) == 1
        ent = ents[0]
        assert ent.hasAttr(MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX]) and (ent.getValue(MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX]) == NEW_SUFFIX_1)
        
        # create a fourth additional mapping tree, that is child of NEW_SUFFIX_2
        self.instance.mappingtree.create(NEW_CHILDSUFFIX_2, bename=NEW_CHILDBACKEND_2, parent=NEW_SUFFIX_2)
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree + 4)
        
        ents = self.instance.mappingtree.list(suffix=NEW_CHILDSUFFIX_2)
        assert len(ents) == 1
        ent = ents[0]
        assert ent.hasAttr(MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX]) and (ent.getValue(MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX]) == NEW_SUFFIX_2)
        
        
    def test_delete(self):
        '''
            Delete the mapping tree and check the remaining number.
            Delete the sub-suffix first.
        '''
        ents = self.instance.mappingtree.list()
        nb_mappingtree = len(ents)
        deleted = 0
        
        self.instance.log.debug("delete MT for suffix " + NEW_CHILDSUFFIX_1)
        self.instance.mappingtree.delete(suffix=NEW_CHILDSUFFIX_1)
        deleted += 1
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree - deleted)
        
        self.instance.log.debug("delete MT with backend " + NEW_CHILDBACKEND_2)
        self.instance.mappingtree.delete(bename=NEW_CHILDBACKEND_2)
        deleted += 1
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree - deleted)
        
        self.instance.log.debug("delete MT for suffix %s and with backend %s" % (NEW_SUFFIX_1, NEW_BACKEND_1))
        self.instance.mappingtree.delete(suffix=NEW_SUFFIX_1,bename=NEW_BACKEND_1)
        deleted += 1
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree - deleted)
        

        ents = self.instance.mappingtree.list(suffix=NEW_SUFFIX_2)
        assert len(ents) == 1
        self.instance.log.debug("delete MT with DN %s (dummy suffix/backend)" % (ents[0].dn))
        self.instance.mappingtree.delete(suffix="o=dummy", bename="foo", name=ents[0].dn)
        
    def test_getProperties(self):
        ents = self.instance.mappingtree.list()
        nb_mappingtree = len(ents)
        
        # create a first additional mapping tree
        self.instance.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
        ents = self.instance.mappingtree.list()
        assert len(ents) == (nb_mappingtree + 1)
        
        # check we can get properties from suffix
        prop_ref = self.instance.mappingtree.getProperties(suffix=NEW_SUFFIX_1)
        self.instance.log.info("properties [suffix] %s: %r" % (NEW_SUFFIX_1, prop_ref))
        
        # check we can get properties from backend name
        properties = self.instance.mappingtree.getProperties(bename=NEW_BACKEND_1)
        for key in properties.keys():
            assert prop_ref[key] == properties[key]
        self.instance.log.info("properties [backend] %s: %r" % (NEW_SUFFIX_1, properties))
        
        # check we can get properties from suffix AND backend name
        properties = self.instance.mappingtree.getProperties(suffix=NEW_SUFFIX_1, bename=NEW_BACKEND_1)
        for key in properties.keys():
            assert prop_ref[key] == properties[key]
        self.instance.log.info("properties [suffix+backend]%s: %r" % (NEW_SUFFIX_1, properties))
        
        # check we can get properties from MT entry
        ents = self.instance.mappingtree.list(suffix=NEW_SUFFIX_1)
        assert len(ents) == 1
        ent = ents[0]
        properties = self.instance.mappingtree.getProperties(name=ent.dn)
        for key in properties.keys():
            assert prop_ref[key] == properties[key]
        self.instance.log.info("properties [MT entry DN] %s: %r" % (NEW_SUFFIX_1, properties))
        
        # check we can get only one properties
        properties = self.instance.mappingtree.getProperties(name=ent.dn,properties=[MT_STATE])
        assert len(properties) == 1
        assert properties[MT_STATE] == prop_ref[MT_STATE]
        
        try:
            properties = self.instance.mappingtree.getProperties(name=ent.dn,properties=['dummy'])
            assert 0  # it should return an exception
        except ValueError:
            pass
        except KeyError:
            pass
        
    def toSuffix(self):
        ents = self.instance.mappingtree.list()
        suffix = self.instance.mappingtree.toSuffix(entry=ents[0])
        assert len(suffix) > 0
        self.instance.log.info("suffix (entry) is %s" % suffix[0])
        
        suffix = self.instance.mappingtree.toSuffix(name=ents[0].dn)
        assert len(suffix) > 0
        self.instance.log.info("suffix (dn) is %s" % suffix[0])
        
        try:
            suffix = self.instance.mappingtree.toSuffix()
        except InvalidArgumentError:
            pass
        
if __name__ == "__main__":
    #import sys;sys.argv = ['', 'Test.testName']
    test = Test_mappingTree()
    test.setUp()
    
    test.test_list()
    test.test_create()
    test.test_delete()
    test.test_getProperties()
    test.toSuffix()
    
    test.tearDown()


