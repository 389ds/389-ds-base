# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

"""
   :Requirement: Basic Directory Server Operations
"""

from subprocess import check_output, Popen
from lib389.idm.user import UserAccounts
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.dbgen import dbgen
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389._constants import DN_DM, PASSWORD, PW_DM
from lib389.topologies import topology_st

log = logging.getLogger(__name__)

# Globals
USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX
USER3_DN = 'uid=user3,' + DEFAULT_SUFFIX

ROOTDSE_DEF_ATTR_LIST = ('namingContexts',
                         'supportedLDAPVersion',
                         'supportedControl',
                         'supportedExtension',
                         'supportedSASLMechanisms',
                         'vendorName',
                         'vendorVersion')


@pytest.fixture(scope="module")
def import_example_ldif(topology_st):
    """Import the Example LDIF for the tests in this suite"""

    log.info('Initializing the "basic" test suite')

    ldif = '%s/dirsrv/data/Example.ldif' % topology_st.standalone.get_data_dir()
    import_ldif = topology_st.standalone.get_ldif_dir() + "/Example.ldif"
    shutil.copyfile(ldif, import_ldif)
    topology_st.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                            input_file=import_ldif,
                                            args={TASK_WAIT: True})


@pytest.fixture(params=ROOTDSE_DEF_ATTR_LIST)
def rootdse_attr(topology_st, request):
    """Adds an attr from the list
    as the default attr to the rootDSE
    """
    # Ensure the server is started and connected
    topology_st.standalone.start()

    RETURN_DEFAULT_OPATTR = "nsslapd-return-default-opattr"
    rootdse_attr_name = ensure_bytes(request.param)

    log.info("        Add the %s: %s to rootdse" % (RETURN_DEFAULT_OPATTR,
                                                    rootdse_attr_name))
    mod = [(ldap.MOD_ADD, RETURN_DEFAULT_OPATTR, rootdse_attr_name)]
    try:
        topology_st.standalone.modify_s("", mod)
    except ldap.LDAPError as e:
        log.fatal('Failed to add attr: error (%s)' % (e.message['desc']))
        assert False

    def fin():
        log.info("        Delete the %s: %s from rootdse" % (RETURN_DEFAULT_OPATTR,
                                                             rootdse_attr_name))
        mod = [(ldap.MOD_DELETE, RETURN_DEFAULT_OPATTR, rootdse_attr_name)]
        try:
            topology_st.standalone.modify_s("", mod)
        except ldap.LDAPError as e:
            log.fatal('Failed to delete attr: error (%s)' % (e.message['desc']))
            assert False

    request.addfinalizer(fin)

    return rootdse_attr_name


