# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from subprocess import check_output

import ldap.sasl
import pytest
from lib389.tasks import *
from lib389.utils import *
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

    ldif = '%s/Example.ldif' % get_data_dir(topology_st.standalone.prefix)
    import_ldif = topology_st.standalone.get_ldif_dir() + "/Example.ldif"
    shutil.copyfile(ldif, import_ldif)
    try:
        topology_st.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                                input_file=import_ldif,
                                                args={TASK_WAIT: True})
    except ValueError:
        log.error('Online import failed')
        assert False


@pytest.fixture(params=ROOTDSE_DEF_ATTR_LIST)
def rootdse_attr(topology_st, request):
    """Adds an attr from the list
    as the default attr to the rootDSE
    """
    # Ensure the server is started and connected
    topology_st.standalone.start()

    RETURN_DEFAULT_OPATTR = "nsslapd-return-default-opattr"
    rootdse_attr_name = request.param

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
    """Test doing adds, mods, modrdns, and deletes"""

    log.info('Running test_basic_ops...')

    USER1_NEWDN = 'cn=user1'
    USER2_NEWDN = 'cn=user2'
    USER3_NEWDN = 'cn=user3'
    NEW_SUPERIOR = 'ou=people,' + DEFAULT_SUFFIX
    USER1_RDN_DN = 'cn=user1,' + DEFAULT_SUFFIX
    USER2_RDN_DN = 'cn=user2,' + DEFAULT_SUFFIX
    USER3_RDN_DN = 'cn=user3,' + NEW_SUPERIOR  # New superior test

    #
    # Adds
    #
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
                                                    'New description')])
    except ldap.LDAPError as e:
        log.error('Failed to add description: error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'description',
                                                    'Modified description')])
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
        assert False

    # Modrdn - New superior
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
    """Test online and offline LDIF imports & exports"""

    log.info('Running test_basic_import_export...')

    tmp_dir = '/tmp'

    #
    # Test online/offline LDIF imports
    #

    # Generate a test ldif (50k entries)
    ldif_dir = topology_st.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/basic_import.ldif'
    try:
        topology_st.standalone.buildLDIF(50000, import_ldif)
    except OSError as e:
        log.fatal('test_basic_import_export: failed to create test ldif,\
                  error: %s - %s' % (e.errno, e.strerror))
        assert False

    # Online
    try:
        topology_st.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                                input_file=import_ldif,
                                                args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_import_export: Online import failed')
        assert False

    # Offline
    if not topology_st.standalone.ldif2db(DEFAULT_BENAME, None, None, None, import_ldif):
        log.fatal('test_basic_import_export: Offline import failed')
        assert False

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
    if not topology_st.standalone.db2ldif(DEFAULT_BENAME, (DEFAULT_SUFFIX,),
                                          None, None, None, export_ldif):
        log.fatal('test_basic_import_export: Failed to run offline db2ldif')
        assert False

    #
    # Cleanup - Import the Example LDIF for the other tests in this suite
    #
    ldif = '%s/Example.ldif' % get_data_dir(topology_st.standalone.prefix)
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
    """Test online and offline back and restore"""

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
    if not topology_st.standalone.db2bak(backup_dir):
        log.fatal('test_basic_backup: Offline backup failed')
        assert False

    # Test offline restore
    if not topology_st.standalone.bak2db(backup_dir):
        log.fatal('test_basic_backup: Offline backup failed')
        assert False

    log.info('test_basic_backup: PASSED')


