# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

from subprocess import check_output, PIPE, run
from lib389 import DirSrv
from lib389.idm.user import UserAccount, UserAccounts
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.dbgen import dbgen_users
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389._constants import DN_DM, PASSWORD, PW_DM, ReplicaRole
from lib389.paths import Paths
from lib389.idm.directorymanager import DirectoryManager
from lib389.config import LDBMConfig, CertmapLegacy
from lib389.dseldif import DSEldif
from lib389.rootdse import RootDSE
from ....conftest import get_rpm_version
from lib389._mapped_object import DSLdapObjects
from lib389.replica import Replicas, Changelog
from lib389.backend import Backends
from lib389.idm.domain import Domain
from lib389.nss_ssl import NssSsl


pytestmark = pytest.mark.tier0

default_paths = Paths()

log = logging.getLogger(__name__)

# Globals
USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX
USER3_DN = 'uid=user3,' + DEFAULT_SUFFIX
USER4_DN = 'uid=user4,' + DEFAULT_SUFFIX

ROOTDSE_DEF_ATTR_LIST = ('namingContexts',
                         'supportedLDAPVersion',
                         'supportedControl',
                         'supportedExtension',
                         'supportedSASLMechanisms',
                         'vendorName',
                         'vendorVersion')


@pytest.fixture(scope="function")
def _reset_attr(request, topology_st):
    """ Reset nsslapd-close-on-failed-bind attr to the default (off) """

    def fin():
        dm = DirectoryManager(topology_st.standalone)
        try:
            dm_conn = dm.bind()
            dm_conn.config.replace('nsslapd-close-on-failed-bind', 'off')
            assert (dm_conn.config.get_attr_val_utf8('nsslapd-close-on-failed-bind')) == 'off'
        except ldap.LDAPError as e:
            log.error('Failure reseting attr')
            assert False
        topology_st.standalone.restart()

    request.addfinalizer(fin)


@pytest.fixture(scope="module")
def import_example_ldif(topology_st):
    """Import the Example LDIF for the tests in this suite"""

    log.info('Initializing the "basic" test suite')

    ldif = '%s/dirsrv/data/Example.ldif' % topology_st.standalone.get_data_dir()
    import_ldif = topology_st.standalone.get_ldif_dir() + "/Example.ldif"
    shutil.copy(ldif, import_ldif)

    import_task = ImportTask(topology_st.standalone)
    import_task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait()


def check_db_sanity(topology_st):
    try:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  '(uid=scarter)')
        if entries is None:
            log.fatal('Unable to find user uid=scarter. DB or indexes are probably corrupted !')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Search suffix failed: ' + e.args[0]['desc'])
        assert False


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
        log.fatal('Failed to add attr: error (%s)' % (e.args[0]['desc']))
        assert False

    def fin():
        log.info("        Delete the %s: %s from rootdse" % (RETURN_DEFAULT_OPATTR,
                                                             rootdse_attr_name))
        mod = [(ldap.MOD_DELETE, RETURN_DEFAULT_OPATTR, rootdse_attr_name)]
        try:
            topology_st.standalone.modify_s("", mod)
        except ldap.LDAPError as e:
            log.fatal('Failed to delete attr: error (%s)' % (e.args[0]['desc']))
            assert False

    request.addfinalizer(fin)

    return rootdse_attr_name