def test_basic_ops(topology_st, import_example_ldif):
    """Tests adds, mods, modrdns, and deletes operations

    :id: 33f97f55-60bf-46c7-b880-6c488517ae19

    :setup: Standalone instance

    :steps:
         1. Add 3 test users USER1, USER2 and USER3 to database
         2. Modify (ADD, REPLACE and DELETE) description for USER1 in database
         3. Rename USER1, USER2 and USER3 using Modrds
         4. Delete test entries USER1, USER2 and USER3

    :expectedresults:
         1. Add operation should PASS.
         2. Modify operations should PASS.
         3. Rename operations should PASS.
         4. Delete operations should PASS.
    """
    log.info('Running test_basic_ops...')
    USER1_NEWDN = 'cn=user1'
    USER2_NEWDN = 'cn=user2'
    USER3_NEWDN = 'cn=user3'
    NEW_SUPERIOR = 'ou=people,' + DEFAULT_SUFFIX
    USER1_RDN_DN = 'cn=user1,' + DEFAULT_SUFFIX
    USER2_RDN_DN = 'cn=user2,' + DEFAULT_SUFFIX
    USER3_RDN_DN = 'cn=user3,' + NEW_SUPERIOR  # New superior test

    #
    # Adds#
    try:
        topology_st.standalone.add_s(Entry((USER1_DN,
                                            {'objectclass': "top extensibleObject".split(),
                                             'sn': '1',
                                             'cn': 'user1',
                                             'uid': 'user1',
                                             'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.error('Failed to add test user' + USER1_DN + ': error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER2_DN,
                                            {'objectclass': "top extensibleObject".split(),
                                             'sn': '2',
                                             'cn': 'user2',
                                             'uid': 'user2',
                                             'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.error('Failed to add test user' + USER2_DN + ': error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER3_DN,
                                            {'objectclass': "top extensibleObject".split(),
                                             'sn': '3',
                                             'cn': 'user3',
                                             'uid': 'user3',
                                             'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.error('Failed to add test user' + USER3_DN + ': error ' + e.message['desc'])
        assert False

    #
    # Mods
    #
    try:
        topology_st.standalone.modify_s(USER1_DN, [(ldap.MOD_ADD, 'description',
                                                    b'New description')])
    except ldap.LDAPError as e:
        log.error('Failed to add description: error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'description',
                                                    b'Modified description')])
    except ldap.LDAPError as e:
        log.error('Failed to modify description: error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(USER1_DN, [(ldap.MOD_DELETE, 'description',
                                                    None)])
    except ldap.LDAPError as e:
        log.error('Failed to delete description: error ' + e.message['desc'])
        assert False

    #
    # Modrdns
    #
    try:
        topology_st.standalone.rename_s(USER1_DN, USER1_NEWDN, delold=1)
    except ldap.LDAPError as e:
        log.error('Failed to modrdn user1: error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.rename_s(USER2_DN, USER2_NEWDN, delold=0)
    except ldap.LDAPError as e:
        log.error('Failed to modrdn user2: error ' + e.message['desc'])
        assert False  # Modrdn - New superior

    try:
        topology_st.standalone.rename_s(USER3_DN, USER3_NEWDN,
                                        newsuperior=NEW_SUPERIOR, delold=1)
    except ldap.LDAPError as e:
        log.error('Failed to modrdn(new superior) user3: error ' + e.message['desc'])
        assert False
    #
    # Deletes
    #
    try:
        topology_st.standalone.delete_s(USER1_RDN_DN)
    except ldap.LDAPError as e:
        log.error('Failed to delete test entry1: ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.delete_s(USER2_RDN_DN)
    except ldap.LDAPError as e:
        log.error('Failed to delete test entry2: ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.delete_s(USER3_RDN_DN)
    except ldap.LDAPError as e:
        log.error('Failed to delete test entry3: ' + e.message['desc'])
        assert False
    log.info('test_basic_ops: PASSED')


def test_basic_import_export(topology_st, import_example_ldif):
    """Test online and offline LDIF import & export

    :id: 3ceeea11-9235-4e20-b80e-7203b2c6e149

    :setup: Standalone instance

    :steps:
         1. Generate a test ldif (50k entries)
         2. Import test ldif file using Online import.
         3. Import test ldif file using Offline import (ldif2db).
         4. Export test ldif file using Online export.
         5. Export test ldif file using Offline export (db2ldif).
         6. Cleanup - Import the Example LDIF for the other tests in this suite

    :expectedresults:
         1. Test ldif file creation should PASS.
         2. Online import should PASS.
         3. Offline import should PASS.
         4. Online export should PASS.
         5. Offline export should PASS.
         6. Cleanup should PASS.
    """

    log.info('Running test_basic_import_export...')

    tmp_dir = '/tmp'

    #
    # Test online/offline LDIF imports
    #
    topology_st.standalone.start()

    # Generate a test ldif (50k entries)
    ldif_dir = topology_st.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/basic_import.ldif'
    dbgen(topology_st.standalone, 50000, import_ldif, DEFAULT_SUFFIX)

    # Online
    try:
        topology_st.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                                input_file=import_ldif,
                                                args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_import_export: Online import failed')
        assert False

    # Offline
    topology_st.standalone.stop()
    if not topology_st.standalone.ldif2db(DEFAULT_BENAME, None, None, None, import_ldif):
        log.fatal('test_basic_import_export: Offline import failed')
        assert False
    topology_st.standalone.start()

    #
    # Test online and offline LDIF export
    #

    # Online export
    export_ldif = ldif_dir + '/export.ldif'
    exportTask = Tasks(topology_st.standalone)
    try:
        args = {TASK_WAIT: True}
        exportTask.exportLDIF(DEFAULT_SUFFIX, None, export_ldif, args)
    except ValueError:
        log.fatal('test_basic_import_export: Online export failed')
        assert False

    # Offline export
    topology_st.standalone.stop()
    if not topology_st.standalone.db2ldif(DEFAULT_BENAME, (DEFAULT_SUFFIX,),
                                          None, None, None, export_ldif):
        log.fatal('test_basic_import_export: Failed to run offline db2ldif')
        assert False

    topology_st.standalone.start()

    #
    # Cleanup - Import the Example LDIF for the other tests in this suite
    #
    ldif = '%s/dirsrv/data/Example.ldif' % topology_st.standalone.get_data_dir()
    import_ldif = topology_st.standalone.get_ldif_dir() + "/Example.ldif"
    shutil.copyfile(ldif, import_ldif)
    try:
        topology_st.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                                input_file=import_ldif,
                                                args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_import_export: Online import failed')
        assert False

    log.info('test_basic_import_export: PASSED')


def test_basic_backup(topology_st, import_example_ldif):
    """Tests online and offline backup and restore

    :id: 0e9d91f8-8748-40b6-ab03-fbd1998eb985

    :setup: Standalone instance and import example.ldif

    :steps:
         1. Test online backup using db2bak.
         2. Test online restore using bak2db.
         3. Test offline backup using db2bak.
         4. Test offline restore using bak2db.

    :expectedresults:
         1. Online backup should PASS.
         2. Online restore should PASS.
         3. Offline backup should PASS.
         4. Offline restore should PASS.
    """

    log.info('Running test_basic_backup...')

    backup_dir = topology_st.standalone.get_bak_dir() + '/backup_test'

    # Test online backup
    try:
        topology_st.standalone.tasks.db2bak(backup_dir=backup_dir,
                                            args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_backup: Online backup failed')
        assert False

    # Test online restore
    try:
        topology_st.standalone.tasks.bak2db(backup_dir=backup_dir,
                                            args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_backup: Online restore failed')
        assert False

    # Test offline backup
    topology_st.standalone.stop()
    if not topology_st.standalone.db2bak(backup_dir):
        log.fatal('test_basic_backup: Offline backup failed')
        assert False

    # Test offline restore
    if not topology_st.standalone.bak2db(backup_dir):
        log.fatal('test_basic_backup: Offline backup failed')
        assert False
    topology_st.standalone.start()

    log.info('test_basic_backup: PASSED')

def test_basic_db2index(topology_st, import_example_ldif):
    """Assert db2index can operate correctly.

    :id: 191fc0fd-9722-46b5-a7c3-e8760effe119

    :setup: Standalone instance

    :steps:
        1: call db2index

    :expectedresults:
        1: Index succeeds.

    """
    topology_st.standalone.stop()
    topology_st.standalone.db2index()
    topology_st.standalone.db2index(suffixes=[DEFAULT_SUFFIX], attrs=['uid'])
    topology_st.standalone.start()


def test_basic_acl(topology_st, import_example_ldif):
    """Run some basic access control (ACL) tests

    :id: 4f4e705f-32f4-4065-b3a8-2b0c2525798b

    :setup: Standalone instance

    :steps:
         1. Add two test users USER1_DN and USER2_DN.
         2. Add an aci that denies USER1 from doing anything.
         3. Set the default anonymous access for USER2.
         4. Try searching entries using USER1.
         5. Try searching entries using USER2.
         6. Try searching entries using root dn.
         7. Cleanup - delete test users and test ACI.

    :expectedresults:
         1. Test Users should be added.
         2. ACI should be added.
         3. This operation should PASS.
         4. USER1 should not be able to search anything.
         5. USER2 should be able to search everything except password.
         6. RootDN should be allowed to search everything.
         7. Cleanup should PASS.
    """

    """Run some basic access control(ACL) tests"""
    log.info('Running test_basic_acl...')

    DENY_ACI = ensure_bytes('(targetattr = "*")(version 3.0;acl "deny user";deny (all)(userdn = "ldap:///%s");)' % USER1_DN)

    #
    # Add two users
    #
    try:
        topology_st.standalone.add_s(Entry((USER1_DN,
                                            {'objectclass': "top extensibleObject".split(),
                                             'sn': '1',
                                             'cn': 'user 1',
                                             'uid': 'user1',
                                             'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to add test user ' + USER1_DN
                  + ': error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER2_DN,
                                            {'objectclass': "top extensibleObject".split(),
                                             'sn': '2',
                                             'cn': 'user 2',
                                             'uid': 'user2',
                                             'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to add test user ' + USER1_DN
                  + ': error ' + e.message['desc'])
        assert False

    #
    # Add an aci that denies USER1 from doing anything,
    # and also set the default anonymous access
    #
    try:
        topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', DENY_ACI)])
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to add DENY ACI: error ' + e.message['desc'])
        assert False

    #
    # Make sure USER1_DN can not search anything, but USER2_dn can...
    #
    try:
        topology_st.standalone.simple_bind_s(USER1_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to bind as user1, error: ' + e.message['desc'])
        assert False

    try:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  '(uid=*)')
        if entries:
            log.fatal('test_basic_acl: User1 was incorrectly able to search the suffix!')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Search suffix failed(as user1): ' + e.message['desc'])
        assert False

    # Now try user2...  Also check that userpassword is stripped out
    try:
        topology_st.standalone.simple_bind_s(USER2_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to bind as user2, error: ' + e.message['desc'])
        assert False

    try:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  '(uid=user1)')
        if not entries:
            log.fatal('test_basic_acl: User1 incorrectly not able to search the suffix')
            assert False
        if entries[0].hasAttr('userpassword'):
            # The default anonymous access aci should have stripped out userpassword
            log.fatal('test_basic_acl: User2 was incorrectly able to see userpassword')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Search for user1 failed(as user2): ' + e.message['desc'])
        assert False

    # Make sure RootDN can also search (this also resets the bind dn to the
    # Root DN for future operations)
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PW_DM)
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to bind as ROotDN, error: ' + e.message['desc'])
        assert False

    try:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  '(uid=*)')
        if not entries:
            log.fatal('test_basic_acl: Root DN incorrectly not able to search the suffix')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Search for user1 failed(as user2): ' + e.message['desc'])
        assert False

    #
    # Cleanup
    #
    try:
        topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_DELETE, 'aci', DENY_ACI)])
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to delete DENY ACI: error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to delete test entry1: ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.delete_s(USER2_DN)
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to delete test entry2: ' + e.message['desc'])
        assert False

    log.info('test_basic_acl: PASSED')


def test_basic_searches(topology_st, import_example_ldif):
    """Tests basic search operations with filters.

    :id: 426a59ff-49b8-4a70-b377-0c0634a29b6f

    :setup: Standalone instance, add example.ldif to the database

    :steps:
         1. Execute search command while using different filters.
         2. Check number of entries returned by search filters.

    :expectedresults:
         1. Search command should PASS.
         2. Number of result entries returned should match number of the database entries according to the search filter.
    """

    log.info('Running test_basic_searches...')

    filters = (('(uid=scarter)', 1),
               ('(uid=tmorris*)', 1),
               ('(uid=*hunt*)', 4),
               ('(uid=*cope)', 2),
               ('(mail=*)', 150),
               ('(roomnumber>=4000)', 35),
               ('(roomnumber<=4000)', 115),
               ('(&(roomnumber>=4000)(roomnumber<=4500))', 18),
               ('(!(l=sunnyvale))', 120),
               ('(&(uid=t*)(l=santa clara))', 7),
               ('(|(uid=k*)(uid=r*))', 18),
               ('(|(uid=t*)(l=sunnyvale))', 50),
               ('(&(!(uid=r*))(ou=people))', 139),
               ('(&(uid=m*)(l=sunnyvale)(ou=people)(mail=*example*)(roomNumber=*))', 3),
               ('(&(|(uid=m*)(l=santa clara))(roomNumber=22*))', 5),
               ('(&(|(uid=m*)(l=santa clara))(roomNumber=22*)(!(roomnumber=2254)))', 4),)

    for (search_filter, search_result) in filters:
        try:
            entries = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                      ldap.SCOPE_SUBTREE,
                                                      search_filter)
            if len(entries) != search_result:
                log.fatal('test_basic_searches: An incorrect number of entries\
                        was returned from filter (%s): (%d) expected (%d)' %
                          (search_filter, len(entries), search_result))
                assert False
        except ldap.LDAPError as e:
            log.fatal('Search failed: ' + e.message['desc'])
            assert False

    log.info('test_basic_searches: PASSED')


def test_basic_referrals(topology_st, import_example_ldif):
    """Test LDAP server in referral mode.

    :id: c586aede-7ac3-4e8d-a1cf-bfa8b8d78cc2

    :setup: Standalone instance

    :steps:
         1. Set the referral and the backend state
         2. Set backend state to referral mode.
         3. Set server to not follow referral.
         4. Search using referral.
         5. Make sure server can restart in referral mode.
         6. Cleanup - Delete referral.

    :expectedresults:
         1. Set the referral, and the backend state should PASS.
         2. Set backend state to referral mode should PASS.
         3. Set server to not follow referral should PASS.
         4. referral error(10) should occur.
         5. Restart should PASS.
         6. Cleanup should PASS.
    """

    log.info('Running test_basic_referrals...')
    SUFFIX_CONFIG = 'cn="dc=example,dc=com",cn=mapping tree,cn=config'
    #
    # Set the referral, and the backend state
    #
    try:
        topology_st.standalone.modify_s(SUFFIX_CONFIG,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-referral',
                                          b'ldap://localhost.localdomain:389/o%3dnetscaperoot')])
    except ldap.LDAPError as e:
        log.fatal('test_basic_referrals: Failed to set referral: error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(SUFFIX_CONFIG, [(ldap.MOD_REPLACE,
                                                         'nsslapd-state', b'Referral')])
    except ldap.LDAPError as e:
        log.fatal('test_basic_referrals: Failed to set backend state: error '
                  + e.message['desc'])
        assert False

    #
    # Test that a referral error is returned
    #
    topology_st.standalone.set_option(ldap.OPT_REFERRALS, 0)  # Do not follow referral
    try:
        topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=top')
    except ldap.REFERRAL:
        pass
    except ldap.LDAPError as e:
        log.fatal('test_basic_referrals: Search failed: ' + e.message['desc'])
        assert False

    #
    # Make sure server can restart in referral mode
    #
    topology_st.standalone.restart(timeout=10)

    #
    # Cleanup
    #
    try:
        topology_st.standalone.modify_s(SUFFIX_CONFIG, [(ldap.MOD_REPLACE,
                                                         'nsslapd-state', b'Backend')])
    except ldap.LDAPError as e:
        log.fatal('test_basic_referrals: Failed to set backend state: error '
                  + e.message['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(SUFFIX_CONFIG, [(ldap.MOD_DELETE,
                                                         'nsslapd-referral', None)])
    except ldap.LDAPError as e:
        log.fatal('test_basic_referrals: Failed to delete referral: error '
                  + e.message['desc'])
        assert False
    topology_st.standalone.set_option(ldap.OPT_REFERRALS, 1)

    log.info('test_basic_referrals: PASSED')