def test_basic_acl(topology_st, import_example_ldif):
    """Run some basic access control(ACL) tests"""

    log.info('Running test_basic_acl...')

    DENY_ACI = ('(targetattr = "*") (version 3.0;acl "deny user";deny (all)' +
                '(userdn = "ldap:///' + USER1_DN + '");)')

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

    # Make sure Root DN can also search (this also resets the bind dn to the
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
    """The search results are gathered from testing with Example.ldif"""

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
               ('(&(|(uid=m*)(l=santa clara))(roomNumber=22*)(!(roomnumber=2254)))', 4))

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
    """Set the server to referral mode,
    and make sure we recive the referal error(10)
    """

    log.info('Running test_basic_referrals...')

    SUFFIX_CONFIG = 'cn="dc=example,dc=com",cn=mapping tree,cn=config'

    #
    # Set the referral, adn the backend state
    #
    try:
        topology_st.standalone.modify_s(SUFFIX_CONFIG,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-referral',
                                          'ldap://localhost.localdomain:389/o%3dnetscaperoot')])
    except ldap.LDAPError as e:
        log.fatal('test_basic_referrals: Failed to set referral: error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(SUFFIX_CONFIG, [(ldap.MOD_REPLACE,
                                                         'nsslapd-state', 'Referral')])
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
                                                         'nsslapd-state', 'Backend')])
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
    """Test systemctl/lib389 can stop and start the server.  Also test that start reports an
    error when the instance does not start.  Only for RPM builds
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
    """Test that the ldap agent starts"""

    log.info('Running test_basic_ldapagent...')

    var_dir = topology_st.standalone.get_local_state_dir()
    config_file = os.path.join(topology_st.standalone.get_sysconf_dir(), 'dirsrv/config/agent.conf')
    cmd = 'sudo %s %s' % (os.path.join(topology_st.standalone.get_sbin_dir(), 'ldap-agent'), config_file)

    agent_config_file = open(config_file, 'w')
    agent_config_file.write('agentx-master ' + var_dir + '/agentx/master\n')
    agent_config_file.write('agent-logdir ' + var_dir + '/log/dirsrv\n')
    agent_config_file.write('server slapd-' + topology_st.standalone.serverid + '\n')
    agent_config_file.close()

    rc = os.system(cmd)
    if rc != 0:
        log.fatal('test_basic_ldapagent: Failed to start snmp ldap agent %s: error %d' % (cmd, rc))
        assert False

    log.info('snmp ldap agent started')

    #
    # Cleanup - kill the agent
    #
    pid = check_output(['pidof', '-s', 'ldap-agent-bin'])
    log.info('Cleanup - killing agent: ' + pid)
    rc = os.system('sudo kill -9 ' + pid)

    log.info('test_basic_ldapagent: PASSED')


def test_basic_dse(topology_st, import_example_ldif):
    """Test that the dse.ldif is not wipped out
    after the process is killed (bug 910581)
    """

    log.info('Running test_basic_dse...')

    dse_file = topology_st.standalone.confdir + '/dse.ldif'
    pid = check_output(['pidof', '-s', 'ns-slapd'])
    os.system('sudo kill -9 ' + pid)
    if os.path.getsize(dse_file) == 0:
        log.fatal('test_basic_dse: dse.ldif\'s content was incorrectly removed!')
        assert False

    topology_st.standalone.start(timeout=60)
    log.info('dse.ldif was not corrupted, and the server was restarted')

    log.info('test_basic_dse: PASSED')


@pytest.mark.parametrize("rootdse_attr_name", ROOTDSE_DEF_ATTR_LIST)
def test_def_rootdse_attr(topology_st, import_example_ldif, rootdse_attr_name):
    """Tests that operational attributes
    are not returned by default in rootDSE searches
    """

    topology_st.standalone.start()

    log.info("        Assert rootdse search hasn't %s attr" % rootdse_attr_name)
    try:
        entries = topology_st.standalone.search_s("", ldap.SCOPE_BASE)
        entry = str(entries[0])
        assert rootdse_attr_name not in entry

    except ldap.LDAPError as e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        assert False


def test_mod_def_rootdse_attr(topology_st, import_example_ldif, rootdse_attr):
    """Tests that operational attributes are returned
    by default in rootDSE searches after config modification
    """

    log.info("        Assert rootdse search has %s attr" % rootdse_attr)
    try:
        entries = topology_st.standalone.search_s("", ldap.SCOPE_BASE)
        entry = str(entries[0])
        assert rootdse_attr in entry

    except ldap.LDAPError as e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
