# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
import time
from lib389.dirsrv_log import DirsrvAccessLog
from lib389.tasks import *
from lib389.backend import Backends, Backend
from lib389.dbgen import dbgen_users, dbgen_groups
from lib389.topologies import topology_st
from lib389._constants import PASSWORD, DEFAULT_SUFFIX, DN_DM, SUFFIX, DN_CONFIG_LDBM
from lib389.utils import *

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

ENTRY_NAME = 'test_entry'


@pytest.mark.bz918686
@pytest.mark.ds497
def test_filter_escaped(topology_st):
    """Test we can search for an '*' in a attribute value.

    :id: 5c9aa40c-c641-4603-bce3-b19f4c1f2031
    :setup: Standalone instance
    :steps:
         1. Add a test user with an '*' in its attribute value
            i.e. 'cn=test * me'
         2. Add another similar test user without '*' in its attribute value
         3. Search test user using search filter "cn=*\\**"
    :expectedresults:
         1. This should pass
         2. This should pass
         3. Test user with 'cn=test * me' only, should be listed
    """
    log.info('Running test_filter_escaped...')

    USER1_DN = 'uid=test_entry,' + DEFAULT_SUFFIX
    USER2_DN = 'uid=test_entry2,' + DEFAULT_SUFFIX

    try:
        topology_st.standalone.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '1',
                                                       'cn': 'test * me',
                                                       'uid': 'test_entry',
                                                       'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_filter_escaped: Failed to add test user ' + USER1_DN + ': error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '2',
                                                       'cn': 'test me',
                                                       'uid': 'test_entry2',
                                                       'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_filter_escaped: Failed to add test user ' + USER2_DN + ': error ' + e.message['desc'])
        assert False

    try:
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'cn=*\\**')
        if not entry or len(entry) > 1:
            log.fatal('test_filter_escaped: Entry was not found using "cn=*\\**"')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_filter_escaped: Failed to search for user(%s), error: %s' %
                  (USER1_DN, e.message('desc')))
        assert False

    log.info('test_filter_escaped: PASSED')


def test_filter_search_original_attrs(topology_st):
    """Search and request attributes with extra characters. The returned entry
      should not have these extra characters: objectclass EXTRA"

    :id: d30d8a1c-84ac-47ba-95f9-41e3453fbf3a
    :setup: Standalone instance
    :steps:
         1. Execute a search operation for attributes with extra characters
         2. Check the search result have these extra characters or not
    :expectedresults:
         1. Search should pass
         2. Search result should not have these extra characters attribute
    """

    log.info('Running test_filter_search_original_attrs...')

    try:
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE,
                                                'objectclass=top', ['objectclass-EXTRA'])
        if entry[0].hasAttr('objectclass-EXTRA'):
            log.fatal('test_filter_search_original_attrs: Entry does not have the original attribute')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_filter_search_original_attrs: Failed to search suffix(%s), error: %s' %
                  (DEFAULT_SUFFIX, e.message('desc')))
        assert False

    log.info('test_filter_search_original_attrs: PASSED')

@pytest.mark.bz1511462
def test_filter_scope_one(topology_st):
    """Test ldapsearch with scope one gives only single entry

    :id: cf5a6078-bbe6-4d43-ac71-553c45923f91
    :setup: Standalone instance
    :steps:
         1. Search ou=services,dc=example,dc=com using ldapsearch with
            scope one using base as dc=example,dc=com
         2. Check that search should return only one entry
    :expectedresults:
         1. This should pass
         2. This should pass
    """

    log.info('Search user using ldapsearch with scope one')
    results = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_ONELEVEL,'ou=services',['ou'] )
    log.info(results)

    log.info('Search should only have one entry')
    assert len(results) == 1

