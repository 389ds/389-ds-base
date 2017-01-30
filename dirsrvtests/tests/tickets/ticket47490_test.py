# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
'''
Created on Nov 7, 2013

@author: tbordaz
'''
import logging
import re
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_m1c1

from lib389.utils import *

# Skip on older versions
pytestmark = pytest.mark.skipif(ds_is_older('1.3'), reason="Not implemented")
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
ENTRY_DN = "cn=test_entry, %s" % SUFFIX
MUST_OLD = "(postalAddress $ preferredLocale)"
MUST_NEW = "(postalAddress $ preferredLocale $ telexNumber)"
MAY_OLD = "(postalCode $ street)"
MAY_NEW = "(postalCode $ street $ postOfficeBox)"


def _header(topology_m1c1, label):
    topology_m1c1.ms["master1"].log.info("\n\n###############################################")
    topology_m1c1.ms["master1"].log.info("#######")
    topology_m1c1.ms["master1"].log.info("####### %s" % label)
    topology_m1c1.ms["master1"].log.info("#######")
    topology_m1c1.ms["master1"].log.info("###################################################")


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
    oid = "1.2.3.4.5.6.7.8.9.10.%d" % oid_ext
    desc = 'To test ticket 47490'
    sup = 'person'
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


def support_schema_learning(topology_m1c1):
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
    ent = topology_m1c1.cs["consumer1"].getEntry(DN_CONFIG, ldap.SCOPE_BASE, "(cn=config)", ['nsslapd-versionstring'])
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


def trigger_update(topology_m1c1):
    """
        It triggers an update on the supplier. This will start a replication
        session and a schema push
    """
    try:
        trigger_update.value += 1
    except AttributeError:
        trigger_update.value = 1
    replace = [(ldap.MOD_REPLACE, 'telephonenumber', str(trigger_update.value))]
    topology_m1c1.ms["master1"].modify_s(ENTRY_DN, replace)

    # wait 10 seconds that the update is replicated
    loop = 0
    while loop <= 10:
        try:
            ent = topology_m1c1.cs["consumer1"].getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)",
                                                         ['telephonenumber'])
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


def trigger_schema_push(topology_m1c1):
    '''
    Trigger update to create a replication session.
    In case of 47721 is fixed and the replica needs to learn the missing definition, then
    the first replication session learn the definition and the second replication session
    push the schema (and the schemaCSN.
    This is why there is two updates and replica agreement is stopped/start (to create a second session)
    '''
    agreements = topology_m1c1.ms["master1"].agreement.list(suffix=SUFFIX,
                                                            consumer_host=topology_m1c1.cs["consumer1"].host,
                                                            consumer_port=topology_m1c1.cs["consumer1"].port)
    assert (len(agreements) == 1)
    ra = agreements[0]
    trigger_update(topology_m1c1)
    topology_m1c1.ms["master1"].agreement.pause(ra.dn)
    topology_m1c1.ms["master1"].agreement.resume(ra.dn)
    trigger_update(topology_m1c1)


def test_ticket47490_init(topology_m1c1):
    """
        Initialize the test environment
    """
    log.debug("test_ticket47490_init topology_m1c1 %r (master %r, consumer %r" % (
    topology_m1c1, topology_m1c1.ms["master1"], topology_m1c1.cs["consumer1"]))
    # the test case will check if a warning message is logged in the
    # error log of the supplier
    topology_m1c1.ms["master1"].errorlog_file = open(topology_m1c1.ms["master1"].errlog, "r")

    # This entry will be used to trigger attempt of schema push
    topology_m1c1.ms["master1"].add_s(Entry((ENTRY_DN, {
        'objectclass': "top person".split(),
        'sn': 'test_entry',
        'cn': 'test_entry'})))