def test_basic_systemctl(topology_st, import_example_ldif):
    """Tests systemctl/lib389 can stop and start the server.

    :id: a92a7438-ecfa-4583-a89c-5fbfc0220b69

    :setup: Standalone instance

    :steps:
         1. Stop the server.
         2. Start the server.
         3. Stop the server, break the dse.ldif and dse.ldif.bak, so a start fails.
         4. Verify that systemctl detects the failed start.
         5. Fix the dse.ldif, and make sure the server starts up.
         6. Verify systemctl correctly identifies the successful start.

    :expectedresults:
         1. Server should be stopped.
         2. Server should start
         3. Stop should work but start after breaking dse.ldif should fail.
         4. Systemctl should be able to detect the failed start.
         5. Server should start.
         6. Systemctl should be able to detect the successful start.
    """

    log.info('Running test_basic_systemctl...')

    config_dir = topology_st.standalone.get_config_dir()

    #
    # Stop the server
    #
    log.info('Stopping the server...')
    topology_st.standalone.stop()
    log.info('Stopped the server.')

    #
    # Start the server
    #
    log.info('Starting the server...')
    topology_st.standalone.start()
    log.info('Started the server.')

    #
    # Stop the server, break the dse.ldif so a start fails,
    # and verify that systemctl detects the failed start
    #
    log.info('Stopping the server...')
    topology_st.standalone.stop()
    log.info('Stopped the server before breaking the dse.ldif.')

    shutil.copy(config_dir + '/dse.ldif', config_dir + '/dse.ldif.correct')
    open(config_dir + '/dse.ldif', 'w').close()
    # We need to kill the .bak file too, DS is just too smart!
    open(config_dir + '/dse.ldif.bak', 'w').close()

    log.info('Attempting to start the server with broken dse.ldif...')
    try:
        topology_st.standalone.start()
    except:
        log.info('Server failed to start as expected')
    log.info('Check the status...')
    assert (not topology_st.standalone.status())
    log.info('Server failed to start as expected')
    time.sleep(5)

    #
    # Fix the dse.ldif, and make sure the server starts up,
    # and systemctl correctly identifies the successful start
    #
    shutil.copy(config_dir + '/dse.ldif.correct', config_dir + '/dse.ldif')
    log.info('Starting the server with good dse.ldif...')
    topology_st.standalone.start()
    log.info('Check the status...')
    assert (topology_st.standalone.status())
    log.info('Server started after fixing dse.ldif.')

    log.info('test_basic_systemctl: PASSED')


