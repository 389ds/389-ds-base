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

logging.getLogger(__name__).setLevel(logging.DEBUG)
from lib389.utils import *

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.2'), reason="Not implemented")]
log = logging.getLogger(__name__)

SCHEMA_DN = "cn=schema"
TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
OC_NAME = 'OCticket47676'
OC_OID_EXT = 2
MUST = "(postalAddress $ postalCode)"
MAY = "(member $ street)"

OC2_NAME = 'OC2ticket47676'
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


def _oc_definition(oid_ext, name, must=None, may=None):
    oid = "%s.%d" % (BASE_OID, oid_ext)
    desc = 'To test ticket 47490'
    sup = 'person'
    if not must:
        must = MUST
    if not may:
        may = MAY

    new_oc = "( %s  NAME '%s' DESC '%s' SUP %s AUXILIARY MUST %s MAY %s )" % (oid, name, desc, sup, must, may)
    return ensure_bytes(new_oc)

def replication_check(topology_m2):
    repl = ReplicationManager(SUFFIX)
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    return repl.test_replication(supplier1, supplier2)

def test_ticket47676_init(topology_m2):
    """
        It adds
           - Objectclass with MAY 'member'
           - an entry ('bind_entry') with which we bind to test the 'SELFDN' operation
        It deletes the anonymous aci

    """

    topology_m2.ms["supplier1"].log.info("Add %s that allows 'member' attribute" % OC_NAME)
    new_oc = _oc_definition(OC_OID_EXT, OC_NAME, must=MUST, may=MAY)
    topology_m2.ms["supplier1"].schema.add_schema('objectClasses', new_oc)

    # entry used to bind with
    topology_m2.ms["supplier1"].log.info("Add %s" % BIND_DN)
    topology_m2.ms["supplier1"].add_s(Entry((BIND_DN, {
        'objectclass': "top person".split(),
        'sn': BIND_NAME,
        'cn': BIND_NAME,
        'userpassword': BIND_PW})))

    # enable acl error logging
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', ensure_bytes(str(128 + 8192)))]  # ACL + REPL
    topology_m2.ms["supplier1"].modify_s(DN_CONFIG, mod)
    topology_m2.ms["supplier2"].modify_s(DN_CONFIG, mod)

    # add dummy entries
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology_m2.ms["supplier1"].add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))


def test_ticket47676_skip_oc_at(topology_m2):
    '''
        This test ADD an entry on SUPPLIER1 where 47676 is fixed. Then it checks that entry is replicated
        on SUPPLIER2 (even if on SUPPLIER2 47676 is NOT fixed). Then update on SUPPLIER2.
        If the schema has successfully been pushed, updating Supplier2 should succeed
    '''
    topology_m2.ms["supplier1"].log.info("\n\n######################### ADD ######################\n")

    # bind as 'cn=Directory manager'
    topology_m2.ms["supplier1"].log.info("Bind as %s and add the add the entry with specific oc" % DN_DM)
    topology_m2.ms["supplier1"].simple_bind_s(DN_DM, PASSWORD)

    # Prepare the entry with multivalued members
    entry = Entry(ENTRY_DN)
    entry.setValues('objectclass', 'top', 'person', 'OCticket47676')
    entry.setValues('sn', ENTRY_NAME)
    entry.setValues('cn', ENTRY_NAME)
    entry.setValues('postalAddress', 'here')
    entry.setValues('postalCode', '1234')
    members = []
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        members.append("cn=%s,%s" % (name, SUFFIX))
    members.append(BIND_DN)
    entry.setValues('member', members)

    topology_m2.ms["supplier1"].log.info("Try to add Add  %s should be successful" % ENTRY_DN)
    topology_m2.ms["supplier1"].add_s(entry)

    #
    # Now check the entry as been replicated
    #
    topology_m2.ms["supplier2"].simple_bind_s(DN_DM, PASSWORD)
    topology_m2.ms["supplier1"].log.info("Try to retrieve %s from Supplier2" % ENTRY_DN)
    replication_check(topology_m2)
    ent = topology_m2.ms["supplier2"].getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent
    # Now update the entry on Supplier2 (as DM because 47676 is possibly not fixed on M2)
    topology_m2.ms["supplier1"].log.info("Update  %s on M2" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'description', b'test_add')]
    topology_m2.ms["supplier2"].modify_s(ENTRY_DN, mod)

    topology_m2.ms["supplier1"].simple_bind_s(DN_DM, PASSWORD)
    replication_check(topology_m2)
    ent = topology_m2.ms["supplier1"].getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ensure_str(ent.getValue('description')) == 'test_add'