def change_conf_attr(topology_st, suffix, attr_name, attr_value):
    """Change configuration attribute in the given suffix.

    Returns previous attribute value.
    """

    entry = DSLdapObject(topology_st.standalone, suffix)

    attr_value_bck = entry.get_attr_val_bytes(attr_name)
    log.info('Set %s to %s. Previous value - %s. Modified suffix - %s.' % (
        attr_name, attr_value, attr_value_bck, suffix))
    if attr_value is None:
        entry.remove_all(attr_name)
    else:
        entry.replace(attr_name, attr_value)
    return attr_value_bck


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
        log.error('Failed to add test user' + USER1_DN + ': error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER2_DN,
                                            {'objectclass': "top extensibleObject".split(),
                                             'sn': '2',
                                             'cn': 'user2',
                                             'uid': 'user2',
                                             'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.error('Failed to add test user' + USER2_DN + ': error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER3_DN,
                                            {'objectclass': "top extensibleObject".split(),
                                             'sn': '3',
                                             'cn': 'user3',
                                             'uid': 'user3',
                                             'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.error('Failed to add test user' + USER3_DN + ': error ' + e.args[0]['desc'])
        assert False

    #
    # Mods
    #
    try:
        topology_st.standalone.modify_s(USER1_DN, [(ldap.MOD_ADD, 'description',
                                                    b'New description')])
    except ldap.LDAPError as e:
        log.error('Failed to add description: error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'description',
                                                    b'Modified description')])
    except ldap.LDAPError as e:
        log.error('Failed to modify description: error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(USER1_DN, [(ldap.MOD_DELETE, 'description',
                                                    None)])
    except ldap.LDAPError as e:
        log.error('Failed to delete description: error ' + e.args[0]['desc'])
        assert False

    #
    # Modrdns
    #
    try:
        topology_st.standalone.rename_s(USER1_DN, USER1_NEWDN, delold=1)
    except ldap.LDAPError as e:
        log.error('Failed to modrdn user1: error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.rename_s(USER2_DN, USER2_NEWDN, delold=0)
    except ldap.LDAPError as e:
        log.error('Failed to modrdn user2: error ' + e.args[0]['desc'])
        assert False  # Modrdn - New superior

    try:
        topology_st.standalone.rename_s(USER3_DN, USER3_NEWDN,
                                        newsuperior=NEW_SUPERIOR, delold=1)
    except ldap.LDAPError as e:
        log.error('Failed to modrdn(new superior) user3: error ' + e.args[0]['desc'])
        assert False
    #
    # Deletes
    #
    try:
        topology_st.standalone.delete_s(USER1_RDN_DN)
    except ldap.LDAPError as e:
        log.error('Failed to delete test entry1: ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.delete_s(USER2_RDN_DN)
    except ldap.LDAPError as e:
        log.error('Failed to delete test entry2: ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.delete_s(USER3_RDN_DN)
    except ldap.LDAPError as e:
        log.error('Failed to delete test entry3: ' + e.args[0]['desc'])
        assert False
    check_db_sanity(topology_st)
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
    #
    # Test online/offline LDIF imports
    #
    topology_st.standalone.start()
    # topology_st.standalone.config.set('nsslapd-errorlog-level', '1')

    # Generate a test ldif (50k entries)
    log.info("Generating LDIF...")
    ldif_dir = topology_st.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/basic_import.ldif'
    dbgen_users(topology_st.standalone, 50000, import_ldif, DEFAULT_SUFFIX)

    # Online
    log.info("Importing LDIF online...")
    import_task = ImportTask(topology_st.standalone)
    import_task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)

    # Wait a bit till the task is created and available for searching
    time.sleep(0.5)

    # Good as place as any to quick test the task has some expected attributes
    if ds_is_newer('1.4.1.2'):
        assert import_task.present('nstaskcreated')
    assert import_task.present('nstasklog')
    assert import_task.present('nstaskcurrentitem')
    assert import_task.present('nstasktotalitems')
    assert import_task.present('ttl')

    import_task.wait()

    # Offline
    log.info("Importing LDIF offline...")
    topology_st.standalone.stop()
    if not topology_st.standalone.ldif2db(DEFAULT_BENAME, None, None, None, import_ldif):
        log.fatal('test_basic_import_export: Offline import failed')
        assert False
    topology_st.standalone.start()

    #
    # Test online and offline LDIF export
    #

    # Online export
    log.info("Exporting LDIF online...")
    export_ldif = ldif_dir + '/export.ldif'

    export_task = ExportTask(topology_st.standalone)
    export_task.export_suffix_to_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)
    export_task.wait()

    # Offline export
    log.info("Exporting LDIF offline...")
    topology_st.standalone.stop()
    if not topology_st.standalone.db2ldif(DEFAULT_BENAME, (DEFAULT_SUFFIX,),
                                          None, None, None, export_ldif):
        log.fatal('test_basic_import_export: Failed to run offline db2ldif')
        assert False

    topology_st.standalone.start()

    #
    # Cleanup - Import the Example LDIF for the other tests in this suite
    #
    log.info("Restore datrabase, import initial LDIF...")
    ldif = '%s/dirsrv/data/Example.ldif' % topology_st.standalone.get_data_dir()
    import_ldif = topology_st.standalone.get_ldif_dir() + "/Example.ldif"
    shutil.copyfile(ldif, import_ldif)

    import_task = ImportTask(topology_st.standalone)
    import_task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait()

    check_db_sanity(topology_st)
    log.info('test_basic_import_export: PASSED')


def test_basic_backup(topology_st, import_example_ldif):
    """Tests online and offline backup and restore

    :id: 0e9d91f8-8748-40b6-ab03-fbd1998eb985

    :setup: Standalone instance and import example.ldif

    :steps:
         1. Test online backup using db2bak.
         2. Test config files are backed up
         3. Test online restore using bak2db.
         4. Test offline backup using db2bak.
         5. Test config files are backed up
         6. Test offline restore using bak2db.

    :expectedresults:
         1. Online backup should PASS.
         2. Config files were backed up
         3. Online restore should PASS.
         4. Offline backup should PASS.
         5. Config files were backed up
         6. Offline restore should PASS.
    """

    log.info('Running test_basic_backup...')

    backup_dir = topology_st.standalone.get_bak_dir() + '/backup_test_online'
    log.info(f'Backup directory is {backup_dir}')

    # Test online backup
    try:
        topology_st.standalone.tasks.db2bak(backup_dir=backup_dir,
                                            args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_backup: Online backup failed')
        assert False

    # Test config files were backed up
    assert os.path.isfile(backup_dir + "/config_files/dse.ldif")
    assert os.path.isfile(backup_dir + "/config_files/schema/99user.ldif")
    assert os.path.isfile(backup_dir + "/config_files/certmap.conf")
    assert os.path.isfile(backup_dir + "/config_files/cert9.db")

    # Test online restore
    try:
        topology_st.standalone.tasks.bak2db(backup_dir=backup_dir,
                                            args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_backup: Online restore failed')
        assert False

    # Test offline backup
    backup_dir = topology_st.standalone.get_bak_dir() + '/backup_test_offline'
    topology_st.standalone.stop()
    if not topology_st.standalone.db2bak(backup_dir):
        log.fatal('test_basic_backup: Offline backup failed')
        assert False

    # Test config files wre backed up
    assert os.path.isfile(backup_dir + "/config_files/dse.ldif")
    assert os.path.isfile(backup_dir + "/config_files/schema/99user.ldif")
    assert os.path.isfile(backup_dir + "/config_files/certmap.conf")
    assert os.path.isfile(backup_dir + "/config_files/cert9.db")

    # Test offline restore
    if not topology_st.standalone.bak2db(backup_dir):
        log.fatal('test_basic_backup: Offline backup failed')
        assert False
    topology_st.standalone.start()

    check_db_sanity(topology_st)
    log.info('test_basic_backup: PASSED')


def test_basic_db2index(topology_st):
    """Assert db2index can operate correctly.

    :id: 191fc0fd-9722-46b5-a7c3-e8760effe119

    :setup: Standalone instance

    :steps:
        1: Call db2index with a single index attribute
        2: Call db2index with multiple index attributes
        3: Call db2index with no index attributes

    :expectedresults:
        1: Index succeeds for single index attribute
        2: Index succeeds for multiple index attributes
        3: Index succeeds for all backend indexes which have been obtained from dseldif

    """

    # Error log message to confirm a reindex
    if get_default_db_lib() == "mdb":
        dbprefix = "dbmdb"
    else:
        dbprefix = "bdb"
    info_message = f'INFO - {dbprefix}_db2index - {DEFAULT_BENAME}: Indexing attribute: '

    log.info('Start the server')
    topology_st.standalone.start()
    check_db_sanity(topology_st)

    log.info('Offline reindex, stopping the server')
    topology_st.standalone.stop()

    log.info('Reindex with a single index attribute')
    topology_st.standalone.db2index(bename=DEFAULT_BENAME, attrs=['uid'])
    assert topology_st.standalone.searchErrorsLog(info_message + 'uid')

    log.info('Restart the server to clear the logs')
    topology_st.standalone.start()
    check_db_sanity(topology_st)
    topology_st.standalone.stop()

    log.info('Reindex with multiple attributes')
    topology_st.standalone.db2index(bename=DEFAULT_BENAME, attrs=['cn','aci','givenname'])
    assert topology_st.standalone.searchErrorsLog(info_message + 'cn')
    assert topology_st.standalone.searchErrorsLog(info_message + 'aci')
    assert topology_st.standalone.searchErrorsLog(info_message + 'givenname')

    log.info('Restart the server to clear the logs')
    topology_st.standalone.start()
    check_db_sanity(topology_st)
    topology_st.standalone.stop()

    log.info('Start the server and get all indexes for specified backend')
    topology_st.standalone.start()
    check_db_sanity(topology_st)
    dse_ldif = DSEldif(topology_st.standalone)
    indexes = dse_ldif.get_indexes(DEFAULT_BENAME)
    numIndexes = len(indexes)
    assert numIndexes > 0

    log.info('Stop the server and reindex with all backend indexes')
    topology_st.standalone.stop()
    topology_st.standalone.db2index(bename=DEFAULT_BENAME, attrs=indexes)
    log.info('Checking the server logs for %d backend indexes INFO' % numIndexes)
    for indexNum, index in enumerate(indexes):
        if index in "entryrdn":
            assert topology_st.standalone.searchErrorsLog(
                f'INFO - {dbprefix}_db2index - {DEFAULT_BENAME}: Indexing {index}')
        else:
            assert topology_st.standalone.searchErrorsLog(
                f'INFO - {dbprefix}_db2index - {DEFAULT_BENAME}: Indexing attribute: {index}')

    assert indexNum+1 == numIndexes

    topology_st.standalone.start()
    check_db_sanity(topology_st)
    log.info('test_basic_db2index: PASSED')


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
        log.fatal('test_basic_acl: Failed to add test user ' + USER1_DN +
                  ': error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER2_DN,
                                            {'objectclass': "top extensibleObject".split(),
                                             'sn': '2',
                                             'cn': 'user 2',
                                             'uid': 'user2',
                                             'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to add test user ' + USER1_DN +
                  ': error ' + e.args[0]['desc'])
        assert False

    #
    # Add an aci that denies USER1 from doing anything,
    # and also set the default anonymous access
    #
    try:
        topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', DENY_ACI)])
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to add DENY ACI: error ' + e.args[0]['desc'])
        assert False

    #
    # Make sure USER1_DN can not search anything, but USER2_dn can...
    #
    try:
        topology_st.standalone.simple_bind_s(USER1_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to bind as user1, error: ' + e.args[0]['desc'])
        assert False

    try:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  '(uid=*)')
        if entries:
            log.fatal('test_basic_acl: User1 was incorrectly able to search the suffix!')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Search suffix failed(as user1): ' + e.args[0]['desc'])
        assert False

    # Now try user2...  Also check that userpassword is stripped out
    try:
        topology_st.standalone.simple_bind_s(USER2_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to bind as user2, error: ' + e.args[0]['desc'])
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
        log.fatal('test_basic_acl: Search for user1 failed(as user2): ' + e.args[0]['desc'])
        assert False

    # Make sure RootDN can also search (this also resets the bind dn to the
    # Root DN for future operations)
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PW_DM)
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to bind as ROotDN, error: ' + e.args[0]['desc'])
        assert False

    try:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  '(uid=*)')
        if not entries:
            log.fatal('test_basic_acl: Root DN incorrectly not able to search the suffix')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Search for user1 failed(as user2): ' + e.args[0]['desc'])
        assert False

    #
    # Cleanup
    #
    try:
        topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_DELETE, 'aci', DENY_ACI)])
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to delete DENY ACI: error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to delete test entry1: ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.delete_s(USER2_DN)
    except ldap.LDAPError as e:
        log.fatal('test_basic_acl: Failed to delete test entry2: ' + e.args[0]['desc'])
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
            log.fatal('Search failed: ' + e.args[0]['desc'])
            assert False

    log.info('test_basic_searches: PASSED')