def test_basic_ldapagent(topology_st, import_example_ldif):
    """Tests that the ldap agent starts

    :id: da1d1846-8fc4-4b8c-8e53-4c9c16eff1ba

    :setup: Standalone instance

    :steps:
         1. Start SNMP ldap agent using command.
         2. Cleanup - Kill SNMP agent process.

    :expectedresults:
         1. SNMP agent should start.
         2. SNMP agent process should be successfully killed.
    """

    log.info('Running test_basic_ldapagent...')

    var_dir = topology_st.standalone.get_local_state_dir()

    config_file = os.path.join(topology_st.standalone.get_sysconf_dir(), 'dirsrv/config/agent.conf')

    agent_config_file = open(config_file, 'w')
    agent_config_file.write('agentx-master ' + var_dir + '/agentx/master\n')
    agent_config_file.write('agent-logdir ' + var_dir + '/log/dirsrv\n')
    agent_config_file.write('server slapd-' + topology_st.standalone.serverid + '\n')
    agent_config_file.close()

    # Remember, this is *forking*
    check_output([os.path.join(topology_st.standalone.get_sbin_dir(), 'ldap-agent'), config_file])
    # First kill any previous agents ....
    pidpath = os.path.join(var_dir, 'run/ldap-agent.pid')
    pid = None
    with open(pidpath, 'r') as pf:
        pid = pf.readlines()[0].strip()
    if pid:
        log.debug('test_basic_ldapagent: Terminating agent %s', pid)
        check_output(['kill', pid])

    log.info('test_basic_ldapagent: PASSED')


