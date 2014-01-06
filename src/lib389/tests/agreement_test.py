'''
Created on Jan 9, 2014

@author: tbordaz
'''

import ldap
import time
import sys
import os

from lib389 import InvalidArgumentError, NoSuchEntryError
from lib389.agreement import Agreement
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

SUFFIX = DEFAULT_SUFFIX
ENTRY_DN = "cn=test_entry, %s" % SUFFIX



class Test_Agreement():


    def setUp(self):
        #
        # Master
        #
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
        
        # enable replication
        master.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER)
        self.master = master
        
        
        #
        # Consumer
        #
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
        
        # enable replication
        consumer.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_CONSUMER)
        self.consumer = consumer


    def tearDown(self):
        self.master.log.info("\n\n#########################\n### TEARDOWN\n#########################\n")
        for instance in (self.master, self.consumer):
            if instance.exists():
                instance.delete()
        
    def test_create(self):
        '''
            Test to create a replica agreement and initialize the consumer.
            Test on a unknown suffix
        '''
        self.master.log.info("\n\n#########################\n### CREATE\n#########################\n")
        properties = {RA_NAME:      r'meTo_$host:$port',
                      RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                      RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                      RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                      RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
        repl_agreement = self.master.agreement.create(suffix=SUFFIX, host=self.consumer.host, port=self.consumer.port, properties=properties)
        self.master.log.debug("%s created" % repl_agreement)
        self.master.agreement.init(SUFFIX, HOST_CONSUMER, PORT_CONSUMER)
        self.master.waitForReplInit(repl_agreement)
        
        # Add a test entry
        self.master.add_s(Entry((ENTRY_DN, {'objectclass': "top person".split(),
                                            'sn': 'test_entry',
                                            'cn': 'test_entry'})))
        
        # check replication is working
        loop = 0
        while loop <= 10:
            try:
                ent = self.consumer.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
                break
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                loop += 1
        assert loop <= 10
        
        # check that with an invalid suffix it raises NoSuchEntryError
        try:
            properties = {RA_NAME:      r'meAgainTo_$host:$port'}
            self.master.agreement.create(suffix="ou=dummy", host=self.consumer.host, port=self.consumer.port, properties=properties)
        except Exception as e:
            self.master.log.info("Exception (expected): %s" % type(e).__name__)
            assert isinstance(e, NoSuchEntryError)
        
    def test_list(self):
        '''
            List the replica agreement on a suffix => 1
            Add a RA
            List the replica agreements on that suffix again => 2
            List a specific RA
            
            PREREQUISITE: it exists a replica for SUFFIX and a replica agreement
        '''
        self.master.log.info("\n\n#########################\n### LIST\n#########################\n")
        ents = self.master.agreement.list(suffix=SUFFIX)
        assert len(ents) == 1
        assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_HOST]) == self.consumer.host
        assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_PORT]) == str(self.consumer.port)
        
        # Create a second RA to check .list returns 2 RA
        properties = {RA_NAME:      r'meTo_$host:$port',
                      RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                      RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                      RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                      RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
        self.master.agreement.create(suffix=SUFFIX, host=self.consumer.host, port=12345, properties=properties)
        ents = self.master.agreement.list(suffix=SUFFIX)
        assert len(ents) == 2
        
        # Check we can .list a specific RA
        ents = self.master.agreement.list(suffix=SUFFIX, consumer_host=self.consumer.host, consumer_port=self.consumer.port)
        assert len(ents) == 1
        assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_HOST]) == self.consumer.host
        assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_PORT]) == str(self.consumer.port)
        
                
    def test_status(self):
        self.master.log.info("\n\n#########################\n### STATUS\n#########################")
        ents = self.master.agreement.list(suffix=SUFFIX)
        for ent in ents:
            self.master.log.info("Status of %s: %s" % (ent.dn, self.master.agreement.status(ent.dn)))
            
    def test_schedule(self):
        
        self.master.log.info("\n\n#########################\n### SCHEDULE\n#########################")
        ents = self.master.agreement.list(suffix=SUFFIX, consumer_host=self.consumer.host, consumer_port=self.consumer.port)
        assert len(ents) == 1
        
        self.master.agreement.schedule(ents[0].dn, Agreement.ALWAYS)
        ents = self.master.agreement.list(suffix=SUFFIX, consumer_host=self.consumer.host, consumer_port=self.consumer.port)
        assert len(ents) == 1
        assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_SCHEDULE]) == Agreement.ALWAYS
        
        self.master.agreement.schedule(ents[0].dn, Agreement.NEVER)
        ents = self.master.agreement.list(suffix=SUFFIX, consumer_host=self.consumer.host, consumer_port=self.consumer.port)
        assert len(ents) == 1
        assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_SCHEDULE]) == Agreement.NEVER
        
        CUSTOM_SCHEDULE="0000-1234 6420"
        self.master.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)
        ents = self.master.agreement.list(suffix=SUFFIX, consumer_host=self.consumer.host, consumer_port=self.consumer.port)
        assert len(ents) == 1
        assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_SCHEDULE]) == CUSTOM_SCHEDULE
        
        # check that with an invalid HOUR schedule raise ValueError
        try:
            CUSTOM_SCHEDULE="2500-1234 6420"
            self.master.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)
        except Exception as e:
            self.master.log.info("Exception (expected) HOUR: %s" % type(e).__name__)
            assert isinstance(e, ValueError)
        
        try:
            CUSTOM_SCHEDULE="0000-2534 6420"
            self.master.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)
        except Exception as e:
            self.master.log.info("Exception (expected) HOUR: %s" % type(e).__name__)
            assert isinstance(e, ValueError)
        
        # check that with an starting HOUR after ending HOUR raise ValueError
        try:
            CUSTOM_SCHEDULE="1300-1234 6420"
            self.master.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)
        except Exception as e:
            self.master.log.info("Exception (expected) HOUR: %s" % type(e).__name__)
            assert isinstance(e, ValueError)
        
        # check that with an invalid MIN schedule raise ValueError
        try:
            CUSTOM_SCHEDULE="0062-1234 6420"
            self.master.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)
        except Exception as e:
            self.master.log.info("Exception (expected) MIN: %s" % type(e).__name__)
            assert isinstance(e, ValueError)
        
        try:
            CUSTOM_SCHEDULE="0000-1362 6420"
            self.master.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)
        except Exception as e:
            self.master.log.info("Exception (expected) MIN: %s" % type(e).__name__)
            assert isinstance(e, ValueError)
            
        # check that with an invalid DAYS schedule raise ValueError
        try:
            CUSTOM_SCHEDULE="0000-1234 6-420"
            self.master.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)
        except Exception as e:
            self.master.log.info("Exception (expected) MIN: %s" % type(e).__name__)
            assert isinstance(e, ValueError)
        
        try:
            CUSTOM_SCHEDULE="0000-1362 64209"
            self.master.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)
        except Exception as e:
            self.master.log.info("Exception (expected) MIN: %s" % type(e).__name__)
            assert isinstance(e, ValueError)
            
        try:
            CUSTOM_SCHEDULE="0000-1362 01234560"
            self.master.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)
        except Exception as e:
            self.master.log.info("Exception (expected) MIN: %s" % type(e).__name__)
            assert isinstance(e, ValueError)
            
    def test_getProperties(self):
        self.master.log.info("\n\n#########################\n### GETPROPERTIES\n#########################")
        ents = self.master.agreement.list(suffix=SUFFIX, consumer_host=self.consumer.host, consumer_port=self.consumer.port)
        assert len(ents) == 1
        properties = self.master.agreement.getProperties(agmnt_dn=ents[0].dn)
        for prop in properties:
            self.master.log.info("RA %s : %s -> %s" % (prop, RA_PROPNAME_TO_ATTRNAME[prop], properties[prop]))
            
        properties = self.master.agreement.getProperties(agmnt_dn=ents[0].dn, properties=[RA_BINDDN])
        assert len(properties) == 1
        for prop in properties:
            self.master.log.info("RA %s : %s -> %s" % (prop, RA_PROPNAME_TO_ATTRNAME[prop], properties[prop]))
            
    def test_setProperties(self):
        self.master.log.info("\n\n#########################\n### SETPROPERTIES\n#########################")
        ents = self.master.agreement.list(suffix=SUFFIX, consumer_host=self.consumer.host, consumer_port=self.consumer.port)
        assert len(ents) == 1
        test_schedule   = "1234-2345 12345"
        test_desc       = "hello world !"
        self.master.agreement.setProperties(agmnt_dn=ents[0].dn, properties={RA_SCHEDULE: test_schedule, RA_DESCRIPTION: test_desc})
        properties = self.master.agreement.getProperties(agmnt_dn=ents[0].dn, properties=[RA_SCHEDULE, RA_DESCRIPTION])
        assert len(properties) == 2
        assert properties[RA_SCHEDULE][0]      == test_schedule
        assert properties[RA_DESCRIPTION][0]   == test_desc
        
    def test_changes(self):
        self.master.log.info("\n\n#########################\n### CHANGES\n#########################")
        ents = self.master.agreement.list(suffix=SUFFIX, consumer_host=self.consumer.host, consumer_port=self.consumer.port)
        assert len(ents) == 1
        value = self.master.agreement.changes(agmnt_dn=ents[0].dn)
        self.master.log.info("\ntest_changes: %d changes\n" % value)
        assert value > 0
        
        # do an update
        TEST_STRING = 'hello you'
        mod = [(ldap.MOD_REPLACE, 'description', [TEST_STRING])]
        self.master.modify_s(ENTRY_DN, mod)
        
        # the update has been replicated
        loop = 0
        while loop <= 10:
                ent = self.consumer.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
                if ent and ent.hasValue('description'):
                    if ent.getValue('description') == TEST_STRING:
                        break
                time.sleep(1)
                loop += 1
        assert loop <= 10
        
        # check change number
        newvalue = self.master.agreement.changes(agmnt_dn=ents[0].dn)
        self.master.log.info("\ntest_changes: %d changes\n" % newvalue)
        assert (value + 1) == newvalue
        
if __name__ == "__main__":
    test = Test_Agreement()
    test.setUp()
    
    test.test_create()
    test.test_list()
    test.test_status()
    test.test_schedule()
    test.test_getProperties()
    test.test_setProperties()
    test.test_changes()
    
    test.tearDown()