@pytest.mark.parametrize('limit,resp',
                         ((('200'), 'PASS'),
                         (('50'), ldap.ADMINLIMIT_EXCEEDED)))
def test_basic_search_lookthroughlimit(topology_st, limit, resp, import_example_ldif):
    """
    Tests normal search with lookthroughlimit set high and low.

    :id: b5119970-6c9f-41b7-9649-de9233226fec

    :setup: Standalone instance, add example.ldif to the database, search filter (uid=*).

    :steps:
        1. Import ldif user file.
        2. Change lookthroughlimit to 200.
        3. Bind to server as low priv user
        4. Run search 1 with "high" lookthroughlimit.
        5. Change lookthroughlimit to 50.
        6. Run search 2 with "low" lookthroughlimit.
        7. Delete user from DB.
        8. Reset lookthroughlimit to original.

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success, first search should complete with no error.
        5. Success
        6. Success, second search should return ldap.ADMINLIMIT_EXCEEDED error.
        7. Success
        8. Success

    """

    log.info('Running test_basic_search_lookthroughlimit...')

    search_filter = "(uid=*)"

    ltl_orig = change_conf_attr(topology_st, 'cn=config,cn=ldbm database,cn=plugins,cn=config', 'nsslapd-lookthroughlimit', limit)

    try:
        users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX, rdn=None)
        user = users.create_test_user()
        user.replace('userPassword', PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to create test user: error ' + e.args[0]['desc'])
        assert False

    try:
        conn = UserAccount(topology_st.standalone, user.dn).bind(PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind test user: error ' + e.args[0]['desc'])
        assert False

    try:
        if resp == ldap.ADMINLIMIT_EXCEEDED:
            with pytest.raises(ldap.ADMINLIMIT_EXCEEDED):
                searchid = conn.search(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, search_filter)
                rtype, rdata = conn.result(searchid)
        else:
            searchid = conn.search(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, search_filter)
            rtype, rdata = conn.result(searchid)
            assert(len(rdata) == 151) #151 entries in the imported ldif file using "(uid=*)"
    except ldap.LDAPError as e:
        log.fatal('Failed to perform search: error ' + e.args[0]['desc'])
        assert False

    finally:
        #Cleanup
        change_conf_attr(topology_st, 'cn=config,cn=ldbm database,cn=plugins,cn=config', 'nsslapd-lookthroughlimit', ltl_orig)
        user.delete()

    log.info('test_basic_search_lookthroughlimit: PASSED')


@pytest.fixture(scope="module")
def add_test_entry(topology_st, request):
    # Add test entry
    topology_st.standalone.add_s(Entry((USER4_DN,
                                        {'objectclass': "top extensibleObject".split(),
                                         'cn': 'user1', 'uid': 'user1'})))


search_params = [(['1.1'], 'cn', False),
                 (['1.1', 'cn'], 'cn', True),
                 (['+'], 'nsUniqueId', True),
                 (['*'], 'cn', True),
                 (['cn'], 'cn', True)]
@pytest.mark.skipif(ds_is_older("1.4.2.0"), reason="Not implemented")
@pytest.mark.parametrize("attrs, attr, present", search_params)
def test_search_req_attrs(topology_st, add_test_entry, attrs, attr, present):
    """Test requested attributes in search operations.

    :id: 426a59ff-49b8-4a70-b377-0c0634a29b6e
    :parametrized: yes
    :setup: Standalone instance
    :steps:
         1. Test "1.1" does not return any attributes.
         2. Test "1.1" is ignored if there are other requested attributes
         3. Test "+" returns all operational attributes
         4. Test "*" returns all attributes
         5. Test requested attributes

    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
         5. Success
    """

    log.info("Testing attrs: {} attr: {} present: {}".format(attrs, attr, present))
    entry = topology_st.standalone.search_s(USER4_DN,
                                            ldap.SCOPE_BASE,
                                            'objectclass=top',
                                            attrs)
    if present:
        assert entry[0].hasAttr(attr)
    else:
        assert not entry[0].hasAttr(attr)


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
        log.fatal('test_basic_referrals: Failed to set referral: error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(SUFFIX_CONFIG, [(ldap.MOD_REPLACE,
                                                         'nsslapd-state', b'Referral')])
    except ldap.LDAPError as e:
        log.fatal('test_basic_referrals: Failed to set backend state: error '
                  + e.args[0]['desc'])
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
        log.fatal('test_basic_referrals: Search failed: ' + e.args[0]['desc'])
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
                  + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(SUFFIX_CONFIG, [(ldap.MOD_DELETE,
                                                         'nsslapd-referral', None)])
    except ldap.LDAPError as e:
        log.fatal('test_basic_referrals: Failed to delete referral: error '
                  + e.args[0]['desc'])
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
    except Exception as e:
        log.info('Server failed to start as expected: ' + str(e))
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

    if not os.path.exists(os.path.join(topology_st.standalone.get_sbin_dir(), 'ldap-agent')):
        pytest.skip("ldap-agent is not present")
    var_dir = topology_st.standalone.get_local_state_dir()

    config_file = os.path.join(topology_st.standalone.get_sysconf_dir(), 'dirsrv/config/agent.conf')

    agent_config_file = open(config_file, 'w')
    agent_config_file.write('agentx-supplier ' + var_dir + '/agentx/supplier\n')
    agent_config_file.write('agent-logdir ' + var_dir + '/log/dirsrv\n')
    agent_config_file.write('server slapd-' + topology_st.standalone.serverid + '\n')
    agent_config_file.close()

    # Remember, this is *forking*
    check_output([os.path.join(topology_st.standalone.get_sbin_dir(), 'ldap-agent'), config_file])
    # First kill any previous agents ....
    run_dir = topology_st.standalone.get_run_dir()
    pidpath = os.path.join(run_dir, 'ldap-agent.pid')
    pid = None
    with open(pidpath, 'r') as pf:
        pid = pf.readlines()[0].strip()
    if pid:
        log.debug('test_basic_ldapagent: Terminating agent %s', pid)
        check_output(['kill', pid])

    log.info('test_basic_ldapagent: PASSED')


