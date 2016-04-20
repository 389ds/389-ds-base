import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

class TopologyReplication(object):
    def __init__(self, master1, consumer1):
        master1.open()
        self.master1 = master1
        consumer1.open()
        self.consumer1 = consumer1


@pytest.fixture(scope="module")
def topology(request):
    # Creating master 1...
    master1 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_MASTER_1
    args_instance[SER_PORT] = PORT_MASTER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_1
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master1.allocate(args_master)
    instance_master1 = master1.exists()
    if instance_master1:
        master1.delete()
    master1.create()
    master1.open()
    master1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_1)

    # Creating consumer 1...
    consumer1 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_CONSUMER_1
    args_instance[SER_PORT] = PORT_CONSUMER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_CONSUMER_1
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_consumer = args_instance.copy()
    consumer1.allocate(args_consumer)
    instance_consumer1 = consumer1.exists()
    if instance_consumer1:
        consumer1.delete()
    consumer1.create()
    consumer1.open()
    consumer1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_CONSUMER, replicaId=CONSUMER_REPLICAID)

    #
    # Create all the agreements
    #
    # Creating agreement from master 1 to consumer 1
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_c1_agmt = master1.agreement.create(suffix=SUFFIX, host=consumer1.host, port=consumer1.port, properties=properties)
    if not m1_c1_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_c1_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Initialize all the agreements
    #
    master1.agreement.init(SUFFIX, HOST_CONSUMER_1, PORT_CONSUMER_1)
    master1.waitForReplInit(m1_c1_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, consumer1):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Delete each instance in the end
    def fin():
        master1.delete()
        consumer1.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, consumer1)

def _add_custom_schema(server):
    attr_value = "( 10.0.9.2342.19200300.100.1.1 NAME 'customManager' EQUALITY distinguishedNameMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.12 X-ORIGIN 'user defined' )"
    mod = [(ldap.MOD_ADD, 'attributeTypes', attr_value)]
    server.modify_s('cn=schema', mod)

    oc_value = "( 1.3.6.1.4.1.4843.2.1 NAME 'customPerson' SUP inetorgperson STRUCTURAL MAY (customManager) X-ORIGIN 'user defined' )"
    mod = [(ldap.MOD_ADD, 'objectclasses', oc_value)]
    server.modify_s('cn=schema', mod)

def _create_user(server):
    server.add_s(Entry((
        "uid=testuser,ou=People,%s" % DEFAULT_SUFFIX,
        {
            'objectClass' : "top account posixaccount".split(),
            'uid' : 'testuser',
            'gecos' : 'Test User',
            'cn' : 'testuser',
            'homedirectory' : '/home/testuser',
            'passwordexpirationtime' : '20160710184141Z',
            'userpassword' : '!',
            'uidnumber' : '1111212',
            'gidnumber' : '1111212',
            'loginshell' : '/bin/bash'
        }
    )))

def _modify_user(server):
    mod = [
        (ldap.MOD_ADD, 'objectClass', ['customPerson']),
        (ldap.MOD_ADD, 'sn', ['User']),
        (ldap.MOD_ADD, 'customManager', ['cn=manager']),
    ]
    server.modify("uid=testuser,ou=People,%s" % DEFAULT_SUFFIX, mod)

def test_ticket48799(topology):
    """Write your replication testcase here.

    To access each DirSrv instance use:  topology.master1, topology.master2,
        ..., topology.hub1, ..., topology.consumer1,...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).
    """

    # Add the new schema element.
    _add_custom_schema(topology.master1)
    _add_custom_schema(topology.consumer1)

    # Add a new user on the master.
    _create_user(topology.master1)
    # Modify the user on the master.
    _modify_user(topology.master1)

    # We need to wait for replication here.
    time.sleep(15)

    # Now compare the master vs consumer, and see if the objectClass was dropped.

    master_entry = topology.master1.search_s("uid=testuser,ou=People,%s" % DEFAULT_SUFFIX, ldap.SCOPE_BASE, '(objectclass=*)', ['objectClass'])
    consumer_entry = topology.consumer1.search_s("uid=testuser,ou=People,%s" % DEFAULT_SUFFIX, ldap.SCOPE_BASE, '(objectclass=*)', ['objectClass'])

    assert(master_entry == consumer_entry)


    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
