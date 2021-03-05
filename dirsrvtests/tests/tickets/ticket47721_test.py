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
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_m2
from lib389.replica import ReplicationManager
from lib389.utils import *

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

SCHEMA_DN = "cn=schema"
TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
OC_NAME = 'OCticket47721'
OC_OID_EXT = 2
MUST = "(postalAddress $ postalCode)"
MAY = "(member $ street)"

OC2_NAME = 'OC2ticket47721'
OC2_OID_EXT = 3
MUST_2 = "(postalAddress $ postalCode)"
MAY_2 = "(member $ street)"

REPL_SCHEMA_POLICY_CONSUMER = "cn=consumerUpdatePolicy,cn=replSchema,cn=config"
REPL_SCHEMA_POLICY_SUPPLIER = "cn=supplierUpdatePolicy,cn=replSchema,cn=config"

OTHER_NAME = 'other_entry'
MAX_OTHERS = 10

BIND_NAME = 'bind_entry'
BIND_DN = 'cn=%s, %s' % (BIND_NAME, SUFFIX)
BIND_PW = 'password'

ENTRY_NAME = 'test_entry'
ENTRY_DN = 'cn=%s, %s' % (ENTRY_NAME, SUFFIX)
ENTRY_OC = "top person %s" % OC_NAME

BASE_OID = "1.2.3.4.5.6.7.8.9.10"

SLEEP_INTERVAL = 60


def _add_custom_at_definition(name='ATticket47721'):
    new_at = "( %s-oid NAME '%s' DESC 'test AT ticket 47721' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN ( 'Test 47721' 'user defined' ) )" % (
    name, name)
    return ensure_bytes(new_at)


def _chg_std_at_defintion():
    new_at = "( 2.16.840.1.113730.3.1.569 NAME 'cosPriority' DESC 'Netscape defined attribute type' SYNTAX 1.3.6.1.4.1.1466.115.121.1.27 X-ORIGIN 'Netscape Directory Server' )"
    return ensure_bytes(new_at)


def _add_custom_oc_defintion(name='OCticket47721'):
    new_oc = "( %s-oid NAME '%s' DESC 'An group of related automount objects' SUP top STRUCTURAL MUST ou X-ORIGIN 'draft-howard-rfc2307bis' )" % (
    name, name)
    return ensure_bytes(new_oc)


def _chg_std_oc_defintion():
    new_oc = "( 5.3.6.1.1.1.2.0 NAME 'trustAccount' DESC 'Sets trust accounts information' SUP top AUXILIARY MUST trustModel MAY ( accessTo $ ou ) X-ORIGIN 'nss_ldap/pam_ldap' )"
    return ensure_bytes(new_oc)

def replication_check(topology_m2):
    repl = ReplicationManager(SUFFIX)
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    return repl.test_replication(supplier1, supplier2)

def test_ticket47721_init(topology_m2):
    """
        It adds
           - Objectclass with MAY 'member'
           - an entry ('bind_entry') with which we bind to test the 'SELFDN' operation
        It deletes the anonymous aci

    """

    # entry used to bind with
    topology_m2.ms["supplier1"].log.info("Add %s" % BIND_DN)
    topology_m2.ms["supplier1"].add_s(Entry((BIND_DN, {
        'objectclass': "top person".split(),
        'sn': BIND_NAME,
        'cn': BIND_NAME,
        'userpassword': BIND_PW})))

    # enable repl error logging
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', ensure_bytes(str(8192)))]  # REPL logging
    topology_m2.ms["supplier1"].modify_s(DN_CONFIG, mod)
    topology_m2.ms["supplier2"].modify_s(DN_CONFIG, mod)

    # add dummy entries
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology_m2.ms["supplier1"].add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))


def test_ticket47721_0(topology_m2):
    dn = "cn=%s0,%s" % (OTHER_NAME, SUFFIX)
    replication_check(topology_m2)
    ent = topology_m2.ms["supplier2"].getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent


def test_ticket47721_1(topology_m2):
    log.info('Running test 1...')
    # topology_m2.ms["supplier1"].log.info("Attach debugger\n\n")
    # time.sleep(30)

    new = _add_custom_at_definition()
    topology_m2.ms["supplier1"].log.info("Add (M2) %s " % new)
    topology_m2.ms["supplier2"].schema.add_schema('attributetypes', new)

    new = _chg_std_at_defintion()
    topology_m2.ms["supplier1"].log.info("Chg (M2) %s " % new)
    topology_m2.ms["supplier2"].schema.add_schema('attributetypes', new)

    new = _add_custom_oc_defintion()
    topology_m2.ms["supplier1"].log.info("Add (M2) %s " % new)
    topology_m2.ms["supplier2"].schema.add_schema('objectClasses', new)

    new = _chg_std_oc_defintion()
    topology_m2.ms["supplier1"].log.info("Chg (M2) %s " % new)
    topology_m2.ms["supplier2"].schema.add_schema('objectClasses', new)

    mod = [(ldap.MOD_REPLACE, 'description', b'Hello world 1')]
    dn = "cn=%s0,%s" % (OTHER_NAME, SUFFIX)
    topology_m2.ms["supplier2"].modify_s(dn, mod)

    replication_check(topology_m2)
    ent = topology_m2.ms["supplier1"].getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ensure_str(ent.getValue('description')) == 'Hello world 1'

    time.sleep(2)
    schema_csn_supplier1 = topology_m2.ms["supplier1"].schema.get_schema_csn()
    schema_csn_supplier2 = topology_m2.ms["supplier2"].schema.get_schema_csn()
    log.debug('Supplier 1 schemaCSN: %s' % schema_csn_supplier1)
    log.debug('Supplier 2 schemaCSN: %s' % schema_csn_supplier2)


def test_ticket47721_2(topology_m2):
    log.info('Running test 2...')

    mod = [(ldap.MOD_REPLACE, 'description', b'Hello world 2')]
    dn = "cn=%s0,%s" % (OTHER_NAME, SUFFIX)
    topology_m2.ms["supplier1"].modify_s(dn, mod)

    replication_check(topology_m2)
    ent = topology_m2.ms["supplier2"].getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ensure_str(ent.getValue('description')) == 'Hello world 2'

    time.sleep(2)
    schema_csn_supplier1 = topology_m2.ms["supplier1"].schema.get_schema_csn()
    schema_csn_supplier2 = topology_m2.ms["supplier2"].schema.get_schema_csn()
    log.debug('Supplier 1 schemaCSN: %s' % schema_csn_supplier1)
    log.debug('Supplier 2 schemaCSN: %s' % schema_csn_supplier2)
    if schema_csn_supplier1 != schema_csn_supplier2:
        # We need to give the server a little more time, then check it again
        log.info('Schema CSNs are not in sync yet: m1 (%s) vs m2 (%s), wait a little...'
                 % (schema_csn_supplier1, schema_csn_supplier2))
        time.sleep(SLEEP_INTERVAL)
        schema_csn_supplier1 = topology_m2.ms["supplier1"].schema.get_schema_csn()
        schema_csn_supplier2 = topology_m2.ms["supplier2"].schema.get_schema_csn()

    assert schema_csn_supplier1 is not None
    assert schema_csn_supplier1 == schema_csn_supplier2