@pytest.mark.skipif(not get_user_is_ds_owner(),
                    reason="process ownership permission is required")
def test_basic_dse_survives_kill9(topology_st, import_example_ldif):
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
    # We can't guarantee we have access to sudo in any environment ... Either
    # run py.test with sudo, or as the same user as the dirsrv.
    check_output(['kill', '-9', ensure_str(pid)])
    if os.path.getsize(dse_file) == 0:
        log.fatal('test_basic_dse: dse.ldif\'s content was incorrectly removed!')
        assert False

    topology_st.standalone.start(timeout=60)
    log.info('dse.ldif was not corrupted, and the server was restarted')

    log.info('test_basic_dse: PASSED')
    # Give the server time to startup, in some conditions this can be racey without systemd notification.
    # Only affects this one test though...
    time.sleep(10)


@pytest.mark.parametrize("rootdse_attr_name", ROOTDSE_DEF_ATTR_LIST)
def test_def_rootdse_attr(topology_st, import_example_ldif, rootdse_attr_name):
    """Tests that operational attributes are not returned by default in rootDSE searches

    :id: 4fee33cc-4019-4c27-89e8-998e6c770dc0
    :parametrized: yes
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
        log.fatal('Search failed, error: ' + e.args[0]['desc'])
        assert False


def test_mod_def_rootdse_attr(topology_st, import_example_ldif, rootdse_attr):
    """Tests that operational attributes are returned by default in rootDSE searches after config modification

   :id: c7831e04-f458-4e23-83c7-b6f66109f639
   :parametrized: yes
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
        log.fatal('Search failed, error: ' + e.args[0]['desc'])
        assert False


