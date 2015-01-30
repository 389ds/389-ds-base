'''
Created on Nov 7, 2013

@author: tbordaz
'''
import os
import sys
import ldap
import socket
import time
import logging
import pytest
import re
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation_prefix = None

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
ENTRY_DN = "cn=test_entry, %s" % SUFFIX
MUST_OLD = "(postalAddress $ preferredLocale)"
MUST_NEW = "(postalAddress $ preferredLocale $ telexNumber)"
MAY_OLD  = "(postalCode $ street)"
MAY_NEW  = "(postalCode $ street $ postOfficeBox)"


class TopologyMasterConsumer(object):
    def __init__(self, master, consumer):
        master.open()
        self.master = master

        consumer.open()
        self.consumer = consumer


def _header(topology, label):
    topology.master.log.info("\n\n###############################################")
    topology.master.log.info("#######")
    topology.master.log.info("####### %s" % label)
    topology.master.log.info("#######")
    topology.master.log.info("###################################################")


def pattern_errorlog(file, log_pattern):
    try:
        pattern_errorlog.last_pos += 1
    except AttributeError:
        pattern_errorlog.last_pos = 0

    found = None
    log.debug("_pattern_errorlog: start at offset %d" % pattern_errorlog.last_pos)
    file.seek(pattern_errorlog.last_pos)

    # Use a while true iteration because 'for line in file: hit a
    # python bug that break file.tell()
    while True:
        line = file.readline()
        log.debug("_pattern_errorlog: [%d] %s" % (file.tell(), line))
        found = log_pattern.search(line)
        if ((line == '') or (found)):
            break

    log.debug("_pattern_errorlog: end at offset %d" % file.tell())
    pattern_errorlog.last_pos = file.tell()
    return found


def _oc_definition(oid_ext, name, must=None, may=None):
    oid  = "1.2.3.4.5.6.7.8.9.10.%d" % oid_ext
    desc = 'To test ticket 47490'
    sup  = 'person'
    if not must:
        must = MUST_OLD
    if not may:
        may = MAY_OLD

    new_oc = "( %s  NAME '%s' DESC '%s' SUP %s AUXILIARY MUST %s MAY %s )" % (oid, name, desc, sup, must, may)
    return new_oc


def add_OC(instance, oid_ext, name):
    new_oc = _oc_definition(oid_ext, name)
    instance.schema.add_schema('objectClasses', new_oc)


def mod_OC(instance, oid_ext, name, old_must=None, old_may=None, new_must=None, new_may=None):
    old_oc = _oc_definition(oid_ext, name, old_must, old_may)
    new_oc = _oc_definition(oid_ext, name, new_must, new_may)
    instance.schema.del_schema('objectClasses', old_oc)
    instance.schema.add_schema('objectClasses', new_oc)


def support_schema_learning(topology):
    """
    with https://fedorahosted.org/389/ticket/47721, the supplier and consumer can learn
    schema definitions when a replication occurs.
    Before that ticket: replication of the schema fails requiring administrative operation
    In the test the schemaCSN (master consumer) differs

    After that ticket: replication of the schema succeeds (after an initial phase of learning)
    In the test the schema CSN (master consumer) are in sync

    This function returns True if 47721 is fixed in the current release
    False else
    """
    ent = topology.consumer.getEntry(DN_CONFIG, ldap.SCOPE_BASE, "(cn=config)", ['nsslapd-versionstring'])
    if ent.hasAttr('nsslapd-versionstring'):
        val = ent.getValue('nsslapd-versionstring')
        version = val.split('/')[1].split('.')  # something like ['1', '3', '1', '23', 'final_fix']
        major = int(version[0])
        minor = int(version[1])
        if major > 1:
            return True
        if minor > 3:
            # version is 1.4 or after
            return True
        if minor == 3:
            if version[2].isdigit():
                if int(version[2]) >= 3:
                    return True
        return False


def trigger_update(topology):
    """
        It triggers an update on the supplier. This will start a replication
        session and a schema push
    """
    try:
        trigger_update.value += 1
    except AttributeError:
        trigger_update.value = 1
    replace = [(ldap.MOD_REPLACE, 'telephonenumber', str(trigger_update.value))]
    topology.master.modify_s(ENTRY_DN, replace)

    # wait 10 seconds that the update is replicated
    loop = 0
    while loop <= 10:
        try:
            ent = topology.consumer.getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['telephonenumber'])
            val = ent.telephonenumber or "0"
            if int(val) == trigger_update.value:
                return
            # the expected value is not yet replicated. try again
            time.sleep(1)
            loop += 1
            log.debug("trigger_update: receive %s (expected %d)" % (val, trigger_update.value))
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1