def test_basic_dse(topology_st, import_example_ldif):
    """Tests that the dse.ldif is not wiped out after the process is killed (bug 910581)

    :id: 10f141da-9b22-443a-885c-87271dcd7a59

    :setup: Standalone instance

    :steps:
         1. Check out pid of ns-slapd process and Kill ns-slapd process.
         2. Check the contents of dse.ldif file.
         3. Start server.

    :expectedresults:
         1. ns-slapd process should be killed.
         2. dse.ldif should not be corrupted.
         3. Server should start successfully.
    """
    log.info('Running test_basic_dse...')

    dse_file = topology_st.standalone.confdir + '/dse.ldif'
    pid = check_output(['pidof', '-s', 'ns-slapd']).strip()
    check_output(['sudo', 'kill', '-9', ensure_str(pid)])
    if os.path.getsize(dse_file) == 0:
        log.fatal('test_basic_dse: dse.ldif\'s content was incorrectly removed!')
        assert False

    topology_st.standalone.start(timeout=60)
    log.info('dse.ldif was not corrupted, and the server was restarted')

    log.info('test_basic_dse: PASSED')
    # Give the server time to startup, in some conditions this can be racey without systemd notification. Only affects this one test though...
    time.sleep(10)


@pytest.mark.parametrize("rootdse_attr_name", ROOTDSE_DEF_ATTR_LIST)
def test_def_rootdse_attr(topology_st, import_example_ldif, rootdse_attr_name):
    """Tests that operational attributes are not returned by default in rootDSE searches

    :id: 4fee33cc-4019-4c27-89e8-998e6c770dc0

    :setup: Standalone instance

    :steps:
         1. Make an ldapsearch for rootdse attribute
         2. Check the returned entries.

    :expectedresults:
         1. Search should not fail
         2. Operational attributes should not be returned.
    """

    topology_st.standalone.start()

    log.info(" Assert rootdse search hasn't %s attr" % rootdse_attr_name)
    try:
        entry = topology_st.standalone.search_s("", ldap.SCOPE_BASE)[0]
        assert not entry.hasAttr(rootdse_attr_name)

    except ldap.LDAPError as e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        assert False


