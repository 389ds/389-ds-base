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

m1_m2_agmt = ""

class TopologyReplication(object):
    def __init__(self, master1, master2):
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2


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
    master1.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_1)

    # Creating master 2...
    master2 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_MASTER_2
    args_instance[SER_PORT] = PORT_MASTER_2
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_2
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master2.allocate(args_master)
    instance_master2 = master2.exists()
    if instance_master2:
        master2.delete()
    master2.create()
    master2.open()
    master2.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_2)

    #
    # Create all the agreements
    #
    # Creating agreement from master 1 to master 2
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    global m1_m2_agmt
    m1_m2_agmt = master1.agreement.create(suffix=DEFAULT_SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=DEFAULT_SUFFIX, host=master1.host, port=master1.port, properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Initialize all the agreements
    #
    master1.agreement.init(DEFAULT_SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, master2)


def test_ticket47966(topology):
    '''
    Testing bulk import when the backend with VLV was recreated.
    If the test passes without the server crash, 47966 is verified.
    '''
    log.info('Testing Ticket 47966 - [VLV] slapd crashes during Dogtag clone reinstallation')
    M1 = topology.master1
    M2 = topology.master2

    log.info('0. Create a VLV index on Master 2.')
    # get the backend entry
    be = M2.replica.conn.backend.list(suffix=DEFAULT_SUFFIX)
    if not be:
        log.fatal("ticket47966: enable to retrieve the backend for %s" % DEFAULT_SUFFIX)
        raise ValueError("no backend for suffix %s" % DEFAULT_SUFFIX)
    bent = be[0]
    beName = bent.getValue('cn')
    beDn = "cn=%s,cn=ldbm database,cn=plugins,cn=config" % beName

    # generate vlvSearch entry
    vlvSrchDn = "cn=vlvSrch,%s" % beDn
    log.info('0-1. vlvSearch dn: %s' % vlvSrchDn)
    vlvSrchEntry = Entry(vlvSrchDn)
    vlvSrchEntry.setValues('objectclass', 'top', 'vlvSearch')
    vlvSrchEntry.setValues('cn', 'vlvSrch')
    vlvSrchEntry.setValues('vlvBase', DEFAULT_SUFFIX)
    vlvSrchEntry.setValues('vlvFilter', '(|(objectclass=*)(objectclass=ldapsubentry))')
    vlvSrchEntry.setValues('vlvScope', '2')
    M2.add_s(vlvSrchEntry)

    # generate vlvIndex entry
    vlvIndexDn = "cn=vlvIdx,%s" % vlvSrchDn
    log.info('0-2. vlvIndex dn: %s' % vlvIndexDn)
    vlvIndexEntry = Entry(vlvIndexDn)
    vlvIndexEntry.setValues('objectclass', 'top', 'vlvIndex')
    vlvIndexEntry.setValues('cn', 'vlvIdx')
    vlvIndexEntry.setValues('vlvSort', 'cn ou sn')
    M2.add_s(vlvIndexEntry)

    log.info('1. Initialize Master 2 from Master 1.')
    M1.agreement.init(DEFAULT_SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    M1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if M1.testReplication(DEFAULT_SUFFIX, M2):
        log.info('1-1. Replication is working.')
    else:
        log.fatal('1-1. Replication is not working.')
        assert False

    log.info('2. Delete the backend instance on Master 2.')
    M2.delete_s(vlvIndexDn)
    M2.delete_s(vlvSrchDn)
    # delete the agreement, replica, and mapping tree, too.
    M2.replica.disableReplication(DEFAULT_SUFFIX)
    mappingTree = 'cn="%s",cn=mapping tree,cn=config' % DEFAULT_SUFFIX
    M2.mappingtree.delete(DEFAULT_SUFFIX, beName, mappingTree)
    M2.backend.delete(DEFAULT_SUFFIX, beDn, beName)

    log.info('3. Recreate the backend and the VLV index on Master 2.')
    M2.mappingtree.create(DEFAULT_SUFFIX, beName)
    M2.backend.create(DEFAULT_SUFFIX, {BACKEND_NAME: beName})
    log.info('3-1. Recreating %s and %s on Master 2.' % (vlvSrchDn, vlvIndexDn))
    M2.add_s(vlvSrchEntry)
    M2.add_s(vlvIndexEntry)
    M2.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_2)
    # agreement m2_m1_agmt is not needed... :p

    log.info('4. Initialize Master 2 from Master 1 again.')
    M1.agreement.init(DEFAULT_SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    M1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if M1.testReplication(DEFAULT_SUFFIX, M2):
        log.info('4-1. Replication is working.')
    else:
        log.fatal('4-1. Replication is not working.')
        assert False

    log.info('5. Check Master 2 is up.')
    entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(cn=*)')
    assert len(entries) > 0
    log.info('5-1. %s entries are returned from M2.' % len(entries))

    log.info('Test complete')


def test_ticket47966_final(topology):
    topology.master1.delete()
    topology.master2.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_ticket47966(topo)
    test_ticket47966_final(topo)


if __name__ == '__main__':
    run_isolated()

