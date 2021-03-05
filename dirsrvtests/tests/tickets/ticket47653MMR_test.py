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

logging.getLogger(__name__).setLevel(logging.DEBUG)
from lib389.utils import *

# Skip on older versions
pytestmark =[pytest.mark.tier2,
             pytest.mark.skipif(ds_is_older('1.3.2'), reason="Not implemented")]
log = logging.getLogger(__name__)

DEBUGGING = os.getenv("DEBUGGING", default=False)

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
OC_NAME = 'OCticket47653'
MUST = "(postalAddress $ postalCode)"
MAY = "(member $ street)"

OTHER_NAME = 'other_entry'
MAX_OTHERS = 10

BIND_NAME = 'bind_entry'
BIND_DN = 'cn=%s, %s' % (BIND_NAME, SUFFIX)
BIND_PW = 'password'

ENTRY_NAME = 'test_entry'
ENTRY_DN = 'cn=%s, %s' % (ENTRY_NAME, SUFFIX)
ENTRY_OC = "top person %s" % OC_NAME


def _oc_definition(oid_ext, name, must=None, may=None):
    oid = "1.2.3.4.5.6.7.8.9.10.%d" % oid_ext
    desc = 'To test ticket 47490'
    sup = 'person'
    if not must:
        must = MUST
    if not may:
        may = MAY

    new_oc = "( %s  NAME '%s' DESC '%s' SUP %s AUXILIARY MUST %s MAY %s )" % (oid, name, desc, sup, must, may)
    return ensure_bytes(new_oc)


def test_ticket47653_init(topology_m2):
    """
        It adds
           - Objectclass with MAY 'member'
           - an entry ('bind_entry') with which we bind to test the 'SELFDN' operation
        It deletes the anonymous aci

    """

    topology_m2.ms["supplier1"].log.info("Add %s that allows 'member' attribute" % OC_NAME)
    new_oc = _oc_definition(2, OC_NAME, must=MUST, may=MAY)
    topology_m2.ms["supplier1"].schema.add_schema('objectClasses', new_oc)

    # entry used to bind with
    topology_m2.ms["supplier1"].log.info("Add %s" % BIND_DN)
    topology_m2.ms["supplier1"].add_s(Entry((BIND_DN, {
        'objectclass': "top person".split(),
        'sn': BIND_NAME,
        'cn': BIND_NAME,
        'userpassword': BIND_PW})))

    if DEBUGGING:
        # enable acl error logging
        mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', ensure_bytes(str(128 + 8192)))]  # ACL + REPL
        topology_m2.ms["supplier1"].modify_s(DN_CONFIG, mod)
        topology_m2.ms["supplier2"].modify_s(DN_CONFIG, mod)

    # remove all aci's and start with a clean slate
    mod = [(ldap.MOD_DELETE, 'aci', None)]
    topology_m2.ms["supplier1"].modify_s(SUFFIX, mod)
    topology_m2.ms["supplier2"].modify_s(SUFFIX, mod)

    # add dummy entries
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology_m2.ms["supplier1"].add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))


