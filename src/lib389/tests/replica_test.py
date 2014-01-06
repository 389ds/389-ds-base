'''
Created on Jan 08, 2014

@author: tbordaz
'''


import ldap
import time
import sys
import os

from lib389 import InvalidArgumentError
from lib389._constants import *
from lib389.properties import *
from lib389 import DirSrv, Entry
from _constants import REPLICAROLE_CONSUMER

# Used for One master / One consumer topology
HOST_MASTER = LOCALHOST
PORT_MASTER = 40389
SERVERID_MASTER = 'master'
REPLICAID_MASTER = 1

HOST_CONSUMER = LOCALHOST
PORT_CONSUMER = 50389
SERVERID_CONSUMER = 'consumer'

TEST_REPL_DN = "uid=test,%s" % DEFAULT_SUFFIX
INSTANCE_PORT     = 54321
INSTANCE_SERVERID = 'dirsrv'
#INSTANCE_PREFIX   = os.environ.get('PREFIX', None)
INSTANCE_PREFIX = '/home/tbordaz/install'
INSTANCE_BACKUP = os.environ.get('BACKUPDIR', DEFAULT_BACKUPDIR)
NEW_SUFFIX_1    = 'ou=test_master'
NEW_BACKEND_1   = 'test_masterdb'
NEW_RM_1        = "cn=replrepl,%s" % NEW_SUFFIX_1

NEW_SUFFIX_2    = 'ou=test_consumer'
NEW_BACKEND_2   = 'test_consumerdb'

NEW_SUFFIX_3    = 'ou=test_enablereplication_1'
NEW_BACKEND_3   = 'test_enablereplicationdb_1'

NEW_SUFFIX_4    = 'ou=test_enablereplication_2'
NEW_BACKEND_4   = 'test_enablereplicationdb_2'

NEW_SUFFIX_5    = 'ou=test_enablereplication_3'
NEW_BACKEND_5   = 'test_enablereplicationdb_3'