@pytest.mark.ds47313
def test_filter_with_attribute_subtype(topology_st):
    """Adds 2 test entries and Search with
    filters including subtype and !

    :id: 0e69f5f2-6a0a-480e-8282-fbcc50231908
    :setup: Standalone instance
    :steps:
        1. Add 2 entries and create 3 filters
        2. Search for entry with filter: (&(cn=test_entry en only)(!(cn=test_entry fr)))
        3. Search for entry with filter: (&(cn=test_entry en only)(!(cn;fr=test_entry fr)))
        4. Search for entry with filter: (&(cn=test_entry en only)(!(cn;en=test_entry en)))
        5. Delete the added entries
    :expectedresults:
        1. Operation should be successful
        2. Search should be successful
        3. Search should be successful
        4. Search should not be successful
        5. Delete the added entries
    """

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # enable filter error logging
    # mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '32')]
    # topology_st.standalone.modify_s(DN_CONFIG, mod)

    topology_st.standalone.log.info("\n\n######################### ADD ######################\n")

    # Prepare the entry with cn;fr & cn;en
    entry_name_fr = '%s fr' % (ENTRY_NAME)
    entry_name_en = '%s en' % (ENTRY_NAME)
    entry_name_both = '%s both' % (ENTRY_NAME)
    entry_dn_both = 'cn=%s, %s' % (entry_name_both, SUFFIX)
    entry_both = Entry(entry_dn_both)
    entry_both.setValues('objectclass', 'top', 'person')
    entry_both.setValues('sn', entry_name_both)
    entry_both.setValues('cn', entry_name_both)
    entry_both.setValues('cn;fr', entry_name_fr)
    entry_both.setValues('cn;en', entry_name_en)

    # Prepare the entry with one member
    entry_name_en_only = '%s en only' % (ENTRY_NAME)
    entry_dn_en_only = 'cn=%s, %s' % (entry_name_en_only, SUFFIX)
    entry_en_only = Entry(entry_dn_en_only)
    entry_en_only.setValues('objectclass', 'top', 'person')
    entry_en_only.setValues('sn', entry_name_en_only)
    entry_en_only.setValues('cn', entry_name_en_only)
    entry_en_only.setValues('cn;en', entry_name_en)

    topology_st.standalone.log.info("Try to add Add %s: %r" % (entry_dn_both, entry_both))
    topology_st.standalone.add_s(entry_both)

    topology_st.standalone.log.info("Try to add Add %s: %r" % (entry_dn_en_only, entry_en_only))
    topology_st.standalone.add_s(entry_en_only)

    topology_st.standalone.log.info("\n\n######################### SEARCH ######################\n")

    # filter: (&(cn=test_entry en only)(!(cn=test_entry fr)))
    myfilter = '(&(sn=%s)(!(cn=%s)))' % (entry_name_en_only, entry_name_fr)
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1
    assert ensure_str(ents[0].sn) == entry_name_en_only
    topology_st.standalone.log.info("Found %s" % ents[0].dn)

    # filter: (&(cn=test_entry en only)(!(cn;fr=test_entry fr)))
    myfilter = '(&(sn=%s)(!(cn;fr=%s)))' % (entry_name_en_only, entry_name_fr)
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1
    assert ensure_str(ents[0].sn) == entry_name_en_only
    topology_st.standalone.log.info("Found %s" % ents[0].dn)

    # filter: (&(cn=test_entry en only)(!(cn;en=test_entry en)))
    myfilter = '(&(sn=%s)(!(cn;en=%s)))' % (entry_name_en_only, entry_name_en)
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 0
    topology_st.standalone.log.info("Found none")

    topology_st.standalone.log.info("\n\n######################### DELETE ######################\n")

    topology_st.standalone.log.info("Try to delete  %s " % entry_dn_both)
    topology_st.standalone.delete_s(entry_dn_both)

    topology_st.standalone.log.info("Try to delete  %s " % entry_dn_en_only)
    topology_st.standalone.delete_s(entry_dn_en_only)

    log.info('Testcase PASSED')

