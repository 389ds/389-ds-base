# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st

log = logging.getLogger(__name__)

from lib389.utils import *

# Skip on older versions
pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.3.2'), reason="Not implemented")]
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


@pytest.fixture(scope="module")
def allow_user_init(topology_st):
    """Initialize the test environment

     """
    topology_st.standalone.log.info("Add %s that allows 'member' attribute" % OC_NAME)
    new_oc = _oc_definition(2, OC_NAME, must=MUST, may=MAY)
    topology_st.standalone.schema.add_schema('objectClasses', new_oc)

    # entry used to bind with
    topology_st.standalone.log.info("Add %s" % BIND_DN)
    topology_st.standalone.add_s(Entry((BIND_DN, {
        'objectclass': "top person".split(),
        'sn': BIND_NAME,
        'cn': BIND_NAME,
        'userpassword': BIND_PW})))

    # enable acl error logging
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'128')]
    topology_st.standalone.modify_s(DN_CONFIG, mod)

    # Remove aci's to start with a clean slate
    mod = [(ldap.MOD_DELETE, 'aci', None)]
    topology_st.standalone.modify_s(SUFFIX, mod)

    # add dummy entries
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology_st.standalone.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))


@pytest.mark.ds47653
def test_selfdn_permission_add(topology_st, allow_user_init):
    """Check add entry operation with and without SelfDN aci

    :id: e837a9ef-be92-48da-ad8b-ebf42b0fede1
    :setup: Standalone instance, add a entry which is used to bind,
            enable acl error logging by setting 'nsslapd-errorlog-level' to '128',
            remove aci's to start with a clean slate, and add dummy entries
    :steps:
        1. Check we can not ADD an entry without the proper SELFDN aci
        2. Check with the proper ACI we can not ADD with 'member' attribute
        3. Check entry to add with memberS and with the ACI
        4. Check with the proper ACI and 'member' it succeeds to ADD
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should fail with Insufficient Access
        4. Operation should be successful
     """
    topology_st.standalone.log.info("\n\n######################### ADD ######################\n")

    # bind as bind_entry
    topology_st.standalone.log.info("Bind as %s" % BIND_DN)
    topology_st.standalone.simple_bind_s(BIND_DN, BIND_PW)

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

    # Prepare the entry with one member
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
        topology_st.standalone.log.info("Try to add Add  %s (aci is missing): %r" % (ENTRY_DN, entry_with_member))

        topology_st.standalone.add_s(entry_with_member)
    except Exception as e:
        topology_st.standalone.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # Ok Now add the proper ACI
    topology_st.standalone.log.info("Bind as %s and add the ADD SELFDN aci" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    ACI_TARGET = "(target = \"ldap:///cn=*,%s\")" % SUFFIX
    ACI_TARGETFILTER = "(targetfilter =\"(objectClass=%s)\")" % OC_NAME
    ACI_ALLOW = "(version 3.0; acl \"SelfDN add\"; allow (add)"
    ACI_SUBJECT = " userattr = \"member#selfDN\";)"
    ACI_BODY = ACI_TARGET + ACI_TARGETFILTER + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ensure_bytes(ACI_BODY))]
    topology_st.standalone.modify_s(SUFFIX, mod)

    # bind as bind_entry
    topology_st.standalone.log.info("Bind as %s" % BIND_DN)
    topology_st.standalone.simple_bind_s(BIND_DN, BIND_PW)

    # entry to add WITHOUT member and WITH the ACI -> ldap.INSUFFICIENT_ACCESS
    try:
        topology_st.standalone.log.info("Try to add Add  %s (member is missing)" % ENTRY_DN)
        topology_st.standalone.add_s(Entry((ENTRY_DN, {
            'objectclass': ENTRY_OC.split(),
            'sn': ENTRY_NAME,
            'cn': ENTRY_NAME,
            'postalAddress': 'here',
            'postalCode': '1234'})))
    except Exception as e:
        topology_st.standalone.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # entry to add WITH memberS and WITH the ACI -> ldap.INSUFFICIENT_ACCESS
    # member should contain only one value
    try:
        topology_st.standalone.log.info("Try to add Add  %s (with several member values)" % ENTRY_DN)
        topology_st.standalone.add_s(entry_with_members)
    except Exception as e:
        topology_st.standalone.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    topology_st.standalone.log.info("Try to add Add  %s should be successful" % ENTRY_DN)
    topology_st.standalone.add_s(entry_with_member)