def test_mod_def_rootdse_attr(topology_st, import_example_ldif, rootdse_attr):
    """Tests that operational attributes are returned by default in rootDSE searches after config modification

   :id: c7831e04-f458-4e23-83c7-b6f66109f639

   :setup: Standalone instance and we are using rootdse_attr fixture which 
adds nsslapd-return-default-opattr attr with value of one operation attribute.

   :steps:
         1. Make an ldapsearch for rootdse attribute
         2. Check the returned entries.

   :expectedresults:
         1. Search should not fail
         2. Operational attributes should be returned after the config modification
   """

    log.info(" Assert rootdse search has %s attr" % rootdse_attr)
    try:
        entry = topology_st.standalone.search_s("", ldap.SCOPE_BASE)[0]
        assert entry.hasAttr(rootdse_attr)

    except ldap.LDAPError as e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        assert False


@pytest.fixture(scope="module")
def create_users(topology_st):
    """Add users to the default suffix
    """

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user_names = ["Directory", "Server", "389", "lib389", "pytest"]

    log.info('Adding 5 test users')
    for name in user_names:
        user = users.create(properties={
            'uid': name,
            'sn': name,
            'cn': name,
            'uidNumber': '1000',
            'gidNumber': '1000',
            'homeDirectory': '/home/%s' % name,
            'mail': '%s@example.com' % name,
            'userpassword': 'pass%s' % name,
        })