@pytest.fixture(scope="module")
def create_users(topology_st):
    """Add users to the default suffix
    """

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user_names = ["Directory", "Server", "389", "lib389", "pytest"]

    log.info('Adding 5 test users')
    for name in user_names:
        users.create(properties={
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


def test_bind_invalid_entry(topology_st):
    """Test the failing bind does not return information about the entry

    :id: 5cd9b083-eea6-426b-84ca-83c26fc49a6f
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1: bind as non existing entry
        2: check that bind info does not report 'No such entry'
    :expectedresults:
        1: pass
        2: pass
    """

    topology_st.standalone.restart()
    INVALID_ENTRY="cn=foooo,%s" % DEFAULT_SUFFIX
    try:
        topology_st.standalone.simple_bind_s(INVALID_ENTRY, PASSWORD)
    except ldap.LDAPError as e:
        log.info('test_bind_invalid_entry: Failed to bind as %s (expected)' % INVALID_ENTRY)
        log.info('exception description: ' + e.args[0]['desc'])
        if 'info' in e.args[0]:
            log.info('exception info: ' + e.args[0]['info'])
        assert e.args[0]['desc'] == 'Invalid credentials'
        assert 'info' not in e.args[0]
        pass

    log.info('test_bind_invalid_entry: PASSED')

    # reset credentials
    topology_st.standalone.simple_bind_s(DN_DM, PW_DM)


def test_bind_entry_missing_passwd(topology_st):
    """
    :id: af209149-8fb8-48cb-93ea-3e82dd7119d2
    :setup: Standalone Instance
    :steps:
        1. Bind as database entry that does not have userpassword set
        2. Bind as database entry that does not exist
        3. Bind as cn=config entry that does not have userpassword set
        4. Bind as cn=config entry that does not exist
    :expectedresults:
        1. Fails with error 49
        2. Fails with error 49
        3. Fails with error 49
        4. Fails with error 49
    """
    user = UserAccount(topology_st.standalone, DEFAULT_SUFFIX)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        # Bind as the suffix root entry which does not have a userpassword
        user.bind("some_password")

    user = UserAccount(topology_st.standalone, "cn=not here," + DEFAULT_SUFFIX)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        # Bind as the entry which does not exist
        user.bind("some_password")

    # Test cn=config since it has its own code path
    user = UserAccount(topology_st.standalone, "cn=config")
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        # Bind as the config entry which does not have a userpassword
        user.bind("some_password")

    user = UserAccount(topology_st.standalone, "cn=does not exist,cn=config")
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        # Bind as an entry under cn=config that does not exist
        user.bind("some_password")


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
         5. This should pass
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


@pytest.mark.bz1870624
@pytest.mark.ds4379
@pytest.mark.parametrize("case,value", [('positive', ['cn','','']),
                                        ("positive", ['cn', '', '', '', '', '', '', '', '', '', '']),
                                        ("negative", ['cn', '', '', '', '', '', '', '', '', '', '', ''])])
def test_attr_description_limit(topology_st, case, value):
    """Test that up to 10 empty attributeDescription is allowed

    :id: 5afd3dcd-1028-428d-822d-a489ecf4b67e
    :customerscenario: True
    :parametrized: yes
    :setup: Standalone instance
    :steps:
        1. Check that 2 empty values are allowed
        2. Check that 10 empty values are allowed
        3. Check that more than 10 empty values are allowed
    :expectedresults:
        1. Should succeed
        2. Should succeed
        3. Should fail
    """
    if case == 'positive':
        DSLdapObjects(topology_st.standalone, basedn='').filter("(objectclass=*)", attrlist=value, scope=0)
    else:
        with pytest.raises(ldap.PROTOCOL_ERROR):
            DSLdapObjects(topology_st.standalone, basedn='').filter("(objectclass=*)", attrlist=value, scope=0)


@pytest.mark.bz1647099
@pytest.mark.ds50026
def test_ldbm_modification_audit_log(topology_st):
    """When updating LDBM config attributes, those attributes/values are not listed
    in the audit log

    :id: 5bf75c47-a283-430e-a65c-3c5fd8dbadb8
    :setup: Standalone Instance
    :steps:
        1. Bind as DM
        2. Enable audit log
        3. Update a set of config attrs in LDBM config
        4. Restart the server
        5. Check that config attrs are listed in the audit log
    :expectedresults:
        1. Operation successful
        2. Operation successful
        3. Operation successful
        4. Operation successful
        5. Audit log should contain modification of attrs"
    """

    VALUE = '10001'

    d_manager = DirectoryManager(topology_st.standalone)
    conn = d_manager.bind()
    config_ldbm = LDBMConfig(conn)

    log.info("Enable audit logging")
    conn.config.enable_log('audit')

    attrs = ['nsslapd-lookthroughlimit', 'nsslapd-pagedidlistscanlimit', 'nsslapd-idlistscanlimit', 'nsslapd-db-locks']

    for attr in attrs:
        log.info("Set attribute %s to value %s" % (attr, VALUE))
        config_ldbm.set(attr, VALUE)

    log.info('Restart the server to flush the logs')
    conn.restart()

    for attr in attrs:
        log.info("Check if attribute %s is replaced in the audit log" % attr)
        assert conn.searchAuditLog('replace: %s' % attr)
        assert conn.searchAuditLog('%s: %s' % (attr, VALUE))


def test_suffix_case(topology_st):
    """Test that the suffix case is preserved when creating a new backend

    :id: 4eff15be-6cde-4312-b492-c88941876bda
    :setup: Standalone Instance
    :steps:
        1. Create backend with uppercase characters
        2. Create root node entry
        3. Search should return suffix with upper case characters
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    # Start with a clean slate
    topology_st.standalone.restart()

    TEST_SUFFIX = 'dc=UPPER_CASE'

    backends = Backends(topology_st.standalone)
    backends.create(properties={'nsslapd-suffix': TEST_SUFFIX,
                                'name': 'upperCaseRoot',
                                'sample_entries': '001004002'})

    domain = Domain(topology_st.standalone, TEST_SUFFIX)
    assert domain.dn == TEST_SUFFIX


def test_bind_disconnect_invalid_entry(topology_st, _reset_attr):
    """Test close connection on failed bind with invalid entry

    :id: b378543e-32dc-432a-9756-ce318d6d654b
    :setup: Standalone instance
    :steps:
        1. create/get user
        2. bind and search as user
        3. enable nsslapd-close-on-failed-bind attr
        4. bind as non existing entry to trigger connection closure
        5. verify connection has been closed and server is still running
        6. cleanup
    :expectedresults:
        1. success
        2. success
        3. nsslapd-close-on-failed-bind attr set to on
        4. returns INVALID_CREDENTIALS, triggering connection closure
        5. success
        6. success
    """

    INVALID_ENTRY="cn=foooo,%s" % DEFAULT_SUFFIX
    inst = topology_st.standalone

    dm = DirectoryManager(inst)

    # create/get user
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    try:
        user = users.create_test_user()
        user.set("userPassword", PW_DM)
    except ldap.ALREADY_EXISTS:
        user = users.get('test_user_1000')

    # verify user can bind and search
    try:
        inst.simple_bind_s(user.dn, PW_DM)
    except ldap.LDAPError as e:
        log.error('Failed to bind {}'.format(user.dn))
        raise e
    try:
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'])
    except ldap.LDAPError as e:
        log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
        raise e

    # enable and verify attr
    try:
        dm_conn = dm.bind()
        dm_conn.config.replace('nsslapd-close-on-failed-bind', 'on')
        assert (dm_conn.config.get_attr_val_utf8('nsslapd-close-on-failed-bind')) == 'on'
    except ldap.LDAPError as e:
        log.error('Failed to replace nsslapd-close-on-failed-bind attr')
        raise e

    # bind as non existing entry which triggers connection close
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        inst.simple_bind_s(INVALID_ENTRY, PW_DM)

    # verify the connection is closed but the server is still running
    assert (inst.status())
    with pytest.raises(ldap.SERVER_DOWN):
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'])
    try:
        dm_conn = dm.bind()
    except ldap.LDAPError as e:
        log.error('DM bind failed')
        raise e


def test_bind_disconnect_cert_map_failed(topology_st, _reset_attr):
    """Test close connection on failed bind with a failed cert mapping

    :id: 0ac60f76-1fd9-4080-a82b-21807e6bc292
    :setup: Standalone Instance
    :steps:
        1. enable TLS
        2. create/get a user
        3. get details of ssca key and cert
        4. create 2 user certificates, one good, one bad
        5. configure certmap
        6. check that EXTERNAL is listed in supported mechns.
        7. bind with good cert
        8. bind with bad cert
        9. enable nsslapd-close-on-failed-bind attr
        10. bind with bad cert
        11. verify connection has been closed and server is still running
        12. cleanup
    :expectedresults:
        1. success
        2. success
        3. success
        4. success
        5. success
        6. success
        7. success
        8. generates INVALID_CREDENTIALS exception
        9. success
        10. generates INVALID_CREDENTIALS exception, triggering connection closure
        11. success
        12. success
    """

    RDN_TEST_USER = 'test_user_1000'
    RDN_TEST_USER_WRONG = 'test_user_wrong'
    inst = topology_st.standalone

    inst.enable_tls()
    dm = DirectoryManager(inst)

    # create/get user
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    try:
        user = users.create_test_user()
        user.set("userPassword", PW_DM)
    except ldap.ALREADY_EXISTS:
        user = users.get(RDN_TEST_USER)

    ssca_dir = inst.get_ssca_dir()
    ssca = NssSsl(dbpath=ssca_dir)

    ssca.create_rsa_user(RDN_TEST_USER)
    ssca.create_rsa_user(RDN_TEST_USER_WRONG)

    # Get the details of where the key and crt are.
    tls_locs = ssca.get_rsa_user(RDN_TEST_USER)
    tls_locs_wrong = ssca.get_rsa_user(RDN_TEST_USER_WRONG)

    user.enroll_certificate(tls_locs['crt_der_path'])

    # Turn on the certmap.
    cm = CertmapLegacy(inst)
    certmaps = cm.list()
    certmaps['default']['DNComps'] = ''
    certmaps['default']['FilterComps'] = ['cn']
    certmaps['default']['VerifyCert'] = 'off'
    cm.set(certmaps)

    # Check that EXTERNAL is listed in supported mechns.
    assert(inst.rootdse.supports_sasl_external())

    # Restart to allow certmaps to be re-read: Note, we CAN NOT use post_open
    # here, it breaks on auth. see lib389/__init__.py
    inst.restart(post_open=False)

    # bind with good cert
    try:
        inst.open(saslmethod='EXTERNAL', connOnly=True, certdir=ssca_dir, userkey=tls_locs['key'], usercert=tls_locs['crt'])
    except ldap.LDAPError as e:
        log.error('Bind with good cert failed')
        raise e

    inst.restart()

    # bind with bad cert
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        inst.open(saslmethod='EXTERNAL', connOnly=True, certdir=ssca_dir, userkey=tls_locs_wrong['key'], usercert=tls_locs_wrong['crt'])

    # enable and verify attr
    try:
        dm_conn = dm.bind()
        dm_conn.config.replace('nsslapd-close-on-failed-bind', 'on')
        assert (dm_conn.config.get_attr_val_utf8('nsslapd-close-on-failed-bind')) == 'on'
    except ldap.LDAPError as e:
        log.error('Failed to replace nsslapd-close-on-failed-bind attr')
        raise e

    # bind with bad cert
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        inst.open(saslmethod='EXTERNAL', connOnly=True, certdir=ssca_dir, userkey=tls_locs_wrong['key'], usercert=tls_locs_wrong['crt'])

    # check the connection is closed but the server is still running
    assert (inst.status())
    with pytest.raises(ldap.SERVER_DOWN):
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'])
    try:
        dm_conn = dm.bind()
    except ldap.LDAPError as e:
        log.error('DM bind failed')
        raise e


def test_bind_disconnect_account_lockout(topology_st, _reset_attr):
    """Test close connection on failed bind with user account lockout

    :id: 12e56d79-ce57-4574-a80a-d3b6d1d74d8f
    :setup: Standalone Instance
    :steps:
        1. configure account lockout
        2. create/get a user
        3. bind and search as user
        4. force account lock out
        5. enable nsslapd-close-on-failed-bind attr
        6. attempt user bind
        7. verify connection has been closed and server is still running
        8. cleanup
    :expectedresults:
        1. success
        2. success
        3. success
        4. generates CONSTRAINT_VIOLATION exception
        5. success
        6. generates CONSTRAINT_VIOLATION exception, triggering connection closure
        7. success
        8. success
    """

    inst = topology_st.standalone
    dm = DirectoryManager(inst)
    inst.config.set('passwordlockout', 'on')
    inst.config.set('passwordMaxFailure', '2')

    # create/get user
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    try:
        user = users.create_test_user()
        user.set("userPassword", PW_DM)
    except ldap.ALREADY_EXISTS:
        user = users.get('test_user_1000')

    # verify user bind and search
    try:
        inst.simple_bind_s(user.dn, PW_DM)
    except ldap.LDAPError as e:
        log.error('Failed to bind {}'.format(user.dn))
        raise e
    try:
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'])
    except ldap.LDAPError as e:
        log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
        raise e

    # Force entry to get locked out
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        inst.simple_bind_s(user.dn, 'whateverlike')
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        inst.simple_bind_s(user.dn, 'whateverlike')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        # Should fail with good or bad password
        inst.simple_bind_s(user.dn, PW_DM)

    # enable and verify attr
    try:
        dm_conn = dm.bind()
        dm_conn.config.replace('nsslapd-close-on-failed-bind', 'on')
        assert (dm_conn.config.get_attr_val_utf8('nsslapd-close-on-failed-bind')) == 'on'
    except ldap.LDAPError as e:
        log.error('Failed to replace nsslapd-close-on-failed-bind attr')
        raise e

    # Should fail with good or bad password
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        inst.simple_bind_s(user.dn, PW_DM)

    # check the connection is closed but the server is still running
    assert (inst.status())
    with pytest.raises(ldap.SERVER_DOWN):
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'])
    try:
        dm_conn = dm.bind()
    except ldap.LDAPError as e:
        log.error('DM bind failed')
        raise e


def test_dscreate(request):
    """Test that dscreate works

    :id: 5bf75c47-a283-430e-a65c-3c5fd8dbadb9
    :setup: None
    :steps:
        1. Create template file for dscreate
        2. Create instance using template file
    :expectedresults:
        1. Should succeeds
        2. Should succeeds
    """

    template_file = "/tmp/dssetup.inf"
    template_text = """[general]
config_version = 2
# This invalid hostname ...
full_machine_name = localhost.localdomain
# Means we absolutely require this.
strict_host_checking = False
# In tests, we can be run in containers, NEVER trust
# that systemd is there, or functional in any capacity
systemd = False

[slapd]
instance_name = test_dscreate
root_dn = cn=directory manager
root_password = someLongPassword_123
# We do not have access to high ports in containers,
# so default to something higher.
port = 38999
secure_port = 63699


[backend-userroot]
suffix = dc=example,dc=com
sample_entries = yes
"""

    with open(template_file, "w") as template_fd:
        template_fd.write(template_text)

    # Unset PYTHONPATH to avoid mixing old CLI tools and new lib389
    tmp_env = os.environ
    if "PYTHONPATH" in tmp_env:
        del tmp_env["PYTHONPATH"]
    try:
        subprocess.check_call([
            'dscreate',
            'from-file',
            template_file
        ], env=tmp_env)
    except subprocess.CalledProcessError as e:
        log.fatal("dscreate failed!  Error ({}) {}".format(e.returncode, e.output))
        assert False

    def fin():
        os.remove(template_file)
        try:
            subprocess.check_call(['dsctl', 'test_dscreate', 'remove', '--do-it'])
        except subprocess.CalledProcessError as e:
            log.fatal("Failed to remove test instance  Error ({}) {}".format(e.returncode, e.output))

    request.addfinalizer(fin)


def test_dscreate_with_replication(request):
    """Test dscreate works with replication shortcuts

    :id: 8391ffc4-5158-4141-9312-0f47ae56f1ed
    :setup: Standalone Instance
    :steps:
        1. Create instance and prepare DirSrv object
        2. Check replication is enabled
        3. Check repl role
        4. Check rid
        5. Check bind dn
        6. Changelog trimming settings
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    template_file = "/tmp/dssetup.inf"
    template_text = """[general]
config_version = 2
# This invalid hostname ...
full_machine_name = localhost.localdomain
# Means we absolutely require this.
strict_host_checking = False
# In tests, we can be run in containers, NEVER trust
# that systemd is there, or functional in any capacity
systemd = False

[slapd]
instance_name = dscreate_repl
root_dn = cn=directory manager
root_password = someLongPassword_123
# We do not have access to high ports in containers,
# so default to something higher.
port = 38999
secure_port = 63699

[backend-userroot]
suffix = dc=example,dc=com
sample_entries = yes
enable_replication = True
replica_binddn = cn=replication manager,cn=config
replica_bindpw = password
replica_id = 111
replica_role = supplier
changelog_max_age = 8d
changelog_max_entries = 200000
"""

    with open(template_file, "w") as template_fd:
        template_fd.write(template_text)

    # Unset PYTHONPATH to avoid mixing old CLI tools and new lib389
    tmp_env = os.environ
    if "PYTHONPATH" in tmp_env:
        del tmp_env["PYTHONPATH"]
    try:
        subprocess.check_call([
            'dscreate',
            'from-file',
            template_file
        ], env=tmp_env)
    except subprocess.CalledProcessError as e:
        log.fatal("dscreate failed!  Error ({}) {}".format(e.returncode, e.output))
        assert False

    def fin():
        os.remove(template_file)
        try:
            subprocess.check_call(['dsctl', 'dscreate_repl', 'remove', '--do-it'])
        except subprocess.CalledProcessError as e:
            log.fatal("Failed to remove test instance  Error ({}) {}".format(e.returncode, e.output))

    request.addfinalizer(fin)

    # Prepare Dirsrv instance
    from lib389 import DirSrv
    container_result = subprocess.run(["systemd-detect-virt", "-c"], stdout=subprocess.PIPE)
    if container_result.returncode == 0:
        ds_instance = DirSrv(False, containerised=True)
    else:
        ds_instance = DirSrv(False)
    args = {
        SER_HOST: "localhost.localdomain",
        SER_PORT: 38999,
        SER_SECURE_PORT: 63699,
        SER_SERVERID_PROP: 'dscreate_repl',
        SER_ROOT_DN: 'cn=directory manager',
        SER_ROOT_PW: 'someLongPassword_123',
        SER_LDAPI_ENABLED: 'on',
        SER_LDAPI_AUTOBIND: 'on'
    }
    ds_instance.allocate(args)
    ds_instance.start(timeout=60)

    dse_ldif = DSEldif(ds_instance, serverid="dscreate_repl")
    socket_path = dse_ldif.get("cn=config", "nsslapd-ldapifilepath")
    ldapiuri=f"ldapi://{socket_path[0].replace('/', '%2f')}"
    ds_instance.open(uri=ldapiuri)

    # Check replication is enabled
    replicas = Replicas(ds_instance)
    replica = replicas.get(DEFAULT_SUFFIX)
    assert replica

    # Check role
    assert replica.get_role() == ReplicaRole.SUPPLIER

    # Check rid
    assert replica.get_rid() == '111'

    # Check bind dn is in config
    assert replica.get_attr_val_utf8('nsDS5ReplicaBindDN') == 'cn=replication manager,cn=config'

    # Check repl manager entry was created
    repl_mgr = UserAccount(ds_instance, 'cn=replication manager,cn=config')
    assert repl_mgr.exists()

    # Changelog trimming settings
    cl = Changelog(ds_instance, DEFAULT_SUFFIX)
    assert cl.get_attr_val_utf8('nsslapd-changelogmaxage') == '8d'
    assert cl.get_attr_val_utf8('nsslapd-changelogmaxentries') == '200000'


@pytest.fixture(scope="function")
def dscreate_long_instance(request):
    template_file = "/tmp/dssetup.inf"
    longname_serverid = "test-longname-deadbeef-deadbeef-deadbeef-deadbeef-deadbeef"
    template_text = """[general]
config_version = 2
# This invalid hostname ...
full_machine_name = localhost.localdomain
# Means we absolutely require this.
strict_host_checking = False
# In tests, we can be run in containers, NEVER trust
# that systemd is there, or functional in any capacity
systemd = False

[slapd]
instance_name = %s
root_dn = cn=directory manager
root_password = someLongPassword_123
# We do not have access to high ports in containers,
# so default to something higher.
port = 38999
secure_port = 63699


[backend-userroot]
suffix = dc=example,dc=com
sample_entries = yes
""" % longname_serverid

    with open(template_file, "w") as template_fd:
        template_fd.write(template_text)

    # Unset PYTHONPATH to avoid mixing old CLI tools and new lib389
    tmp_env = os.environ
    if "PYTHONPATH" in tmp_env:
        del tmp_env["PYTHONPATH"]
    try:
        subprocess.check_call([
            'dscreate',
            'from-file',
            template_file
        ], env=tmp_env)
    except subprocess.CalledProcessError as e:
        log.fatal("dscreate failed!  Error ({}) {}".format(e.returncode, e.output))
        assert False

    inst = DirSrv(verbose=True, external_log=log)
    dse_ldif = DSEldif(inst, serverid=longname_serverid)

    socket_path = dse_ldif.get("cn=config", "nsslapd-ldapifilepath")
    inst.local_simple_allocate(
       serverid=longname_serverid,
       ldapuri=f"ldapi://{socket_path[0].replace('/', '%2f')}",
       password="someLongPassword_123"
    )
    inst.ldapi_enabled = 'on'
    inst.ldapi_socket = socket_path
    inst.ldapi_autobind = 'off'
    try:
        inst.open()
    except:
        log.fatal("Failed to connect via ldapi to %s instance" % longname_serverid)
        os.remove(template_file)
        try:
            subprocess.check_call(['dsctl', longname_serverid, 'remove', '--do-it'])
        except subprocess.CalledProcessError as e:
            log.fatal("Failed to remove test instance  Error ({}) {}".format(e.returncode, e.output))

    def fin():
        os.remove(template_file)
        try:
            subprocess.check_call(['dsctl', longname_serverid, 'remove', '--do-it'])
        except subprocess.CalledProcessError as e:
            log.fatal("Failed to remove test instance  Error ({}) {}".format(e.returncode, e.output))

    request.addfinalizer(fin)

    return inst


@pytest.mark.skipif(not get_user_is_root() or ds_is_older('1.4.2.0'),
                    reason="This test is only required with new admin cli, and requires root.")
@pytest.mark.bz1748016
@pytest.mark.ds50581
def test_dscreate_ldapi(dscreate_long_instance):
    """Test that an instance with a long name can
    handle ldapi connection using a long socket name

    :id: 5d72d955-aff8-4741-8c9a-32c1c707cf1f
    :setup: None
    :steps:
        1. Ccreate an instance with a long serverId name, that open a ldapi connection
        2. Connect with ldapi, that hit 50581 and crash the instance
    :expectedresults:
        1. Should succeeds
        2. Should succeeds
    """

    root_dse = RootDSE(dscreate_long_instance)
    log.info(root_dse.get_supported_ctrls())


@pytest.mark.skipif(not get_user_is_root() or ds_is_older('1.4.2.0'),
                    reason="This test is only required with new admin cli, and requires root.")
@pytest.mark.bz1715406
@pytest.mark.ds50923
def test_dscreate_multiple_dashes_name(dscreate_long_instance):
    """Test that an instance with a multiple dashes in the name
    can be removed with dsctl --remove-all

    :id: 265c3ac7-5ba6-4278-b8f4-4e7692afd1a5
    :setup: An instance with a few dashes in its name
    :steps:
        1. Run 'dsctl --remove-all' command
        2. Check if the instance exists
    :expectedresults:
        1. Should succeeds
        2. Instance doesn't exists
    """

    p = run(['dsctl', '--remove-all'], stdout=PIPE, input='Yes\n', encoding='ascii')
    assert not dscreate_long_instance.exists()


@pytest.fixture(scope="module", params=('c=uk', 'cn=test_user', 'dc=example,dc=com', 'o=south', 'ou=sales', 'wrong=some_value'))
def dscreate_test_rdn_value(request):
    template_file = "/tmp/dssetup.inf"
    template_text = f"""[general]
config_version = 2
# This invalid hostname ...
full_machine_name = localhost.localdomain
# Means we absolutely require this.
strict_host_checking = False
# In tests, we can be run in containers, NEVER trust
# that systemd is there, or functional in any capacity
systemd = False

[slapd]
instance_name = test_different_rdn
root_dn = cn=directory manager
root_password = someLongPassword_123
# We do not have access to high ports in containers,
# so default to something higher.
port = 38999
secure_port = 63699

[backend-userroot]
create_suffix_entry = True
suffix = {request.param}
"""

    with open(template_file, "w") as template_fd:
        template_fd.write(template_text)

    # Unset PYTHONPATH to avoid mixing old CLI tools and new lib389
    tmp_env = os.environ
    if "PYTHONPATH" in tmp_env:
        del tmp_env["PYTHONPATH"]

    def fin():
        os.remove(template_file)
        if request.param != "wrong=some_value":
            try:
                subprocess.check_call(['dsctl', 'test_different_rdn', 'remove', '--do-it'])
            except subprocess.CalledProcessError as e:
                log.fatal(f"Failed to remove test instance  Error ({e.returncode}) {e.output}")
        else:
            log.info("Wrong RDN is passed, instance not created")
    request.addfinalizer(fin)
    return template_file, tmp_env, request.param,


@pytest.mark.skipif(not get_user_is_root() or ds_is_older('1.4.0.0'),
                    reason="This test is only required with new admin cli, and requires root.")
@pytest.mark.bz1807419
@pytest.mark.ds50928
def test_dscreate_with_different_rdn(dscreate_test_rdn_value):
    """Test that dscreate works with different RDN attributes as suffix

    :id: 77ed6300-6a2f-4e79-a862-1f1105f1e3ef
    :customerscenario: True
    :parametrized: yes
    :setup: None
    :steps:
        1. Create template file for dscreate with different RDN attributes as suffix
        2. Create instance using template file
        3. Create instance with 'wrong=some_value' as suffix's RDN attribute
    :expectedresults:
        1. Should succeeds
        2. Should succeeds
        3. Should fail
    """
    try:
        subprocess.check_call([
            'dscreate',
            'from-file',
            dscreate_test_rdn_value[0]
        ], env=dscreate_test_rdn_value[1])
    except subprocess.CalledProcessError as e:
        log.fatal(f"dscreate failed!  Error ({e.returncode}) {e.output}")
        if  dscreate_test_rdn_value[2] != "wrong=some_value":
            assert False
        else:
            assert True


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