@pytest.mark.ds47653
def test_selfdn_permission_search(topology_st, allow_user_init):
    """Check search operation with and without SelfDN aci

    :id: 06d51ef9-c675-4583-99b2-4852dbda190e
    :setup: Standalone instance, add a entry which is used to bind,
            enable acl error logging by setting 'nsslapd-errorlog-level' to '128',
            remove aci's to start with a clean slate, and add dummy entries
    :steps:
        1. Check we can not search an entry without the proper SELFDN aci
        2. Add proper ACI
        3. Check we can search with the proper ACI
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
     """
    topology_st.standalone.log.info("\n\n######################### SEARCH ######################\n")
    # bind as bind_entry
    topology_st.standalone.log.info("Bind as %s" % BIND_DN)
    topology_st.standalone.simple_bind_s(BIND_DN, BIND_PW)

    # entry to search WITH member being BIND_DN but WITHOUT the ACI -> no entry returned
    topology_st.standalone.log.info("Try to search  %s (aci is missing)" % ENTRY_DN)
    ents = topology_st.standalone.search_s(ENTRY_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 0

    # Ok Now add the proper ACI
    topology_st.standalone.log.info("Bind as %s and add the READ/SEARCH SELFDN aci" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    ACI_TARGET = "(target = \"ldap:///cn=*,%s\")" % SUFFIX
    ACI_TARGETATTR = "(targetattr = *)"
    ACI_TARGETFILTER = "(targetfilter =\"(objectClass=%s)\")" % OC_NAME
    ACI_ALLOW = "(version 3.0; acl \"SelfDN search-read\"; allow (read, search, compare)"
    ACI_SUBJECT = " userattr = \"member#selfDN\";)"
    ACI_BODY = ACI_TARGET + ACI_TARGETATTR + ACI_TARGETFILTER + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ensure_bytes(ACI_BODY))]
    topology_st.standalone.modify_s(SUFFIX, mod)

    # bind as bind_entry
    topology_st.standalone.log.info("Bind as %s" % BIND_DN)
    topology_st.standalone.simple_bind_s(BIND_DN, BIND_PW)

    # entry to search with the proper aci
    topology_st.standalone.log.info("Try to search  %s should be successful" % ENTRY_DN)
    ents = topology_st.standalone.search_s(ENTRY_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 1


@pytest.mark.ds47653
def test_selfdn_permission_modify(topology_st, allow_user_init):
    """Check modify operation with and without SelfDN aci

    :id: 97a58844-095f-44b0-9029-dd29a7d83d68
    :setup: Standalone instance, add a entry which is used to bind,
            enable acl error logging by setting 'nsslapd-errorlog-level' to '128',
            remove aci's to start with a clean slate, and add dummy entries
    :steps:
        1. Check we can not modify an entry without the proper SELFDN aci
        2. Add proper ACI
        3. Modify the entry and check the modified value
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
     """
    # bind as bind_entry
    topology_st.standalone.log.info("Bind as %s" % BIND_DN)
    topology_st.standalone.simple_bind_s(BIND_DN, BIND_PW)

    topology_st.standalone.log.info("\n\n######################### MODIFY ######################\n")

    # entry to modify WITH member being BIND_DN but WITHOUT the ACI -> ldap.INSUFFICIENT_ACCESS
    try:
        topology_st.standalone.log.info("Try to modify  %s (aci is missing)" % ENTRY_DN)
        mod = [(ldap.MOD_REPLACE, 'postalCode', b'9876')]
        topology_st.standalone.modify_s(ENTRY_DN, mod)
    except Exception as e:
        topology_st.standalone.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # Ok Now add the proper ACI
    topology_st.standalone.log.info("Bind as %s and add the WRITE SELFDN aci" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    ACI_TARGET = "(target = \"ldap:///cn=*,%s\")" % SUFFIX
    ACI_TARGETATTR = "(targetattr = *)"
    ACI_TARGETFILTER = "(targetfilter =\"(objectClass=%s)\")" % OC_NAME
    ACI_ALLOW = "(version 3.0; acl \"SelfDN write\"; allow (write)"
    ACI_SUBJECT = " userattr = \"member#selfDN\";)"
    ACI_BODY = ACI_TARGET + ACI_TARGETATTR + ACI_TARGETFILTER + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ensure_bytes(ACI_BODY))]
    topology_st.standalone.modify_s(SUFFIX, mod)

    # bind as bind_entry
    topology_st.standalone.log.info("Bind as %s" % BIND_DN)
    topology_st.standalone.simple_bind_s(BIND_DN, BIND_PW)

    # modify the entry and checks the value
    topology_st.standalone.log.info("Try to modify  %s. It should succeeds" % ENTRY_DN)
    mod = [(ldap.MOD_REPLACE, 'postalCode', b'1928')]
    topology_st.standalone.modify_s(ENTRY_DN, mod)

    ents = topology_st.standalone.search_s(ENTRY_DN, ldap.SCOPE_BASE, 'objectclass=*')
    assert len(ents) == 1
    assert ensure_str(ents[0].postalCode) == '1928'