def test_basic_anonymous_search(topology_st, create_users):
    """Tests basic anonymous search operations

    :id: c7831e04-f458-4e50-83c7-b6f77109f639
    :setup: Standalone instance
            Add 5 test users with different user names
    :steps:
         1. Execute anonymous search with different filters
    :expectedresults:
         1. Search should be successful
    """

    filters = ["uid=Directory", "(|(uid=S*)(uid=3*))", "(&(uid=l*)(mail=l*))", "(&(!(uid=D*))(ou=People))"]
    log.info("Execute anonymous search with different filters")
    for filtr in filters:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filtr)
        assert len(entries) != 0


@pytest.mark.ds604
@pytest.mark.bz915801
def test_search_original_type(topology_st, create_users):
    """Test ldapsearch returning original attributes
        using nsslapd-search-return-original-type-switch

    :id: d7831d04-f558-4e50-93c7-b6f77109f640
    :setup: Standalone instance
            Add some test entries
    :steps:
         1. Set nsslapd-search-return-original-type-switch to ON
         2. Check that ldapsearch *does* return unknown attributes
         3. Turn off nsslapd-search-return-original-type-switch
         4. Check that ldapsearch doesn't return any unknown attributes
    :expectedresults:
         1. nsslapd-search-return-original-type-switch should be set to ON
         2. ldapsearch should return unknown attributes
         3. nsslapd-search-return-original-type-switch should be OFF
         4. ldapsearch should not return any unknown attributes
    """

    log.info("Set nsslapd-search-return-original-type-switch to ON")
    topology_st.standalone.config.set('nsslapd-search-return-original-type-switch', 'on')

    log.info("Check that ldapsearch *does* return unknown attributes")
    entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'uid=Directory',
                                              ['objectclass overflow', 'unknown'])
    assert "objectclass overflow" in entries[0].getAttrs()

    log.info("Set nsslapd-search-return-original-type-switch to Off")
    topology_st.standalone.config.set('nsslapd-search-return-original-type-switch', 'off')
    log.info("Check that ldapsearch *does not* return unknown attributes")
    entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'uid=Directory',
                                              ['objectclass overflow', 'unknown'])
    assert "objectclass overflow" not in entries[0].getAttrs()