def test_ticket47676_reject_action(topology_m2):
    topology_m2.ms["supplier1"].log.info("\n\n######################### REJECT ACTION ######################\n")

    topology_m2.ms["supplier1"].simple_bind_s(DN_DM, PASSWORD)
    topology_m2.ms["supplier2"].simple_bind_s(DN_DM, PASSWORD)

    # make supplier1 to refuse to push the schema if OC_NAME is present in consumer schema
    mod = [(ldap.MOD_ADD, 'schemaUpdateObjectclassReject', ensure_bytes('%s' % (OC_NAME)))]  # ACL + REPL
    topology_m2.ms["supplier1"].modify_s(REPL_SCHEMA_POLICY_SUPPLIER, mod)

    # Restart is required to take into account that policy
    topology_m2.ms["supplier1"].stop(timeout=10)
    topology_m2.ms["supplier1"].start(timeout=10)

    # Add a new OC on M1 so that schema CSN will change and M1 will try to push the schema
    topology_m2.ms["supplier1"].log.info("Add %s on M1" % OC2_NAME)
    new_oc = _oc_definition(OC2_OID_EXT, OC2_NAME, must=MUST, may=MAY)
    topology_m2.ms["supplier1"].schema.add_schema('objectClasses', new_oc)

    # Safety checking that the schema has been updated on M1
    topology_m2.ms["supplier1"].log.info("Check %s is in M1" % OC2_NAME)
    ent = topology_m2.ms["supplier1"].getEntry(SCHEMA_DN, ldap.SCOPE_BASE, "(objectclass=*)", ["objectclasses"])
    assert ent.hasAttr('objectclasses')
    found = False
    for objectclass in ent.getValues('objectclasses'):
        if str(objectclass).find(OC2_NAME) >= 0:
            found = True
            break
    assert found

    # Do an update of M1 so that M1 will try to push the schema
    topology_m2.ms["supplier1"].log.info("Update  %s on M1" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'description', b'test_reject')]
    topology_m2.ms["supplier1"].modify_s(ENTRY_DN, mod)

    # Check the replication occured and so also M1 attempted to push the schema
    topology_m2.ms["supplier1"].log.info("Check updated %s on M2" % ENTRY_DN)

    replication_check(topology_m2)
    ent = topology_m2.ms["supplier2"].getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
    assert ensure_str(ent.getValue('description')) == 'test_reject'

    # Check that the schema has not been pushed
    topology_m2.ms["supplier1"].log.info("Check %s is not in M2" % OC2_NAME)
    ent = topology_m2.ms["supplier2"].getEntry(SCHEMA_DN, ldap.SCOPE_BASE, "(objectclass=*)", ["objectclasses"])
    assert ent.hasAttr('objectclasses')
    found = False
    for objectclass in ent.getValues('objectclasses'):
        if str(objectclass).find(OC2_NAME) >= 0:
            found = True
            break
    assert not found

    topology_m2.ms["supplier1"].log.info("\n\n######################### NO MORE REJECT ACTION ######################\n")

    # make supplier1 to do no specific action on OC_NAME
    mod = [(ldap.MOD_DELETE, 'schemaUpdateObjectclassReject', ensure_bytes('%s' % (OC_NAME)))]  # ACL + REPL
    topology_m2.ms["supplier1"].modify_s(REPL_SCHEMA_POLICY_SUPPLIER, mod)

    # Restart is required to take into account that policy
    topology_m2.ms["supplier1"].stop(timeout=10)
    topology_m2.ms["supplier1"].start(timeout=10)

    # Do an update of M1 so that M1 will try to push the schema
    topology_m2.ms["supplier1"].log.info("Update  %s on M1" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'description', b'test_no_more_reject')]
    topology_m2.ms["supplier1"].modify_s(ENTRY_DN, mod)

    # Check the replication occured and so also M1 attempted to push the schema
    topology_m2.ms["supplier1"].log.info("Check updated %s on M2" % ENTRY_DN)

    replication_check(topology_m2)
    ent = topology_m2.ms["supplier2"].getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
    assert ensure_str(ent.getValue('description')) == 'test_no_more_reject'
    # Check that the schema has been pushed
    topology_m2.ms["supplier1"].log.info("Check %s is in M2" % OC2_NAME)
    ent = topology_m2.ms["supplier2"].getEntry(SCHEMA_DN, ldap.SCOPE_BASE, "(objectclass=*)", ["objectclasses"])
    assert ent.hasAttr('objectclasses')
    found = False
    for objectclass in ent.getValues('objectclasses'):
        if str(objectclass).find(OC2_NAME) >= 0:
            found = True
            break
    assert found


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