class Test_replica():


    def setUp(self):
        # Create the master instance
        master = DirSrv(verbose=False)
        master.log.debug("Master allocated")
        args = {SER_HOST:          HOST_MASTER,
                SER_PORT:          PORT_MASTER,
                SER_DEPLOYED_DIR:  INSTANCE_PREFIX,
                SER_SERVERID_PROP: SERVERID_MASTER
                }
        master.allocate(args)
        if master.exists():
            master.delete()
        master.create()
        master.open()
        self.master = master
        
        # Create the consumer instance
        consumer = DirSrv(verbose=False)
        consumer.log.debug("Consumer allocated")
        args = {SER_HOST:          HOST_CONSUMER,
                SER_PORT:          PORT_CONSUMER,
                SER_DEPLOYED_DIR:  INSTANCE_PREFIX,
                SER_SERVERID_PROP: SERVERID_CONSUMER
                }
        consumer.allocate(args)
        if consumer.exists():
            consumer.delete()
        consumer.create()
        consumer.open()
        self.consumer = consumer


    def tearDown(self):
        self.master.log.info("\n\n#########################\n### TEARDOWN\n#########################")
        for instance in (self.master, self.consumer):
            if instance.exists():
                instance.delete()
                
    def test_create(self):
        self.master.log.info("\n\n#########################\n### CREATE\n#########################")
        '''
            This test creates
                - suffix/backend (NEW_SUFFIX_[12], NEW_BACKEND_[12]) : Master
                - suffix/backend (NEW_SUFFIX_[12], NEW_BACKEND_[12]) : Consumer
                - replica NEW_SUFFIX_1 as MASTER : Master
                - replica NEW_SUFFIX_2 as CONSUMER : Master
        '''
        #
        # MASTER (suffix/backend)
        #
        backendEntry = self.master.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
        backendEntry = self.master.backend.create(suffix=NEW_SUFFIX_2, properties={BACKEND_NAME: NEW_BACKEND_2})
        
        ents = self.master.mappingtree.list()
        master_nb_mappingtree = len(ents)
        
        # create a first additional mapping tree
        self.master.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
        ents = self.master.mappingtree.list()
        assert len(ents) == (master_nb_mappingtree + 1)
        self.master.add_s(Entry((NEW_SUFFIX_1, {'objectclass': "top organizationalunit".split(),
                                                'ou': NEW_SUFFIX_1.split('=',1)[1]})))
        
        
        # create a second additional mapping tree
        self.master.mappingtree.create(NEW_SUFFIX_2, bename=NEW_BACKEND_2)
        ents = self.master.mappingtree.list()
        assert len(ents) == (master_nb_mappingtree + 2)
        self.master.add_s(Entry((NEW_SUFFIX_2, {'objectclass': "top organizationalunit".split(),
                                                'ou': NEW_SUFFIX_2.split('=',1)[1]})))
        self.master.log.info('test_create): Master it exists now %d suffix(es)' % len(ents))
        
        #
        # CONSUMER (suffix/backend)
        #
        backendEntry = self.consumer.backend.create(suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
        backendEntry = self.consumer.backend.create(suffix=NEW_SUFFIX_2, properties={BACKEND_NAME: NEW_BACKEND_2})
        
        ents = self.consumer.mappingtree.list()
        consumer_nb_mappingtree = len(ents)
        
        # create a first additional mapping tree
        self.consumer.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
        ents = self.consumer.mappingtree.list()
        assert len(ents) == (consumer_nb_mappingtree + 1)
        self.consumer.add_s(Entry((NEW_SUFFIX_1, {'objectclass': "top organizationalunit".split(),
                                                'ou': NEW_SUFFIX_1.split('=',1)[1]})))
        
        # create a second additional mapping tree
        self.consumer.mappingtree.create(NEW_SUFFIX_2, bename=NEW_BACKEND_2)
        ents = self.consumer.mappingtree.list()
        assert len(ents) == (consumer_nb_mappingtree + 2)
        self.consumer.add_s(Entry((NEW_SUFFIX_2, {'objectclass': "top organizationalunit".split(),
                                                'ou': NEW_SUFFIX_2.split('=',1)[1]})))
        self.consumer.log.info('test_create): Consumer it exists now %d suffix(es)' % len(ents))
 
        #
        # Now create REPLICAS on master
        #
        # check it exists this entry to stores the changelogs
        self.master.changelog.create()
        
        # create a master
        self.master.replica.create(suffix=NEW_SUFFIX_1, role=REPLICAROLE_MASTER, rid=1)
        ents = self.master.replica.list()
        assert len(ents) == 1
        self.master.log.info('test_create): Master replica %s' % ents[0].dn)
        
        # create a consumer
        self.master.replica.create(suffix=NEW_SUFFIX_2, role=REPLICAROLE_CONSUMER)
        ents = self.master.replica.list()
        assert len(ents) == 2
        ents = self.master.replica.list(suffix=NEW_SUFFIX_2)
        self.master.log.info('test_create): Consumer replica %s' % ents[0].dn)
        
        #
        # Now create REPLICAS on consumer
        #
        # create a master
        self.consumer.replica.create(suffix=NEW_SUFFIX_1, role=REPLICAROLE_CONSUMER)
        ents = self.consumer.replica.list()
        assert len(ents) == 1
        self.consumer.log.info('test_create): Consumer replica %s' % ents[0].dn)
        
        # create a consumer
        self.consumer.replica.create(suffix=NEW_SUFFIX_2, role=REPLICAROLE_CONSUMER)
        ents = self.consumer.replica.list()
        assert len(ents) == 2
        ents = self.consumer.replica.list(suffix=NEW_SUFFIX_2)
        self.consumer.log.info('test_create): Consumer replica %s' % ents[0].dn)
        
    def test_list(self):
        self.master.log.info("\n\n#########################\n### LIST\n#########################")
        '''
            This test checks:
                - existing replicas can be retrieved
                - access to unknown replica does not fail
                
            PRE-CONDITION:
                It exists on MASTER two replicas NEW_SUFFIX_1 and NEW_SUFFIX_2
                created by test_create()
        '''
        ents = self.master.replica.list()
        assert len(ents) == 2
        
        # Check we can retrieve a replica with its suffix
        ents = self.master.replica.list(suffix=NEW_SUFFIX_1)
        assert len(ents) == 1
        replica_dn_1 = ents[0].dn
        
        # Check we can retrieve a replica with its suffix
        ents = self.master.replica.list(suffix=NEW_SUFFIX_2)
        assert len(ents) == 1
        replica_dn_2 = ents[0].dn
        
        # Check we can retrieve a replica with its DN
        ents = self.master.replica.list(replica_dn=replica_dn_1)
        assert len(ents) == 1
        assert replica_dn_1 == ents[0].dn
        

        # Check we can retrieve a replica if we provide DN and suffix
        ents = self.master.replica.list(suffix=NEW_SUFFIX_2, replica_dn=replica_dn_2)
        assert len(ents) == 1
        assert replica_dn_2 == ents[0].dn
        
        # Check DN is used before suffix name
        ents = self.master.replica.list(suffix=NEW_SUFFIX_2, replica_dn=replica_dn_1)
        assert len(ents) == 1
        assert replica_dn_1 == ents[0].dn
        
        # Check that invalid value does not break 
        ents = self.master.replica.list(suffix="X")
        for ent in ents:
            self.master.log.critical("Unexpected replica: %s" % ent.dn)
        assert len(ents) == 0

    def test_create_repl_manager(self):
        self.master.log.info("\n\n#########################\n### CREATE_REPL_MANAGER\n#########################")
        '''
            The tests are
                - create the default Replication manager/Password
                - create a specific Replication manager/ default Password
                - Check we can bind successfully
                - create a specific Replication manager / specific Password
                - Check we can bind successfully
        '''
        # First create the default replication manager
        self.consumer.replica.create_repl_manager()
        ents = self.consumer.search_s(defaultProperties[REPLICATION_BIND_DN], ldap.SCOPE_BASE, "objectclass=*")
        assert len(ents) == 1
        assert ents[0].dn == defaultProperties[REPLICATION_BIND_DN]
        
        # Second create a custom replication manager under NEW_SUFFIX_2
        rm_dn = "cn=replrepl,%s" % NEW_SUFFIX_2
        self.consumer.replica.create_repl_manager(repl_manager_dn=rm_dn)
        ents = self.consumer.search_s(rm_dn, ldap.SCOPE_BASE, "objectclass=*")
        assert len(ents) == 1
        assert ents[0].dn == rm_dn
        
        # Check we can bind
        self.consumer.simple_bind_s(rm_dn, defaultProperties[REPLICATION_BIND_PW])
        
        # Check we fail to bind
        try:
            self.consumer.simple_bind_s(rm_dn, "dummy")
        except Exception as e:
            self.consumer.log.info("Exception: %s" % type(e).__name__)
            assert isinstance(e, ldap.INVALID_CREDENTIALS)
            
            #now rebind
            self.consumer.simple_bind_s(self.consumer.binddn, self.consumer.bindpw)
            

        # Create a custom replication manager under NEW_SUFFIX_1 with a specified password
        rm_dn = NEW_RM_1
        self.consumer.replica.create_repl_manager(repl_manager_dn=rm_dn, repl_manager_pw="Secret123")
        ents = self.consumer.search_s(rm_dn, ldap.SCOPE_BASE, "objectclass=*")
        assert len(ents) == 1
        assert ents[0].dn == rm_dn

        # Check we can bind
        self.consumer.simple_bind_s(rm_dn, "Secret123")
        
        # Check we fail to bind
        try:
            self.consumer.simple_bind_s(rm_dn, "dummy")
        except Exception as e:
            self.consumer.log.info("Exception: %s" % type(e).__name__)
            assert isinstance(e, ldap.INVALID_CREDENTIALS)
            #now rebind
            self.consumer.simple_bind_s(self.consumer.binddn, self.consumer.bindpw)
            
    def test_enableReplication(self):
        self.master.log.info("\n\n#########################\n### ENABLEREPLICATION\n#########################")
        '''
            It checks
                - Ability to enable replication on a supplier
                - Ability to enable replication on a consumer
                - Failure to enable replication with wrong replicaID on supplier
                - Failure to enable replication with wrong replicaID on consumer 
        '''
        #
        # MASTER (suffix/backend)
        #
        backendEntry = self.master.backend.create(suffix=NEW_SUFFIX_3, properties={BACKEND_NAME: NEW_BACKEND_3})
        
        ents = self.master.mappingtree.list()
        master_nb_mappingtree = len(ents)
        
        # create a first additional mapping tree
        self.master.mappingtree.create(NEW_SUFFIX_3, bename=NEW_BACKEND_3)
        ents = self.master.mappingtree.list()
        assert len(ents) == (master_nb_mappingtree + 1)
        self.master.add_s(Entry((NEW_SUFFIX_3, {'objectclass': "top organizationalunit".split(),
                                                'ou': NEW_SUFFIX_3.split('=',1)[1]})))
        
        try:
            # a supplier should have replicaId in [1..CONSUMER_REPLICAID[
            self.master.replica.enableReplication(suffix=NEW_SUFFIX_3, role=REPLICAROLE_MASTER, replicaId=CONSUMER_REPLICAID)
        except Exception as e:
            self.master.log.info("Exception (expected): %s" % type(e).__name__)
            assert isinstance(e, ValueError)
        self.master.replica.enableReplication(suffix=NEW_SUFFIX_3, role=REPLICAROLE_MASTER, replicaId=1)

        #
        # MASTER (suffix/backend)
        #
        backendEntry = self.master.backend.create(suffix=NEW_SUFFIX_4, properties={BACKEND_NAME: NEW_BACKEND_4})
        
        ents = self.master.mappingtree.list()
        master_nb_mappingtree = len(ents)
        
        # create a first additional mapping tree
        self.master.mappingtree.create(NEW_SUFFIX_4, bename=NEW_BACKEND_4)
        ents = self.master.mappingtree.list()
        assert len(ents) == (master_nb_mappingtree + 1)
        self.master.add_s(Entry((NEW_SUFFIX_4, {'objectclass': "top organizationalunit".split(),
                                                'ou': NEW_SUFFIX_4.split('=',1)[1]})))
        try:
            # A consumer should have CONSUMER_REPLICAID not '1'
            self.master.replica.enableReplication(suffix=NEW_SUFFIX_4, role=REPLICAROLE_CONSUMER, replicaId=1)
        except Exception as e:
            self.master.log.info("Exception (expected): %s" % type(e).__name__)
            assert isinstance(e, ValueError)
        self.master.replica.enableReplication(suffix=NEW_SUFFIX_4, role=REPLICAROLE_CONSUMER)
        
    def test_disableReplication(self):
        self.master.log.info("\n\n#########################\n### DISABLEREPLICATION\n#########################")
        '''
            Currently not implemented 
        '''
        try:
            self.master.replica.disableReplication(suffix=NEW_SUFFIX_4)
        except Exception as e:
            self.master.log.info("Exception (expected): %s" % type(e).__name__)
            assert isinstance(e, NotImplementedError)
        
    def test_setProperties(self):
        self.master.log.info("\n\n#########################\n### SETPROPERTIES\n#########################")
        '''
            Set some properties
            Verified that valid properties are set
            Verified that invalid properties raise an Exception
            
            PRE-REQUISITE: it exists a replica for NEW_SUFFIX_1
        '''
        
        # set valid values to SUFFIX_1
        properties = {REPLICA_LEGACY_CONS:      'off',
                      REPLICA_BINDDN:           NEW_RM_1,
                      REPLICA_PURGE_INTERVAL:   str(3600),
                      REPLICA_PURGE_DELAY:      str(5*24*3600),
                      REPLICA_REFERRAL:         "ldap://%s:1234/" % LOCALHOST}
        self.master.replica.setProperties(suffix=NEW_SUFFIX_1, properties=properties)
        
        # Check the values have been written
        replicas = self.master.replica.list(suffix=NEW_SUFFIX_1)
        assert len(replicas) == 1
        for prop in properties:
            attr = REPLICA_PROPNAME_TO_ATTRNAME[prop]
            val = replicas[0].getValue(attr)
            self.master.log.info("Replica[%s] -> %s: %s" % (prop, attr, val))
            assert val == properties[prop]
            
        # Check invalid properties raise exception
        try:
            properties = {"dummy": 'dummy'}
            self.master.replica.setProperties(suffix=NEW_SUFFIX_1, properties=properties)
        except Exception as e:
            self.master.log.info("Exception (expected): %s" % type(e).__name__)
            assert isinstance(e, ValueError)
            
        # check call without suffix/dn/entry raise InvalidArgumentError
        try:
            properties = {REPLICA_LEGACY_CONS:      'off'}
            self.master.replica.setProperties(properties=properties)
        except Exception as e:
            self.master.log.info("Exception (expected): %s" % type(e).__name__)
            assert isinstance(e, InvalidArgumentError)
            
        # check that if we do not provide a valid entry it raises ValueError
        try:
            properties = {REPLICA_LEGACY_CONS:      'off'}
            self.master.replica.setProperties(replica_entry="dummy", properties=properties)
        except Exception as e:
            self.master.log.info("Exception (expected): %s" % type(e).__name__)
            assert isinstance(e, ValueError)
            
        # check that with an invalid suffix or replica_dn it raise ValueError
        try:
            properties = {REPLICA_LEGACY_CONS:      'off'}
            self.master.replica.setProperties(suffix="dummy", properties=properties)
        except Exception as e:
            self.master.log.info("Exception (expected): %s" % type(e).__name__)
            assert isinstance(e, ValueError)
            
    def test_getProperties(self):
        self.master.log.info("\n\n#########################\n### GETPROPERTIES\n#########################")
        '''
            Currently not implemented 
        '''
        try:
            properties = {REPLICA_LEGACY_CONS:      'off'}
            self.master.replica.setProperties(suffix=NEW_SUFFIX_1, properties=properties)
        except Exception as e:
            self.master.log.info("Exception (expected): %s" % type(e).__name__)
            assert isinstance(e, NotImplementedError)
            
    
    def test_delete(self):
        self.master.log.info("\n\n#########################\n### DELETE\n#########################")
        '''
            Currently not implemented 
        '''
        try:
            self.master.replica.delete(suffix=NEW_SUFFIX_4)
        except Exception as e:
            self.master.log.info("Exception (expected): %s" % type(e).__name__)
            assert isinstance(e, NotImplementedError)
        
            
if __name__ == "__main__":
    test = Test_replica()
    test.setUp()

    #time.sleep(30)
    test.test_create()
    test.test_list()
    test.test_create_repl_manager()
    test.test_enableReplication()
    test.test_disableReplication()
    test.test_setProperties()
    test.test_getProperties()
    test.test_delete()
    
    test.tearDown()



# 
# 
# conn = DirSrv()
# def changelog():
#     changelog_e = conn.replica.changelog(dbname='foo')
#     assert changelog_e.data['nsslapd-changelogdir'].endswith('foo')
# 
# 
# def changelog_default_test():
#     e = conn.replica.changelog()
#     conn.added_entries.append(e.dn)
#     assert e.dn, "Bad changelog entry: %r " % e
#     assert e.getValue('nsslapd-changelogdir').endswith("changelogdb"), "Mismatching entry %r " % e.data.get('nsslapd-changelogdir')
#     conn.delete_s("cn=changelog5,cn=config")
# 
# 
# def changelog_customdb_test():
#     e = conn.replica.changelog(dbname="mockChangelogDb")
#     conn.added_entries.append(e.dn)
#     assert e.dn, "Bad changelog entry: %r " % e
#     assert e.getValue('nsslapd-changelogdir').endswith("mockChangelogDb"), "Mismatching entry %r " % e.data.get('nsslapd-changelogdir')
#     conn.delete_s("cn=changelog5,cn=config")
# 
# 
# def changelog_full_path_test():
#     e = conn.replica.changelog(dbname="/tmp/mockChangelogDb")
#     conn.added_entries.append(e.dn)
# 
#     assert e.dn, "Bad changelog entry: %r " % e
#     expect(e, 'nsslapd-changelogdir', "/tmp/mockChangelogDb")
#     conn.delete_s("cn=changelog5,cn=config")
# 
# def ruv_test():
#     ruv = conn.replica.ruv(suffix='o=testReplica')
#     assert ruv, "Missing RUV"
#     assert len(ruv.rid), "Missing RID"
#     assert int(MOCK_REPLICA_ID) in ruv.rid.keys()
# 
# 
# @raises(ldap.NO_SUCH_OBJECT)
# def ruv_missing_test():
#     ruv = conn.replica.ruv(suffix='o=MISSING')
#     assert ruv, "Missing RUV"
#     assert len(ruv.rid), "Missing RID"
#     assert int(MOCK_REPLICA_ID) in ruv.rid.keys()
# 
# 
# def start_test():
#     raise NotImplementedError()
# 
# 
# def stop_test():
#     raise NotImplementedError()
# 
# 
# def restart_test():
#     raise NotImplementedError()
# 
# 
# def start_async_test():
#     raise NotImplementedError()
# 
# 
# def wait_for_init_test():
#     raise NotImplementedError()
# 
# 
# def setup_agreement_default_test():
#     user = {
#         'binddn': DN_RMANAGER,
#         'bindpw': "password"
#     }
#     params = {'consumer': MockDirSrv(), 'suffix': "o=testReplica"}
#     params.update(user)
# 
#     agreement_dn = conn.replica.agreement_add(**params)
#     conn.added_entries.append(agreement_dn)
# 
# @raises(ldap.ALREADY_EXISTS)
# def setup_agreement_duplicate_test():
#     user = {
#         'binddn': DN_RMANAGER,
#         'bindpw': "password"
#     }
#     params = {
#         'consumer': MockDirSrv(),
#         'suffix': "o=testReplica",
#         'cn_format': 'testAgreement',
#         'description_format': 'testAgreement'
#     }
#     params.update(user)
#     conn.replica.agreement_add(**params)
# 
# 
# def setup_agreement_test():
#     user = {
#         'binddn': DN_RMANAGER,
#         'bindpw': "password"
#     }
#     params = {'consumer': MockDirSrv(), 'suffix': "o=testReplica"}
#     params.update(user)
# 
#     conn.replica.agreement_add(**params)
#     # timeout=120, auto_init=False, bindmethod='simple', starttls=False, args=None):
#     raise NotImplementedError()
# 
# def setup_agreement_fractional_test():
#     # TODO: fractiona replicates only a subset of attributes 
#     # 
#     user = {
#         'binddn': DN_RMANAGER,
#         'bindpw': "password"
#     }
#     params = {'consumer': MockDirSrv(), 'suffix': "o=testReplica"}
#     params.update(user)
# 
#     #conn.replica.agreement_add(**params)
#     #cn_format=r'meTo_%s:%s', description_format=r'me to %s:%s', timeout=120, auto_init=False, bindmethod='simple', starttls=False, args=None):
#     raise NotImplementedError()
# 
# 
# def find_agreements_test():
#     agreements = conn.replica.agreements(dn=False)
#     assert any(['testagreement' in x.dn.lower(
#     ) for x in agreements]), "Missing agreement"
# 
# 
# def find_agreements_dn_test():
#     agreements_dn = conn.replica.agreements()
#     assert any(['testagreement' in x.lower(
#     ) for x in agreements_dn]), "Missing agreement"
# 
# 
# def setup_replica_test():
#     args = {
#         'suffix': "o=testReplicaCreation",
#         'binddn': DN_RMANAGER,
#         'bindpw': "password",
#         'rtype': lib389.MASTER_TYPE,
#         'rid': MOCK_REPLICA_ID
#     }
#     # create a replica entry
#     replica_e = conn.replica.add(**args)
#     assert 'dn' in replica_e, "Missing dn in replica"
#     conn.added_entries.append(replica_e['dn'])
# 
# 
# def setup_replica_hub_test():
#     args = {
#         'suffix': "o=testReplicaCreation",
#         'binddn': DN_RMANAGER,
#         'bindpw': "password",
#         'rtype': lib389.HUB_TYPE,
#         'rid': MOCK_REPLICA_ID
#     }
#     # create a replica entry
#     replica_e = conn.replica.add(**args)
#     assert 'dn' in replica_e, "Missing dn in replica"
#     conn.added_entries.append(replica_e['dn'])
# 
# 
# def setup_replica_referrals_test():
#     #tombstone_purgedelay=None, purgedelay=None, referrals=None, legacy=False
#     raise NotImplementedError()
# 
# 
# def setup_all_replica_test():
#     raise NotImplementedError()
# 
# def replica_keep_in_sync_test():
#     raise NotImplementedError()