@pytest.mark.bz1615155
def test_extended_search(topology_st):
    """Test we can search with equality extended matching rule

    :id: 396942ac-467b-435b-8d9f-e80c7ec4ba6c
    :setup: Standalone instance
    :steps:
         1. Add a test user with 'sn: ext-test-entry'
         2. Search '(cn:de:=ext-test-entry)'
         3. Search '(sn:caseIgnoreIA5Match:=EXT-TEST-ENTRY)'
         4. Search '(sn:caseIgnoreMatch:=EXT-TEST-ENTRY)'
         5. Search '(sn:caseExactMatch:=EXT-TEST-ENTRY)'
         6. Search '(sn:caseExactMatch:=ext-test-entry)'
         7. Search '(sn:caseExactIA5Match:=EXT-TEST-ENTRY)'
         8. Search '(sn:caseExactIA5Match:=ext-test-entry)'
    :expectedresults:
         1. This should pass
         2. This should return one entry
         3. This should return one entry
         4. This should return one entry
         5. This should return NO entry
         6. This should return one entry
         7. This should return NO entry
         8. This should return one entry
    """
    log.info('Running test_filter_escaped...')

    ATTR_VAL = 'ext-test-entry'
    USER1_DN = "uid=%s,%s" % (ATTR_VAL, DEFAULT_SUFFIX)

    try:
        topology_st.standalone.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': ATTR_VAL.encode(),
                                                       'cn': ATTR_VAL.encode(),
                                                       'uid': ATTR_VAL.encode()})))
    except ldap.LDAPError as e:
        log.fatal('test_extended_search: Failed to add test user ' + USER1_DN + ': error ' +
                  e.message['desc'])
        assert False

    # filter: '(cn:de:=ext-test-entry)'
    myfilter = '(cn:de:=%s)' % ATTR_VAL
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1

    # filter: '(sn:caseIgnoreIA5Match:=EXT-TEST-ENTRY)'
    myfilter = '(cn:caseIgnoreIA5Match:=%s)' % ATTR_VAL.upper()
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1

    # filter: '(sn:caseIgnoreMatch:=EXT-TEST-ENTRY)'
    myfilter = '(cn:caseIgnoreMatch:=%s)' % ATTR_VAL.upper()
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1

    # filter: '(sn:caseExactMatch:=EXT-TEST-ENTRY)'
    myfilter = '(cn:caseExactMatch:=%s)' % ATTR_VAL.upper()
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 0

    # filter: '(sn:caseExactMatch:=ext-test-entry)'
    myfilter = '(cn:caseExactMatch:=%s)' % ATTR_VAL
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1

    # filter: '(sn:caseExactIA5Match:=EXT-TEST-ENTRY)'
    myfilter = '(cn:caseExactIA5Match:=%s)' % ATTR_VAL.upper()
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 0

    # filter: '(sn:caseExactIA5Match:=ext-test-entry)'
    myfilter = '(cn:caseExactIA5Match:=%s)' % ATTR_VAL
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1

