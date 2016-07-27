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
from collections import Counter

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

WAITFOR_ASYNC_ATTR = "nsDS5ReplicaWaitForAsyncResults"


class TopologyReplication(object):
    def __init__(self, master1, master2, m1_m2_agmt, m2_m1_agmt):
        master1.open()
        master2.open()
        self.masters = ((master1, m1_m2_agmt),
                        (master2, m2_m1_agmt))


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
    master1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_1)

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
    master2.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_2)

    #
    # Create all the agreements
    #
    # Creating agreement from master 1 to master 2
    properties = {RA_NAME:      'meTo_%s:%s' %(master2.host, master2.port),
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME:      'meTo_%s:%s' %(master1.host, master1.port),
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Initialize all the agreements
    #
    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)
    master2.agreement.init(SUFFIX, HOST_MASTER_1, PORT_MASTER_1)
    master2.waitForReplInit(m2_m1_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    log.info("Set Replication Debugging loglevel for the errorlog")
    master1.setLogLevel(LOG_REPLICA)
    master2.setLogLevel(LOG_REPLICA)

    logging_attr = 'nsslapd-logging-hr-timestamps-enabled'
    master1.modify_s("cn=config", [(ldap.MOD_REPLACE, logging_attr, "off")])
    master2.modify_s("cn=config", [(ldap.MOD_REPLACE, logging_attr, "off")])

    # Delete each instance in the end
    def fin():
        master1.delete()
        master2.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, master2, m1_m2_agmt, m2_m1_agmt)


@pytest.fixture(params=[(None, (4, 10)),
                        ('2000', (0, 1)),
                        ('0', (4, 10)),
                        ('-5', (4, 10))])
def waitfor_async_attr(topology, request):
    """Sets attribute on all replicas"""

    attr_value = request.param[0]
    expected_result = request.param[1]

    # Run through all masters
    for master in topology.masters:
        agmt = master[1]
        try:
            if attr_value:
                log.info("Set %s: %s on %s" % (
                        WAITFOR_ASYNC_ATTR, attr_value, master[0].serverid))
                mod = [(ldap.MOD_REPLACE, WAITFOR_ASYNC_ATTR, attr_value)]
            else:
                log.info("Delete %s from %s" % (
                        WAITFOR_ASYNC_ATTR, master[0].serverid))
                mod = [(ldap.MOD_DELETE, WAITFOR_ASYNC_ATTR, None)]
            master[0].modify_s(agmt, mod)
        except ldap.LDAPError as e:
            log.error('Failed to set or delete %s attribute: (%s)' % (
                   WAITFOR_ASYNC_ATTR, e.message['desc']))

    return (attr_value, expected_result)


@pytest.fixture
def entries(topology, request):
    """Adds entries to the master1"""

    master1 = topology.masters[0][0]

    TEST_OU = "test"
    test_dn = SUFFIX
    test_list = []

    log.info("Add 100 nested entries under replicated suffix on %s" % master1.serverid)
    for i in xrange(100):
        test_dn = 'ou=%s%s,%s' % (TEST_OU, i, test_dn)
        test_list.insert(0, test_dn)
        try:
            master1.add_s(Entry((test_dn,
                                 {'objectclass': 'top',
                                  'objectclass': 'organizationalUnit',
                                  'ou': TEST_OU})))
        except ldap.LDAPError as e:
            log.error('Failed to add entry (%s): error (%s)' % (test_dn,
                                                               e.message['desc']))
            assert False

    log.info("Delete created entries")
    for test_dn in test_list:
        try:
            master1.delete_s(test_dn)
        except ldap.LDAPError, e:
            log.error('Failed to delete entry (%s): error (%s)' % (test_dn,
                                                                   e.message['desc']))
            assert False

    def fin():
        log.info("Clear the errors log in the end of the test case")
        with open(master1.errlog, 'w') as errlog:
            errlog.writelines("")
    request.addfinalizer(fin)


def test_not_int_value(topology):
    """Tests not integer value"""

    master1 = topology.masters[0][0]
    agmt = topology.masters[0][1]

    log.info("Try to set %s: wv1" % WAITFOR_ASYNC_ATTR)
    try:
        mod = [(ldap.MOD_REPLACE, WAITFOR_ASYNC_ATTR, "wv1")]
        master1.modify_s(agmt, mod)
    except ldap.LDAPError as e:
        assert e.message['desc'] == 'Invalid syntax'


def test_multi_value(topology):
    """Tests multi value"""

    master1 = topology.masters[0][0]
    agmt = topology.masters[0][1]
    log.info("agmt: %s" % agmt)

    log.info("Try to set %s: 100 and 101 in the same time (multi value test)" % (
            WAITFOR_ASYNC_ATTR))
    try:
        mod = [(ldap.MOD_ADD, WAITFOR_ASYNC_ATTR, "100")]
        master1.modify_s(agmt, mod)
        mod = [(ldap.MOD_ADD, WAITFOR_ASYNC_ATTR, "101")]
        master1.modify_s(agmt, mod)
    except ldap.LDAPError as e:
        assert e.message['desc'] == 'Object class violation'


def test_value_check(topology, waitfor_async_attr):
    """Checks that value has been set correctly"""

    attr_value = waitfor_async_attr[0]

    for master in topology.masters:
        agmt = master[1]

        log.info("Check attr %s on %s" % (WAITFOR_ASYNC_ATTR, master[0].serverid))
        try:
            if attr_value:
                entry = master[0].search_s(agmt, ldap.SCOPE_BASE, "%s=%s" % (
                                          WAITFOR_ASYNC_ATTR, attr_value))
                assert entry
            else:
                entry = master[0].search_s(agmt, ldap.SCOPE_BASE, "%s=*" % WAITFOR_ASYNC_ATTR)
                assert not entry
        except ldap.LDAPError as e:
            log.fatal('Search failed, error: ' + e.message['desc'])
            assert False


def test_behavior_with_value(topology, waitfor_async_attr, entries):
    """Tests replication behavior with valid
    nsDS5ReplicaWaitForAsyncResults attribute values
    """

    master1 = topology.masters[0][0]
    sync_dict = Counter()
    min_ap = waitfor_async_attr[1][0]
    max_ap = waitfor_async_attr[1][1]

    time.sleep(20)

    log.info("Gather all sync attempts within Counter dict, group by timestamp")
    with open(master1.errlog, 'r') as errlog:
        errlog_filtered = filter(lambda x: "waitfor_async_results" in x, errlog)
        for line in errlog_filtered:
            # Watch only over unsuccessful sync attempts
            if line.split()[3] != line.split()[4]:
                timestamp = line.split(']')[0]
                sync_dict[timestamp] += 1

    log.info("Take the most common timestamp and assert it has appeared " \
             "in the range from %s to %s times" % (min_ap, max_ap))
    most_common_val = sync_dict.most_common(1)[0][1]
    assert min_ap <= most_common_val <= max_ap


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