def test_ticket47490_one(topology_m1c1):
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
    _header(topology_m1c1, "Extra OC Schema is pushed - no error")

    log.debug("test_ticket47490_one topology_m1c1 %r (master %r, consumer %r" % (
    topology_m1c1, topology_m1c1.ms["master1"], topology_m1c1.cs["consumer1"]))
    # update the schema of the supplier so that it is a superset of
    # consumer. Schema should be pushed
    add_OC(topology_m1c1.ms["master1"], 2, 'masterNewOCA')

    trigger_schema_push(topology_m1c1)
    master_schema_csn = topology_m1c1.ms["master1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_one master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_one onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["master1"].errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_two(topology_m1c1):
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

    _header(topology_m1c1, "Extra OC Schema is pushed - (ticket 47721 allows to learn missing def)")

    # add this OC on consumer. Supplier will no push the schema
    add_OC(topology_m1c1.cs["consumer1"], 1, 'consumerNewOCA')

    # add a new OC on the supplier so that its nsSchemaCSN is larger than the consumer (wait 2s)
    time.sleep(2)
    add_OC(topology_m1c1.ms["master1"], 3, 'masterNewOCB')

    # now push the scheam
    trigger_schema_push(topology_m1c1)
    master_schema_csn = topology_m1c1.ms["master1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was NOT updated on the consumer
    # with 47721, supplier learns the missing definition
    log.debug("test_ticket47490_two master_schema_csn=%s", master_schema_csn)
    log.debug("test_ticket47490_two consumer_schema_csn=%s", consumer_schema_csn)
    if support_schema_learning(topology_m1c1):
        assert master_schema_csn == consumer_schema_csn
    else:
        assert master_schema_csn != consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    # This message may happen during the learning phase
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["master1"].errorlog_file, regex)


def test_ticket47490_three(topology_m1c1):
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
    _header(topology_m1c1, "Extra OC Schema is pushed - no error")

    # Do an upate to trigger the schema push attempt
    # add this OC on consumer. Supplier will no push the schema
    add_OC(topology_m1c1.ms["master1"], 1, 'consumerNewOCA')

    # now push the scheam
    trigger_schema_push(topology_m1c1)
    master_schema_csn = topology_m1c1.ms["master1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was NOT updated on the consumer
    log.debug("test_ticket47490_three master_schema_csn=%s", master_schema_csn)
    log.debug("test_ticket47490_three consumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["master1"].errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_four(topology_m1c1):
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
    _header(topology_m1c1, "Same OC - extra MUST: Schema is pushed - no error")

    mod_OC(topology_m1c1.ms["master1"], 2, 'masterNewOCA', old_must=MUST_OLD, new_must=MUST_NEW, old_may=MAY_OLD,
           new_may=MAY_OLD)

    trigger_schema_push(topology_m1c1)
    master_schema_csn = topology_m1c1.ms["master1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_four master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_four onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["master1"].errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_five(topology_m1c1):
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
    _header(topology_m1c1, "Same OC - extra MUST: Schema is pushed - (fix for 47721)")

    # get more detail why it fails
    topology_m1c1.ms["master1"].enableReplLogging()

    # add telenumber to 'consumerNewOCA' on the consumer
    mod_OC(topology_m1c1.cs["consumer1"], 1, 'consumerNewOCA', old_must=MUST_OLD, new_must=MUST_NEW, old_may=MAY_OLD,
           new_may=MAY_OLD)
    # add a new OC on the supplier so that its nsSchemaCSN is larger than the consumer (wait 2s)
    time.sleep(2)
    add_OC(topology_m1c1.ms["master1"], 4, 'masterNewOCC')

    trigger_schema_push(topology_m1c1)
    master_schema_csn = topology_m1c1.ms["master1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was NOT updated on the consumer
    # with 47721, supplier learns the missing definition
    log.debug("test_ticket47490_five master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_five onsumer_schema_csn=%s", consumer_schema_csn)
    if support_schema_learning(topology_m1c1):
        assert master_schema_csn == consumer_schema_csn
    else:
        assert master_schema_csn != consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    # This message may happen during the learning phase
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["master1"].errorlog_file, regex)


def test_ticket47490_six(topology_m1c1):
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
    _header(topology_m1c1, "Same OC - extra MUST: Schema is pushed - no error")

    # add telenumber to 'consumerNewOCA' on the consumer
    mod_OC(topology_m1c1.ms["master1"], 1, 'consumerNewOCA', old_must=MUST_OLD, new_must=MUST_NEW, old_may=MAY_OLD,
           new_may=MAY_OLD)

    trigger_schema_push(topology_m1c1)
    master_schema_csn = topology_m1c1.ms["master1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was NOT updated on the consumer
    log.debug("test_ticket47490_six master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_six onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    # This message may happen during the learning phase
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["master1"].errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_seven(topology_m1c1):
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
    _header(topology_m1c1, "Same OC - extra MAY: Schema is pushed - no error")

    mod_OC(topology_m1c1.ms["master1"], 2, 'masterNewOCA', old_must=MUST_NEW, new_must=MUST_NEW, old_may=MAY_OLD,
           new_may=MAY_NEW)

    trigger_schema_push(topology_m1c1)
    master_schema_csn = topology_m1c1.ms["master1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_seven master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_seven consumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["master1"].errorlog_file, regex)
    if res is not None:
        assert False


def test_ticket47490_eight(topology_m1c1):
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
    _header(topology_m1c1, "Same OC - extra MAY: Schema is pushed (fix for 47721)")

    mod_OC(topology_m1c1.cs["consumer1"], 1, 'consumerNewOCA', old_must=MUST_NEW, new_must=MUST_NEW, old_may=MAY_OLD,
           new_may=MAY_NEW)

    # modify OC on the supplier so that its nsSchemaCSN is larger than the consumer (wait 2s)
    time.sleep(2)
    mod_OC(topology_m1c1.ms["master1"], 4, 'masterNewOCC', old_must=MUST_OLD, new_must=MUST_OLD, old_may=MAY_OLD,
           new_may=MAY_NEW)

    trigger_schema_push(topology_m1c1)
    master_schema_csn = topology_m1c1.ms["master1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was not updated on the consumer
    # with 47721, supplier learns the missing definition
    log.debug("test_ticket47490_eight master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_eight onsumer_schema_csn=%s", consumer_schema_csn)
    if support_schema_learning(topology_m1c1):
        assert master_schema_csn == consumer_schema_csn
    else:
        assert master_schema_csn != consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    # This message may happen during the learning phase
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["master1"].errorlog_file, regex)


def test_ticket47490_nine(topology_m1c1):
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
    _header(topology_m1c1, "Same OC - extra MAY: Schema is pushed - no error")

    mod_OC(topology_m1c1.ms["master1"], 1, 'consumerNewOCA', old_must=MUST_NEW, new_must=MUST_NEW, old_may=MAY_OLD,
           new_may=MAY_NEW)

    trigger_schema_push(topology_m1c1)
    master_schema_csn = topology_m1c1.ms["master1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47490_nine master_schema_csn=%s", master_schema_csn)
    log.debug("ctest_ticket47490_nine onsumer_schema_csn=%s", consumer_schema_csn)
    assert master_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["master1"].errorlog_file, regex)
    if res is not None:
        assert False

    log.info('Testcase PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
