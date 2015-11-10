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

installation1_prefix = None


class TopologyReplication(object):
    def __init__(self, master1, hub1, consumer1):
        master1.open()
        self.master1 = master1
        hub1.open()
        self.hub1 = hub1
        consumer1.open()
        self.consumer1 = consumer1


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

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
    master1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER,
                                      replicaId=REPLICAID_MASTER_1)

    # Creating hub 1...
    hub1 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_HUB_1
    args_instance[SER_PORT] = PORT_HUB_1
    args_instance[SER_SERVERID_PROP] = SERVERID_HUB_1
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_hub = args_instance.copy()
    hub1.allocate(args_hub)
    instance_hub1 = hub1.exists()
    if instance_hub1:
        hub1.delete()
    hub1.create()
    hub1.open()
    hub1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_HUB,
                                   replicaId=REPLICAID_HUB_1)

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
    consumer1.changelog.create()
    consumer1.replica.enableReplication(suffix=SUFFIX,
                                        role=REPLICAROLE_CONSUMER,
                                        replicaId=CONSUMER_REPLICAID)

    #
    # Create all the agreements
    #
    # Creating agreement from master 1 to hub 1
    properties = {RA_NAME: r'meTo_$host:$port',
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_h1_agmt = master1.agreement.create(suffix=SUFFIX, host=hub1.host,
                                          port=hub1.port,
                                          properties=properties)
    if not m1_h1_agmt:
        log.fatal("Fail to create a master -> hub replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_h1_agmt)

    # Creating agreement from hub 1 to consumer 1
    properties = {RA_NAME: r'meTo_$host:$port',
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    h1_c1_agmt = hub1.agreement.create(suffix=SUFFIX, host=consumer1.host,
                                       port=consumer1.port,
                                       properties=properties)
    if not h1_c1_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.debug("%s created" % h1_c1_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Initialize all the agreements
    #
    master1.agreement.init(SUFFIX, HOST_HUB_1, PORT_HUB_1)
    master1.waitForReplInit(m1_h1_agmt)
    hub1.agreement.init(SUFFIX, HOST_CONSUMER_1, PORT_CONSUMER_1)
    hub1.waitForReplInit(h1_c1_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, consumer1):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Delete each instance in the end
    def fin():
        master1.delete()
        hub1.delete()
        consumer1.delete()
        pass

    request.addfinalizer(fin)

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, hub1, consumer1)


def checkFirstElement(ds, rid):
    """
    Return True if the first RUV element is for the specified rid
    """
    try:
        entry = ds.search_s(DEFAULT_SUFFIX,
                            ldap.SCOPE_SUBTREE,
                            REPLICA_RUV_FILTER,
                            ['nsds50ruv'])
        assert entry
        entry = entry[0]
    except ldap.LDAPError as e:
        log.fatal('Failed to retrieve RUV entry: %s' % str(e))
        assert False

    ruv_elements = entry.getValues('nsds50ruv')
    if ('replica %s ' % rid) in ruv_elements[1]:
        return True
    else:
        return False


def test_ticket48325(topology):
    """
    Test that the RUV element order is correctly maintained when promoting
    a hub or consumer.
    """

    #
    # Promote consumer to master
    #
    try:
        DN = topology.consumer1.replica._get_mt_entry(DEFAULT_SUFFIX)
        topology.consumer1.modify_s(DN, [(ldap.MOD_REPLACE,
                                          'nsDS5ReplicaType',
                                          '3'),
                                         (ldap.MOD_REPLACE,
                                          'nsDS5ReplicaID',
                                          '1234'),
                                         (ldap.MOD_REPLACE,
                                          'nsDS5Flags',
                                          '1')])
    except ldap.LDAPError as e:
        log.fatal('Failed to promote consuemr to master: error %s' % str(e))
        assert False
    time.sleep(1)

    #
    # Check ruv has been reordered
    #
    if not checkFirstElement(topology.consumer1, '1234'):
        log.fatal('RUV was not reordered')
        assert False

    #
    # Create repl agreement from the newly promoted master to master1
    #
    properties = {RA_NAME: r'meTo_$host:$port',
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    new_agmt = topology.consumer1.agreement.create(suffix=SUFFIX,
                                                   host=topology.master1.host,
                                                   port=topology.master1.port,
                                                   properties=properties)

    if not new_agmt:
        log.fatal("Fail to create new agmt from old consumer to the master")
        assert False

    #
    # Test replication is working
    #
    if topology.consumer1.testReplication(DEFAULT_SUFFIX, topology.master1):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    #
    # Promote hub to master
    #
    try:
        DN = topology.hub1.replica._get_mt_entry(DEFAULT_SUFFIX)
        topology.hub1.modify_s(DN, [(ldap.MOD_REPLACE,
                                     'nsDS5ReplicaType',
                                     '3'),
                                    (ldap.MOD_REPLACE,
                                     'nsDS5ReplicaID',
                                     '5678')])
    except ldap.LDAPError as e:
        log.fatal('Failed to promote consuemr to master: error %s' % str(e))
        assert False
    time.sleep(1)

    #
    # Check ruv has been reordered
    #
    if not checkFirstElement(topology.hub1, '5678'):
        log.fatal('RUV was not reordered')
        assert False

    #
    # Test replication is working
    #
    if topology.hub1.testReplication(DEFAULT_SUFFIX, topology.master1):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Done
    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)