@pytest.mark.ds47653
def test_selfdn_permission_delete(topology_st, allow_user_init):
    """Check delete operation with and without SelfDN aci

    :id: 0ec4c0ec-e7b0-4ef1-8373-ab25aae34516
    :setup: Standalone instance, add a entry which is used to bind,
            enable acl error logging by setting 'nsslapd-errorlog-level' to '128',
            remove aci's to start with a clean slate, and add dummy entries
    :steps:
        1. Check we can not delete an entry without the proper SELFDN aci
        2. Add proper ACI
        3. Check we can perform delete operation with proper ACI
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
     """
    topology_st.standalone.log.info("\n\n######################### DELETE ######################\n")

    # bind as bind_entry
    topology_st.standalone.log.info("Bind as %s" % BIND_DN)
    topology_st.standalone.simple_bind_s(BIND_DN, BIND_PW)

    # entry to delete WITH member being BIND_DN but WITHOUT the ACI -> ldap.INSUFFICIENT_ACCESS
    try:
        topology_st.standalone.log.info("Try to delete  %s (aci is missing)" % ENTRY_DN)
        topology_st.standalone.delete_s(ENTRY_DN)
    except Exception as e:
        topology_st.standalone.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # Ok Now add the proper ACI
    topology_st.standalone.log.info("Bind as %s and add the READ/SEARCH SELFDN aci" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    ACI_TARGET = "(target = \"ldap:///cn=*,%s\")" % SUFFIX
    ACI_TARGETFILTER = "(targetfilter =\"(objectClass=%s)\")" % OC_NAME
    ACI_ALLOW = "(version 3.0; acl \"SelfDN delete\"; allow (delete)"
    ACI_SUBJECT = " userattr = \"member#selfDN\";)"
    ACI_BODY = ACI_TARGET + ACI_TARGETFILTER + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ensure_bytes(ACI_BODY))]
    topology_st.standalone.modify_s(SUFFIX, mod)

    # bind as bind_entry
    topology_st.standalone.log.info("Bind as %s" % BIND_DN)
    topology_st.standalone.simple_bind_s(BIND_DN, BIND_PW)

    # entry to delete with the proper aci
    topology_st.standalone.log.info("Try to delete  %s should be successful" % ENTRY_DN)
    topology_st.standalone.delete_s(ENTRY_DN)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