def test_ticket47721_3(topology_m2):
    '''
    Check that the supplier can update its schema from consumer schema
    Update M2 schema, then trigger a replication M1->M2
    '''
    log.info('Running test 3...')

    # stop RA M2->M1, so that M1 can only learn being a supplier
    ents = topology_m2.ms["supplier2"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology_m2.ms["supplier2"].agreement.pause(ents[0].dn)

    new = _add_custom_at_definition('ATtest3')
    topology_m2.ms["supplier1"].log.info("Update schema (M2) %s " % new)
    topology_m2.ms["supplier2"].schema.add_schema('attributetypes', new)
    time.sleep(1)

    new = _add_custom_oc_defintion('OCtest3')
    topology_m2.ms["supplier1"].log.info("Update schema (M2) %s " % new)
    topology_m2.ms["supplier2"].schema.add_schema('objectClasses', new)
    time.sleep(1)

    mod = [(ldap.MOD_REPLACE, 'description', b'Hello world 3')]
    dn = "cn=%s0,%s" % (OTHER_NAME, SUFFIX)
    topology_m2.ms["supplier1"].modify_s(dn, mod)

    replication_check(topology_m2)
    ent = topology_m2.ms["supplier2"].getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ensure_str(ent.getValue('description')) == 'Hello world 3'

    time.sleep(5)
    schema_csn_supplier1 = topology_m2.ms["supplier1"].schema.get_schema_csn()
    schema_csn_supplier2 = topology_m2.ms["supplier2"].schema.get_schema_csn()
    log.debug('Supplier 1 schemaCSN: %s' % schema_csn_supplier1)
    log.debug('Supplier 2 schemaCSN: %s' % schema_csn_supplier2)
    if schema_csn_supplier1 == schema_csn_supplier2:
        # We need to give the server a little more time, then check it again
        log.info('Schema CSNs are not in sync yet: m1 (%s) vs m2 (%s), wait a little...'
                 % (schema_csn_supplier1, schema_csn_supplier2))
        time.sleep(SLEEP_INTERVAL)
        schema_csn_supplier1 = topology_m2.ms["supplier1"].schema.get_schema_csn()
        schema_csn_supplier2 = topology_m2.ms["supplier2"].schema.get_schema_csn()

    assert schema_csn_supplier1 is not None
    # schema csn on M2 is larger that on M1. M1 only took the new definitions
    assert schema_csn_supplier1 != schema_csn_supplier2


def test_ticket47721_4(topology_m2):
    '''
    Here M2->M1 agreement is disabled.
    with test_ticket47721_3, M1 schema and M2 should be identical BUT
    the nsschemacsn is M2>M1. But as the RA M2->M1 is disabled, M1 keeps its schemacsn.
    Update schema on M2 (nsschemaCSN update), update M2. Check they have the same schemacsn
    '''
    log.info('Running test 4...')

    new = _add_custom_at_definition('ATtest4')
    topology_m2.ms["supplier1"].log.info("Update schema (M1) %s " % new)
    topology_m2.ms["supplier1"].schema.add_schema('attributetypes', new)

    new = _add_custom_oc_defintion('OCtest4')
    topology_m2.ms["supplier1"].log.info("Update schema (M1) %s " % new)
    topology_m2.ms["supplier1"].schema.add_schema('objectClasses', new)

    topology_m2.ms["supplier1"].log.info("trigger replication M1->M2: to update the schema")
    mod = [(ldap.MOD_REPLACE, 'description', b'Hello world 4')]
    dn = "cn=%s0,%s" % (OTHER_NAME, SUFFIX)
    topology_m2.ms["supplier1"].modify_s(dn, mod)

    replication_check(topology_m2)
    ent = topology_m2.ms["supplier2"].getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ensure_str(ent.getValue('description')) == 'Hello world 4'

    topology_m2.ms["supplier1"].log.info("trigger replication M1->M2: to push the schema")
    mod = [(ldap.MOD_REPLACE, 'description', b'Hello world 5')]
    dn = "cn=%s0,%s" % (OTHER_NAME, SUFFIX)
    topology_m2.ms["supplier1"].modify_s(dn, mod)

    replication_check(topology_m2)
    ent = topology_m2.ms["supplier2"].getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ensure_str(ent.getValue('description')) == 'Hello world 5'

    time.sleep(2)
    schema_csn_supplier1 = topology_m2.ms["supplier1"].schema.get_schema_csn()
    schema_csn_supplier2 = topology_m2.ms["supplier2"].schema.get_schema_csn()
    log.debug('Supplier 1 schemaCSN: %s' % schema_csn_supplier1)
    log.debug('Supplier 2 schemaCSN: %s' % schema_csn_supplier2)
    if schema_csn_supplier1 != schema_csn_supplier2:
        # We need to give the server a little more time, then check it again
        log.info('Schema CSNs are incorrectly in sync, wait a little...')
        time.sleep(SLEEP_INTERVAL)
        schema_csn_supplier1 = topology_m2.ms["supplier1"].schema.get_schema_csn()
        schema_csn_supplier2 = topology_m2.ms["supplier2"].schema.get_schema_csn()

    assert schema_csn_supplier1 is not None
    assert schema_csn_supplier1 == schema_csn_supplier2


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