@pytest.mark.bz192901
def test_search_ou(topology_st):
    """Test that DS should not return an entry that does not match the filter

    :id: d7831d05-f117-4e89-93c7-b6f77109f640
    :setup: Standalone instance
    :steps:
         1. Create an OU entry without sub entries
         2. Search from the OU with the filter that does not match the OU
    :expectedresults:
         1. Creation of OU should be successful
         2. Search should not return any results
    """

    log.info("Create a test OU without sub entries")
    ou = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    ou.create(properties={
        'ou': 'test_ou',
    })

    search_base = ("ou=test_ou,%s" % DEFAULT_SUFFIX)
    log.info("Search from the OU with the filter that does not match the OU, it should not return anything")
    entries = topology_st.standalone.search_s(search_base, ldap.SCOPE_SUBTREE, 'uid=*', ['dn'])
    assert len(entries) == 0


@pytest.mark.bz1044135
@pytest.mark.ds47319
def test_connection_buffer_size(topology_st):
    """Test connection buffer size adjustable with different values(valid values and invalid)

    :id: e7831d05-f117-4ec9-1203-b6f77109f117
    :setup: Standalone instance
    :steps:
         1. Set nsslapd-connection-buffer to some valid values (2, 0 , 1)
         2. Set nsslapd-connection-buffer to some invalid values (-1, a)
    :expectedresults:
         1. This should pass
         2. This should fail
    """

    valid_values = ['2', '0', '1']
    for value in valid_values:
        topology_st.standalone.config.replace('nsslapd-connection-buffer', value)

    invalid_values = ['-1', 'a']
    for value in invalid_values:
        with pytest.raises(ldap.OPERATIONS_ERROR):
            topology_st.standalone.config.replace('nsslapd-connection-buffer', value)

@pytest.mark.bz1637439
def test_critical_msg_on_empty_range_idl(topology_st):
    """Doing a range index lookup should not report a critical message even if IDL is empty

    :id: a07a2222-0551-44a6-b113-401d23799364
    :setup: Standalone instance
    :steps:
         1. Create an index for internationalISDNNumber. (attribute chosen because it is
         unlikely that previous tests used it)
         2. telephoneNumber being indexed by default create 20 users without telephoneNumber
         3. add a telephoneNumber value and delete it to trigger an empty index database
         4. Do a search that triggers a range lookup on empty telephoneNumber
         5. Check that the critical message is not logged in error logs
    :expectedresults:
         1. This should pass
         2. This should pass
         3. This should pass
         4. This should pass on normal build but could abort a debug build
         4. This should pass
    """
    indexedAttr = 'internationalISDNNumber'

    # Step 1
    from lib389.index import Indexes

    indexes = Indexes(topology_st.standalone)
    indexes.create(properties={
        'cn': indexedAttr,
        'nsSystemIndex': 'false',
        'nsIndexType': 'eq'
        })
    topology_st.standalone.restart()

    # Step 2
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    log.info('Adding 20 users without "%s"' % indexedAttr)
    for i in range(20):
        name = 'user_%d' % i
        last_user = users.create(properties={
            'uid': name,
            'sn': name,
            'cn': name,
            'uidNumber': '1000',
            'gidNumber': '1000',
            'homeDirectory': '/home/%s' % name,
            'mail': '%s@example.com' % name,
            'userpassword': 'pass%s' % name,
        })

    # Step 3
    # required update to create the indexAttr (i.e. 'loginShell') database, and then make it empty
    topology_st.standalone.modify_s(last_user.dn, [(ldap.MOD_ADD, indexedAttr, b'1234')])
    ent = topology_st.standalone.getEntry(last_user.dn, ldap.SCOPE_BASE,)
    assert ent
    assert ent.hasAttr(indexedAttr)
    topology_st.standalone.modify_s(last_user.dn, [(ldap.MOD_DELETE, indexedAttr, None)])
    ent = topology_st.standalone.getEntry(last_user.dn, ldap.SCOPE_BASE,)
    assert ent
    assert not ent.hasAttr(indexedAttr)

    # Step 4
    # The first component being not indexed the range on second is evaluated
    try:
        ents = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(&(sudoNotAfter=*)(%s>=111))' % indexedAttr)
        assert len(ents) == 0
    except ldap.SERVER_DOWN:
        log.error('Likely testing against a debug version that asserted')
        pass

    # Step 5
    assert not topology_st.standalone.searchErrorsLog('CRIT - list_candidates - NULL idl was recieved from filter_candidates_ext.')

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)