def test_ticket47653_add(topology_m2):
    '''
        This test ADD an entry on SUPPLIER1 where 47653 is fixed. Then it checks that entry is replicated
        on SUPPLIER2 (even if on SUPPLIER2 47653 is NOT fixed). Then update on SUPPLIER2 and check the update on SUPPLIER1

        It checks that, bound as bind_entry,
            - we can not ADD an entry without the proper SELFDN aci.
            - with the proper ACI we can not ADD with 'member' attribute
            - with the proper ACI and 'member' it succeeds to ADD
    '''
    topology_m2.ms["supplier1"].log.info("\n\n######################### ADD ######################\n")

    # bind as bind_entry
    topology_m2.ms["supplier1"].log.info("Bind as %s" % BIND_DN)
    topology_m2.ms["supplier1"].simple_bind_s(BIND_DN, BIND_PW)

    # Prepare the entry with multivalued members
    entry_with_members = Entry(ENTRY_DN)
    entry_with_members.setValues('objectclass', 'top', 'person', 'OCticket47653')
    entry_with_members.setValues('sn', ENTRY_NAME)
    entry_with_members.setValues('cn', ENTRY_NAME)
    entry_with_members.setValues('postalAddress', 'here')
    entry_with_members.setValues('postalCode', '1234')
    members = []
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        members.append("cn=%s,%s" % (name, SUFFIX))
    members.append(BIND_DN)
    entry_with_members.setValues('member', members)

    # Prepare the entry with only one member value
    entry_with_member = Entry(ENTRY_DN)
    entry_with_member.setValues('objectclass', 'top', 'person', 'OCticket47653')
    entry_with_member.setValues('sn', ENTRY_NAME)
    entry_with_member.setValues('cn', ENTRY_NAME)
    entry_with_member.setValues('postalAddress', 'here')
    entry_with_member.setValues('postalCode', '1234')
    member = []
    member.append(BIND_DN)
    entry_with_member.setValues('member', member)

    # entry to add WITH member being BIND_DN but WITHOUT the ACI -> ldap.INSUFFICIENT_ACCESS
    try:
        topology_m2.ms["supplier1"].log.info("Try to add Add  %s (aci is missing): %r" % (ENTRY_DN, entry_with_member))

        topology_m2.ms["supplier1"].add_s(entry_with_member)
    except Exception as e:
        topology_m2.ms["supplier1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # Ok Now add the proper ACI
    topology_m2.ms["supplier1"].log.info("Bind as %s and add the ADD SELFDN aci" % DN_DM)
    topology_m2.ms["supplier1"].simple_bind_s(DN_DM, PASSWORD)

    ACI_TARGET = "(target = \"ldap:///cn=*,%s\")" % SUFFIX
    ACI_TARGETFILTER = "(targetfilter =\"(objectClass=%s)\")" % OC_NAME
    ACI_ALLOW = "(version 3.0; acl \"SelfDN add\"; allow (add)"
    ACI_SUBJECT = " userattr = \"member#selfDN\";)"
    ACI_BODY = ACI_TARGET + ACI_TARGETFILTER + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ensure_bytes(ACI_BODY))]
    topology_m2.ms["supplier1"].modify_s(SUFFIX, mod)
    time.sleep(1)

    # bind as bind_entry
    topology_m2.ms["supplier1"].log.info("Bind as %s" % BIND_DN)
    topology_m2.ms["supplier1"].simple_bind_s(BIND_DN, BIND_PW)

    # entry to add WITHOUT member and WITH the ACI -> ldap.INSUFFICIENT_ACCESS
    try:
        topology_m2.ms["supplier1"].log.info("Try to add Add  %s (member is missing)" % ENTRY_DN)
        topology_m2.ms["supplier1"].add_s(Entry((ENTRY_DN, {
            'objectclass': ENTRY_OC.split(),
            'sn': ENTRY_NAME,
            'cn': ENTRY_NAME,
            'postalAddress': 'here',
            'postalCode': '1234'})))
    except Exception as e:
        topology_m2.ms["supplier1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    time.sleep(1)

    # entry to add WITH memberS and WITH the ACI -> ldap.INSUFFICIENT_ACCESS
    # member should contain only one value
    try:
        topology_m2.ms["supplier1"].log.info("Try to add Add  %s (with several member values)" % ENTRY_DN)
        topology_m2.ms["supplier1"].add_s(entry_with_members)
    except Exception as e:
        topology_m2.ms["supplier1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)
    time.sleep(2)

    topology_m2.ms["supplier1"].log.info("Try to add Add  %s should be successful" % ENTRY_DN)
    try:
        topology_m2.ms["supplier1"].add_s(entry_with_member)
    except ldap.LDAPError as e:
        topology_m2.ms["supplier1"].log.info("Failed to add entry,  error: " + e.message['desc'])
        assert False

    #
    # Now check the entry as been replicated
    #
    topology_m2.ms["supplier2"].simple_bind_s(DN_DM, PASSWORD)
    topology_m2.ms["supplier1"].log.info("Try to retrieve %s from Supplier2" % ENTRY_DN)
    loop = 0
    while loop <= 10:
        try:
            ent = topology_m2.ms["supplier2"].getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    assert loop <= 10

    # Now update the entry on Supplier2 (as DM because 47653 is possibly not fixed on M2)
    topology_m2.ms["supplier1"].log.info("Update  %s on M2" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'description', b'test_add')]
    topology_m2.ms["supplier2"].modify_s(ENTRY_DN, mod)
    time.sleep(1)

    topology_m2.ms["supplier1"].simple_bind_s(DN_DM, PASSWORD)
    loop = 0
    while loop <= 10:
        try:
            ent = topology_m2.ms["supplier1"].getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            if ent.hasAttr('description') and (ensure_str(ent.getValue('description')) == 'test_add'):
                break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1

    assert ensure_str(ent.getValue('description')) == 'test_add'


def test_ticket47653_modify(topology_m2):
    '''
        This test MOD an entry on SUPPLIER1 where 47653 is fixed. Then it checks that update is replicated
        on SUPPLIER2 (even if on SUPPLIER2 47653 is NOT fixed). Then update on SUPPLIER2 (bound as BIND_DN).
        This update may fail whether or not 47653 is fixed on SUPPLIER2

        It checks that, bound as bind_entry,
            - we can not modify an entry without the proper SELFDN aci.
            - adding the ACI, we can modify the entry
    '''
    # bind as bind_entry
    topology_m2.ms["supplier1"].log.info("Bind as %s" % BIND_DN)
    topology_m2.ms["supplier1"].simple_bind_s(BIND_DN, BIND_PW)

    topology_m2.ms["supplier1"].log.info("\n\n######################### MODIFY ######################\n")

    # entry to modify WITH member being BIND_DN but WITHOUT the ACI -> ldap.INSUFFICIENT_ACCESS
    try:
        topology_m2.ms["supplier1"].log.info("Try to modify  %s (aci is missing)" % ENTRY_DN)
        mod = [(ldap.MOD_REPLACE, 'postalCode', b'9876')]
        topology_m2.ms["supplier1"].modify_s(ENTRY_DN, mod)
    except Exception as e:
        topology_m2.ms["supplier1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # Ok Now add the proper ACI
    topology_m2.ms["supplier1"].log.info("Bind as %s and add the WRITE SELFDN aci" % DN_DM)
    topology_m2.ms["supplier1"].simple_bind_s(DN_DM, PASSWORD)

    ACI_TARGET = "(target = \"ldap:///cn=*,%s\")" % SUFFIX
    ACI_TARGETATTR = "(targetattr = *)"
    ACI_TARGETFILTER = "(targetfilter =\"(objectClass=%s)\")" % OC_NAME
    ACI_ALLOW = "(version 3.0; acl \"SelfDN write\"; allow (write)"
    ACI_SUBJECT = " userattr = \"member#selfDN\";)"
    ACI_BODY = ACI_TARGET + ACI_TARGETATTR + ACI_TARGETFILTER + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ensure_bytes(ACI_BODY))]
    topology_m2.ms["supplier1"].modify_s(SUFFIX, mod)
    time.sleep(2)

    # bind as bind_entry
    topology_m2.ms["supplier1"].log.info("M1: Bind as %s" % BIND_DN)
    topology_m2.ms["supplier1"].simple_bind_s(BIND_DN, BIND_PW)
    time.sleep(1)

    # modify the entry and checks the value
    topology_m2.ms["supplier1"].log.info("M1: Try to modify  %s. It should succeeds" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'postalCode', b'1928')]
    topology_m2.ms["supplier1"].modify_s(ENTRY_DN, mod)

    topology_m2.ms["supplier1"].log.info("M1: Bind as %s" % DN_DM)
    topology_m2.ms["supplier1"].simple_bind_s(DN_DM, PASSWORD)

    topology_m2.ms["supplier1"].log.info("M1: Check the update of %s" % ENTRY_DN)
    ents = topology_m2.ms["supplier1"].search_s(ENTRY_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 1
    assert ensure_str(ents[0].postalCode) == '1928'

    # Now check the update has been replicated on M2
    topology_m2.ms["supplier1"].log.info("M2: Bind as %s" % DN_DM)
    topology_m2.ms["supplier2"].simple_bind_s(DN_DM, PASSWORD)
    topology_m2.ms["supplier1"].log.info("M2: Try to retrieve %s" % ENTRY_DN)
    loop = 0
    while loop <= 10:
        try:
            ent = topology_m2.ms["supplier2"].getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            if ent.hasAttr('postalCode') and (ensure_str(ent.getValue('postalCode')) == '1928'):
                break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    assert loop <= 10
    assert ensure_str(ent.getValue('postalCode')) == '1928'

    # Now update the entry on Supplier2 bound as BIND_DN (update may fail if  47653 is  not fixed on M2)
    topology_m2.ms["supplier1"].log.info("M2: Update  %s (bound as %s)" % (ENTRY_DN, BIND_DN))
    topology_m2.ms["supplier2"].simple_bind_s(BIND_DN, PASSWORD)
    time.sleep(1)
    fail = False
    try:
        mod = [(ldap.MOD_REPLACE, 'postalCode', b'1929')]
        topology_m2.ms["supplier2"].modify_s(ENTRY_DN, mod)
        fail = False
    except ldap.INSUFFICIENT_ACCESS:
        topology_m2.ms["supplier1"].log.info(
            "M2: Exception (INSUFFICIENT_ACCESS): that is fine the bug is possibly not fixed on M2")
        fail = True
    except Exception as e:
        topology_m2.ms["supplier1"].log.info("M2: Exception (not expected): %s" % type(e).__name__)
        assert 0

    if not fail:
        # Check the update has been replicaed on M1
        topology_m2.ms["supplier1"].log.info("M1: Bind as %s" % DN_DM)
        topology_m2.ms["supplier1"].simple_bind_s(DN_DM, PASSWORD)
        topology_m2.ms["supplier1"].log.info("M1: Check %s.postalCode=1929)" % (ENTRY_DN))
        loop = 0
        while loop <= 10:
            try:
                ent = topology_m2.ms["supplier1"].getEntry(ENTRY_DN, ldap.SCOPE_BASE, "(objectclass=*)")
                if ent.hasAttr('postalCode') and (ensure_str(ent.getValue('postalCode')) == '1929'):
                    break
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                loop += 1
        assert ensure_str(ent.getValue('postalCode')) == '1929'


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