def trigger_schema_push(topology):
    '''
    Trigger update to create a replication session.
    In case of 47721 is fixed and the replica needs to learn the missing definition, then
    the first replication session learn the definition and the second replication session
    push the schema (and the schemaCSN.
    This is why there is two updates and replica agreement is stopped/start (to create a second session)
    '''
    agreements = topology.master.agreement.list(suffix=SUFFIX, consumer_host=topology.consumer.host, consumer_port=topology.consumer.port)
    assert(len(agreements) == 1)
    ra = agreements[0]
    trigger_update(topology)
    topology.master.agreement.pause(ra.dn)
    topology.master.agreement.resume(ra.dn)
    trigger_update(topology)


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to create a replicated topology for the 'module'.
        The replicated topology is MASTER -> Consumer.
    '''
    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix

    master   = DirSrv(verbose=False)
    consumer = DirSrv(verbose=False)

    # Args for the master instance
    args_instance[SER_HOST] = HOST_MASTER_1
    args_instance[SER_PORT] = PORT_MASTER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_1
    args_master = args_instance.copy()
    master.allocate(args_master)

    # Args for the consumer instance
    args_instance[SER_HOST] = HOST_CONSUMER_1
    args_instance[SER_PORT] = PORT_CONSUMER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_CONSUMER_1
    args_consumer = args_instance.copy()
    consumer.allocate(args_consumer)

    # Get the status of the instance
    instance_master = master.exists()
    instance_consumer = consumer.exists()

    # Remove all the instances
    if instance_master:
        master.delete()
    if instance_consumer:
        consumer.delete()

    # Create the instances
    master.create()
    master.open()
    consumer.create()
    consumer.open()

    #
    # Now prepare the Master-Consumer topology
    #
    # First Enable replication
    master.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_1)
    consumer.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_CONSUMER)

    # Initialize the supplier->consumer
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    repl_agreement = master.agreement.create(suffix=SUFFIX, host=consumer.host, port=consumer.port, properties=properties)

    if not repl_agreement:
        log.fatal("Fail to create a replica agreement")
        sys.exit(1)

    log.debug("%s created" % repl_agreement)
    master.agreement.init(SUFFIX, HOST_CONSUMER_1, PORT_CONSUMER_1)
    master.waitForReplInit(repl_agreement)

    # Check replication is working fine
    master.add_s(Entry((TEST_REPL_DN, {
                 'objectclass': "top person".split(),
                 'sn': 'test_repl',
                 'cn': 'test_repl'})))
    ent = None
    loop = 0
    while loop <= 10:
        try:
            ent = consumer.getEntry(TEST_REPL_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    if ent is None:
        assert False

    # clear the tmp directory
    master.clearTmpDir(__file__)

    #
    # Here we have two instances master and consumer
    # with replication working.
    return TopologyMasterConsumer(master, consumer)


def test_ticket47490_init(topology):
    """
        Initialize the test environment
    """
    log.debug("test_ticket47490_init topology %r (master %r, consumer %r" % (topology, topology.master, topology.consumer))
    # the test case will check if a warning message is logged in the
    # error log of the supplier
    topology.master.errorlog_file = open(topology.master.errlog, "r")

    # This entry will be used to trigger attempt of schema push
    topology.master.add_s(Entry((ENTRY_DN, {
                                            'objectclass': "top person".split(),
                                            'sn': 'test_entry',
                                            'cn': 'test_entry'})))


def test_ticket47490_one(topology):
    """
        Summary: Extra OC Schema is pushed - no error

        If supplier schema is a superset (one extra OC) of consumer schema, then
        schema is pushed and there is no message in the error log
        State at startup:
            - supplier default schema
            - consumer default schema
        Final state
            - supplier +masterNewOCA
            - consumer +masterNewOCA

    """
    _header(topology, "Extra OC Schema is pushed - no error")

    log.debug("test_ticket47490_one topology %r (master %r, consumer %r" % (topology, topology.master, topology.consumer))
    # update the schema of the supplier so that it is a superset of
    # consumer. Schema should be pushed
    add_OC(topology.master, 2, 'masterNewOCA')

    trigger_schema_push(topology)
    master_schema_csn = topology.master.schema.get_schema_csn()
    consumer_schema_csn = topology.consumer.schema.get_schema_csn()

    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_one master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_one onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_two(topology):
    """
        Summary: Extra OC Schema is pushed - (ticket 47721 allows to learn missing def)

        If consumer schema is a superset (one extra OC) of supplier schema, then
        schema is pushed and there is a message in the error log
        State at startup
            - supplier +masterNewOCA
            - consumer +masterNewOCA
        Final state
            - supplier +masterNewOCA +masterNewOCB
            - consumer +masterNewOCA               +consumerNewOCA

    """

    _header(topology, "Extra OC Schema is pushed - (ticket 47721 allows to learn missing def)")

    # add this OC on consumer. Supplier will no push the schema
    add_OC(topology.consumer, 1, 'consumerNewOCA')

    # add a new OC on the supplier so that its nsSchemaCSN is larger than the consumer (wait 2s)
    time.sleep(2)
    add_OC(topology.master, 3, 'masterNewOCB')

    # now push the scheam
    trigger_schema_push(topology)
    master_schema_csn = topology.master.schema.get_schema_csn()
    consumer_schema_csn = topology.consumer.schema.get_schema_csn()

    # Check the schemaCSN was NOT updated on the consumer
    # with 47721, supplier learns the missing definition
    log.debug("test_ticket47490_two master_schema_csn=%s", master_schema_csn)
    log.debug("test_ticket47490_two consumer_schema_csn=%s", consumer_schema_csn)
    if support_schema_learning(topology):
        assert master_schema_csn == consumer_schema_csn
    else:
        assert master_schema_csn != consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    # This message may happen during the learning phase
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)


def test_ticket47490_three(topology):
    """
        Summary: Extra OC Schema is pushed - no error

        If supplier schema is again a superset (one extra OC), then
        schema is  pushed and there is no message in the error log
        State at startup
            - supplier +masterNewOCA +masterNewOCB
            - consumer +masterNewOCA               +consumerNewOCA
        Final state
            - supplier +masterNewOCA +masterNewOCB +consumerNewOCA
            - consumer +masterNewOCA +masterNewOCB +consumerNewOCA

    """
    _header(topology, "Extra OC Schema is pushed - no error")

    # Do an upate to trigger the schema push attempt
    # add this OC on consumer. Supplier will no push the schema
    add_OC(topology.master, 1, 'consumerNewOCA')

    # now push the scheam
    trigger_schema_push(topology)
    master_schema_csn = topology.master.schema.get_schema_csn()
    consumer_schema_csn = topology.consumer.schema.get_schema_csn()

    # Check the schemaCSN was NOT updated on the consumer
    log.debug("test_ticket47490_three master_schema_csn=%s", master_schema_csn)
    log.debug("test_ticket47490_three consumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_four(topology):
    """
        Summary: Same OC - extra MUST: Schema is pushed - no error

        If supplier schema is again a superset (OC with more MUST), then
        schema is  pushed and there is no message in the error log
        State at startup
            - supplier +masterNewOCA +masterNewOCB +consumerNewOCA
            - consumer +masterNewOCA +masterNewOCB +consumerNewOCA
        Final state
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA
                       +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA
                       +must=telexnumber

    """
    _header(topology, "Same OC - extra MUST: Schema is pushed - no error")

    mod_OC(topology.master, 2, 'masterNewOCA', old_must=MUST_OLD, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_OLD)

    trigger_schema_push(topology)
    master_schema_csn = topology.master.schema.get_schema_csn()
    consumer_schema_csn = topology.consumer.schema.get_schema_csn()

    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_four master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_four onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_five(topology):
    """
        Summary: Same OC - extra MUST: Schema is pushed - (fix for 47721)

        If consumer schema is  a superset (OC with more MUST), then
        schema is  pushed (fix for 47721) and there is a message in the error log
        State at startup
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA
                       +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA
                        +must=telexnumber
        Final state
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA
                       +must=telexnumber                   +must=telexnumber

        Note: replication log is enabled to get more details
    """
    _header(topology, "Same OC - extra MUST: Schema is pushed - (fix for 47721)")

    # get more detail why it fails
    topology.master.enableReplLogging()

    # add telenumber to 'consumerNewOCA' on the consumer
    mod_OC(topology.consumer, 1, 'consumerNewOCA', old_must=MUST_OLD, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_OLD)
    # add a new OC on the supplier so that its nsSchemaCSN is larger than the consumer (wait 2s)
    time.sleep(2)
    add_OC(topology.master, 4, 'masterNewOCC')

    trigger_schema_push(topology)
    master_schema_csn = topology.master.schema.get_schema_csn()
    consumer_schema_csn = topology.consumer.schema.get_schema_csn()

    # Check the schemaCSN was NOT updated on the consumer
    # with 47721, supplier learns the missing definition
    log.debug("test_ticket47490_five master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_five onsumer_schema_csn=%s", consumer_schema_csn)
    if support_schema_learning(topology):
        assert master_schema_csn == consumer_schema_csn
    else:
        assert master_schema_csn != consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    # This message may happen during the learning phase
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)


def test_ticket47490_six(topology):
    """
        Summary: Same OC - extra MUST: Schema is pushed - no error

        If supplier schema is  again a superset (OC with more MUST), then
        schema is  pushed and there is no message in the error log
        State at startup
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA
                       +must=telexnumber                   +must=telexnumber
        Final state

            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber

        Note: replication log is enabled to get more details
    """
    _header(topology, "Same OC - extra MUST: Schema is pushed - no error")

    # add telenumber to 'consumerNewOCA' on the consumer
    mod_OC(topology.master, 1, 'consumerNewOCA', old_must=MUST_OLD, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_OLD)

    trigger_schema_push(topology)
    master_schema_csn = topology.master.schema.get_schema_csn()
    consumer_schema_csn = topology.consumer.schema.get_schema_csn()

    # Check the schemaCSN was NOT updated on the consumer
    log.debug("test_ticket47490_six master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_six onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    # This message may happen during the learning phase
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_seven(topology):
    """
        Summary: Same OC - extra MAY: Schema is pushed - no error

        If supplier schema is again a superset (OC with more MAY), then
        schema is  pushed and there is no message in the error log
        State at startup
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
        Final stat
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox
    """
    _header(topology, "Same OC - extra MAY: Schema is pushed - no error")

    mod_OC(topology.master, 2, 'masterNewOCA', old_must=MUST_NEW, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_NEW)

    trigger_schema_push(topology)
    master_schema_csn = topology.master.schema.get_schema_csn()
    consumer_schema_csn = topology.consumer.schema.get_schema_csn()

    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_seven master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_seven consumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_eight(topology):
    """
        Summary: Same OC - extra MAY: Schema is pushed (fix for 47721)

        If consumer schema is a superset (OC with more MAY), then
        schema is  pushed (fix for 47721) and there is  message in the error log
        State at startup
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox
        Final state
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                                     +may=postOfficeBox
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                  +may=postOfficeBox
    """
    _header(topology, "Same OC - extra MAY: Schema is pushed (fix for 47721)")

    mod_OC(topology.consumer, 1, 'consumerNewOCA', old_must=MUST_NEW, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_NEW)

    # modify OC on the supplier so that its nsSchemaCSN is larger than the consumer (wait 2s)
    time.sleep(2)
    mod_OC(topology.master, 4, 'masterNewOCC', old_must=MUST_OLD, new_must=MUST_OLD, old_may=MAY_OLD, new_may=MAY_NEW)

    trigger_schema_push(topology)
    master_schema_csn = topology.master.schema.get_schema_csn()
    consumer_schema_csn = topology.consumer.schema.get_schema_csn()

    # Check the schemaCSN was not updated on the consumer
    # with 47721, supplier learns the missing definition
    log.debug("test_ticket47490_eight master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_eight onsumer_schema_csn=%s", consumer_schema_csn)
    if support_schema_learning(topology):
        assert master_schema_csn == consumer_schema_csn
    else:
        assert master_schema_csn != consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    # This message may happen during the learning phase
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)


def test_ticket47490_nine(topology):
    """
        Summary: Same OC - extra MAY: Schema is pushed - no error

        If consumer schema is a superset (OC with more MAY), then
        schema is  not pushed and there is  message in the error log
        State at startup
            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                                     +may=postOfficeBox
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                  +may=postOfficeBox

        Final state

            - supplier +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                  +may=postOfficeBox +may=postOfficeBox
            - consumer +masterNewOCA     +masterNewOCB     +consumerNewOCA    +masterNewOCC
                       +must=telexnumber                   +must=telexnumber
                       +may=postOfficeBox                  +may=postOfficeBox +may=postOfficeBox
    """
    _header(topology, "Same OC - extra MAY: Schema is pushed - no error")

    mod_OC(topology.master, 1, 'consumerNewOCA', old_must=MUST_NEW, new_must=MUST_NEW, old_may=MAY_OLD, new_may=MAY_NEW)

    trigger_schema_push(topology)
    master_schema_csn = topology.master.schema.get_schema_csn()
    consumer_schema_csn = topology.consumer.schema.get_schema_csn()

    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_nine master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_nine onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology.master.errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_final(topology):
    topology.master.delete()
    topology.consumer.delete()
    log.info('Testcase PASSED')


def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix = None

    topo = topology(True)
    test_ticket47490_init(topo)
    test_ticket47490_one(topo)
    test_ticket47490_two(topo)
    test_ticket47490_three(topo)
    test_ticket47490_four(topo)
    test_ticket47490_five(topo)
    test_ticket47490_six(topo)
    test_ticket47490_seven(topo)
    test_ticket47490_eight(topo)
    test_ticket47490_nine(topo)

    test_ticket47490_final(topo)


if __name__ == '__main__':
    run_isolated()

