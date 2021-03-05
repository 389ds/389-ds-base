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

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
ENTRY_DN = "cn=test_entry, %s" % SUFFIX

MUST_OLD = "(postalAddress $ preferredLocale $ telexNumber)"
MAY_OLD = "(postalCode $ street)"

MUST_NEW = "(postalAddress $ preferredLocale)"
MAY_NEW = "(telexNumber $ postalCode $ street)"


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
    desc = 'To test ticket 47573'
    sup = 'person'
    if not must:
        must = MUST_OLD
    if not may:
        may = MAY_OLD

    new_oc = "( %s  NAME '%s' DESC '%s' SUP %s AUXILIARY MUST %s MAY %s )" % (oid, name, desc, sup, must, may)
    return ensure_bytes(new_oc)


def add_OC(instance, oid_ext, name):
    new_oc = _oc_definition(oid_ext, name)
    instance.schema.add_schema('objectClasses', new_oc)


def mod_OC(instance, oid_ext, name, old_must=None, old_may=None, new_must=None, new_may=None):
    old_oc = _oc_definition(oid_ext, name, old_must, old_may)
    new_oc = _oc_definition(oid_ext, name, new_must, new_may)
    instance.schema.del_schema('objectClasses', old_oc)
    instance.schema.add_schema('objectClasses', new_oc)


def trigger_schema_push(topology_m1c1):
    """
        It triggers an update on the supplier. This will start a replication
        session and a schema push
    """
    try:
        trigger_schema_push.value += 1
    except AttributeError:
        trigger_schema_push.value = 1
    replace = [(ldap.MOD_REPLACE, 'telephonenumber', ensure_bytes(str(trigger_schema_push.value)))]
    topology_m1c1.ms["supplier1"].modify_s(ENTRY_DN, replace)

    # wait 10 seconds that the update is replicated
    loop = 0
    while loop <= 10:
        try:
            ent = topology_m1c1.cs["consumer1"].getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)",
                                                         ['telephonenumber'])
            val = ent.telephonenumber or "0"
            if int(val) == trigger_schema_push.value:
                return
            # the expected value is not yet replicated. try again
            time.sleep(1)
            loop += 1
            log.debug("trigger_schema_push: receive %s (expected %d)" % (val, trigger_schema_push.value))
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1


def test_ticket47573_init(topology_m1c1):
    """
        Initialize the test environment
    """
    log.debug("test_ticket47573_init topology_m1c1 %r (supplier %r, consumer %r" %
              (topology_m1c1, topology_m1c1.ms["supplier1"], topology_m1c1.cs["consumer1"]))
    # the test case will check if a warning message is logged in the
    # error log of the supplier
    topology_m1c1.ms["supplier1"].errorlog_file = open(topology_m1c1.ms["supplier1"].errlog, "r")

    # This entry will be used to trigger attempt of schema push
    topology_m1c1.ms["supplier1"].add_s(Entry((ENTRY_DN, {
        'objectclass': "top person".split(),
        'sn': 'test_entry',
        'cn': 'test_entry'})))


def test_ticket47573_one(topology_m1c1):
    """
        Summary: Add a custom OC with MUST and MAY
            MUST = postalAddress $ preferredLocale
            MAY  = telexNumber   $ postalCode      $ street

        Final state
            - supplier +OCwithMayAttr
            - consumer +OCwithMayAttr

    """
    log.debug("test_ticket47573_one topology_m1c1 %r (supplier %r, consumer %r" % (
    topology_m1c1, topology_m1c1.ms["supplier1"], topology_m1c1.cs["consumer1"]))
    # update the schema of the supplier so that it is a superset of
    # consumer. Schema should be pushed
    new_oc = _oc_definition(2, 'OCwithMayAttr',
                            must=MUST_OLD,
                            may=MAY_OLD)
    topology_m1c1.ms["supplier1"].schema.add_schema('objectClasses', new_oc)

    trigger_schema_push(topology_m1c1)
    supplier_schema_csn = topology_m1c1.ms["supplier1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was updated on the consumer
    log.debug("test_ticket47573_one supplier_schema_csn=%s", supplier_schema_csn)
    log.debug("ctest_ticket47573_one onsumer_schema_csn=%s", consumer_schema_csn)
    assert supplier_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["supplier1"].errorlog_file, regex)
    assert res is None


def test_ticket47573_two(topology_m1c1):
    """
        Summary: Change OCwithMayAttr to move a MAY attribute to a MUST attribute


        Final state
            - supplier OCwithMayAttr updated
            - consumer OCwithMayAttr updated

    """

    # Update the objectclass so that a MAY attribute is moved to MUST attribute
    mod_OC(topology_m1c1.ms["supplier1"], 2, 'OCwithMayAttr', old_must=MUST_OLD, new_must=MUST_NEW, old_may=MAY_OLD,
           new_may=MAY_NEW)

    # now push the scheam
    trigger_schema_push(topology_m1c1)
    supplier_schema_csn = topology_m1c1.ms["supplier1"].schema.get_schema_csn()
    consumer_schema_csn = topology_m1c1.cs["consumer1"].schema.get_schema_csn()

    # Check the schemaCSN was NOT updated on the consumer
    log.debug("test_ticket47573_two supplier_schema_csn=%s", supplier_schema_csn)
    log.debug("test_ticket47573_two consumer_schema_csn=%s", consumer_schema_csn)
    assert supplier_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile("must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(topology_m1c1.ms["supplier1"].errorlog_file, regex)
    assert res is None


def test_ticket47573_three(topology_m1c1):
    '''
        Create a entry with OCwithMayAttr OC
    '''
    # Check replication is working fine
    dn = "cn=ticket47573, %s" % SUFFIX
    topology_m1c1.ms["supplier1"].add_s(Entry((dn,
                                             {'objectclass': "top person OCwithMayAttr".split(),
                                              'sn': 'test_repl',
                                              'cn': 'test_repl',
                                              'postalAddress': 'here',
                                              'preferredLocale': 'en',
                                              'telexNumber': '12$us$21',
                                              'postalCode': '54321'})))
    loop = 0
    ent = None
    while loop <= 10:
        try:
            ent = topology_m1c1.cs["consumer1"].getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    if ent is None:
        assert False

    log.info('Testcase PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