def test_match_large_valueset(topology_st):
    """Test that when returning a big number of entries
    and that we need to match the filter from a large valueset
    we get benefit to use the sorted valueset

    :id: 7db5aa88-50e0-4c31-85dd-1d2072cb674c

    :setup: Standalone instance

    :steps:
         1. Create a users and groups backends and tune them
         2. Generate a test ldif (2k users and 1K groups with all users)
         3. Import test ldif file using Offline import (ldif2db).
         4. Prim the 'groups' entrycache with a "fast" search
         5. Search the 'groups' with a difficult matching value
         6. check that etime from step 5 is less than a second

    :expectedresults:
         1. Create a users and groups backends should PASS
         2. Generate LDIF should PASS.
         3. Offline import should PASS.
         4. Priming should PASS.
         5. Performance search should PASS.
         6. Etime of performance search should PASS.
    """

    log.info('Running test_match_large_valueset...')
    #
    # Test online/offline LDIF imports
    #
    inst = topology_st.standalone
    inst.start()
    backends = Backends(inst)
    users_suffix = "ou=users,%s" % DEFAULT_SUFFIX
    users_backend = 'users'
    users_ldif = 'users_import.ldif'
    groups_suffix = "ou=groups,%s" % DEFAULT_SUFFIX
    groups_backend = 'groups'
    groups_ldif = 'groups_import.ldif'
    groups_entrycache = '200000000'
    users_number = 2000
    groups_number = 1000


    # For priming the cache we just want to be fast
    # taking the first value in the valueset is good
    # whether the valueset is sorted or not
    priming_user_rdn = "user0001"

    # For performance testing, this is important to use
    # user1000 rather then user0001
    # Because user0001 is the first value in the valueset
    # whether we use the sorted valuearray or non sorted
    # valuearray the performance will be similar.
    # With middle value user1000, the performance boost of
    # the sorted valuearray will make the difference.
    perf_user_rdn = "user1000"

    # Step 1. Prepare the backends and tune the groups entrycache
    try:
        be_users = backends.create(properties={'parent': DEFAULT_SUFFIX, 'nsslapd-suffix': users_suffix, 'name': users_backend})
        be_groups = backends.create(properties={'parent': DEFAULT_SUFFIX, 'nsslapd-suffix': groups_suffix, 'name': groups_backend})

        # set the entry cache to 200Mb as the 1K groups of 2K users require at least 170Mb
        if get_default_db_lib() == "bdb":
            config_ldbm = DSLdapObject(inst, DN_CONFIG_LDBM)
            config_ldbm.set('nsslapd-cache-autosize', '0')
        be_groups.replace('nsslapd-cachememsize', groups_entrycache)
    except:
        raise

    # Step 2. Generate a test ldif (10k users entries)
    log.info("Generating users LDIF...")
    ldif_dir = inst.get_ldif_dir()
    users_import_ldif = "%s/%s" % (ldif_dir, users_ldif)
    groups_import_ldif = "%s/%s" % (ldif_dir, groups_ldif)
    dbgen_users(inst, users_number, users_import_ldif, suffix=users_suffix, generic=True, parent=users_suffix)

    # Generate a test ldif (800 groups with 10k members) that fit in 700Mb entry cache
    props = {
        "name": "group",
        "suffix": groups_suffix,
        "parent": groups_suffix,
        "number": groups_number,
        "numMembers": users_number,
        "createMembers": False,
        "memberParent": users_suffix,
        "membershipAttr": "uniquemember",
    }
    dbgen_groups(inst, groups_import_ldif, props)

    # Step 3. Do the both offline imports
    inst.stop()
    if not inst.ldif2db(users_backend, None, None, None, users_import_ldif):
        log.fatal('test_basic_import_export: Offline users import failed')
        assert False
    if not inst.ldif2db(groups_backend, None, None, None, groups_import_ldif):
        log.fatal('test_basic_import_export: Offline groups import failed')
        assert False
    inst.start()

    # Step 4. first prime the cache
    # Just request the 'DN'. We are interested by the time of matching not by the time of transfert
    entries = topology_st.standalone.search_s(groups_suffix, ldap.SCOPE_SUBTREE, "(&(objectclass=groupOfUniqueNames)(uniquemember=uid=%s,%s))" % (priming_user_rdn, users_suffix), ['dn'])
    assert len(entries) == groups_number

    # Step 5. Now do the real performance checking it should take less than a second
    # Just request the 'DN'. We are interested by the time of matching not by the time of transfert
    search_start = time.time()
    entries = topology_st.standalone.search_s(groups_suffix, ldap.SCOPE_SUBTREE, "(&(objectclass=groupOfUniqueNames)(uniquemember=uid=%s,%s))" % (perf_user_rdn, users_suffix), ['dn'])
    duration = time.time() - search_start
    log.info("Duration of the search was %f", duration)

    # Step 6. Gather the etime from the access log
    inst.stop()
    access_log = DirsrvAccessLog(inst)
    search_result = access_log.match(".*RESULT err=0 tag=101 nentries=%s.*" % groups_number)
    log.info("Found patterns are %s", search_result[0])
    log.info("Found patterns are %s", search_result[1])
    etime = float(search_result[1].split('etime=')[1])
    log.info("Duration of the search from access log was %f", etime)
    assert len(entries) == groups_number
    assert (etime < 5)

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
