# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import ldap.sasl
import logging
import pytest
import shutil
from subprocess import check_output
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

log = logging.getLogger(__name__)

installation_prefix = None

# Globals
USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX
USER3_DN = 'uid=user3,' + DEFAULT_SUFFIX


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    """This fixture is used to standalone topology for the 'module'."""

    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix

    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    # Delete each instance in the end
    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


@pytest.fixture(scope="module")
def import_example_ldif(topology):
    """Import the Example LDIF for the tests in this suite"""

    log.info('Initializing the "basic" test suite')

    import_ldif = '%s/Example.ldif' % get_data_dir(topology.standalone.prefix)
    try:
        topology.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                             input_file=import_ldif,
                                             args={TASK_WAIT: True})
    except ValueError:
        log.error('Online import failed')
        assert False


def test_basic_ops(topology, import_example_ldif):
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
        topology.standalone.add_s(Entry((USER1_DN,
                                         {'objectclass': "top extensibleObject".split(),
                                          'sn': '1',
                                          'cn': 'user1',
                                          'uid': 'user1',
                                          'userpassword': 'password'})))
    except ldap.LDAPError, e:
        log.error('Failed to add test user' + USER1_DN + ': error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((USER2_DN,
                                         {'objectclass': "top extensibleObject".split(),
                                          'sn': '2',
                                          'cn': 'user2',
                                          'uid': 'user2',
                                          'userpassword': 'password'})))
    except ldap.LDAPError, e:
        log.error('Failed to add test user' + USER2_DN + ': error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((USER3_DN,
                                         {'objectclass': "top extensibleObject".split(),
                                          'sn': '3',
                                          'cn': 'user3',
                                          'uid': 'user3',
                                          'userpassword': 'password'})))
    except ldap.LDAPError, e:
        log.error('Failed to add test user' + USER3_DN + ': error ' + e.message['desc'])
        assert False

    #
    # Mods
    #
    try:
        topology.standalone.modify_s(USER1_DN, [(ldap.MOD_ADD, 'description',
                                                 'New description')])
    except ldap.LDAPError, e:
        log.error('Failed to add description: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'description',
                                                 'Modified description')])
    except ldap.LDAPError, e:
        log.error('Failed to modify description: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.modify_s(USER1_DN, [(ldap.MOD_DELETE, 'description',
                                                 None)])
    except ldap.LDAPError, e:
        log.error('Failed to delete description: error ' + e.message['desc'])
        assert False

    #
    # Modrdns
    #
    try:
        topology.standalone.rename_s(USER1_DN, USER1_NEWDN, delold=1)
    except ldap.LDAPError, e:
        log.error('Failed to modrdn user1: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.rename_s(USER2_DN, USER2_NEWDN, delold=0)
    except ldap.LDAPError, e:
        log.error('Failed to modrdn user2: error ' + e.message['desc'])
        assert False

    # Modrdn - New superior
    try:
        topology.standalone.rename_s(USER3_DN, USER3_NEWDN,
                                     newsuperior=NEW_SUPERIOR, delold=1)
    except ldap.LDAPError, e:
        log.error('Failed to modrdn(new superior) user3: error ' + e.message['desc'])
        assert False

    #
    # Deletes
    #
    try:
        topology.standalone.delete_s(USER1_RDN_DN)
    except ldap.LDAPError, e:
        log.error('Failed to delete test entry1: ' + e.message['desc'])
        assert False

    try:
        topology.standalone.delete_s(USER2_RDN_DN)
    except ldap.LDAPError, e:
        log.error('Failed to delete test entry2: ' + e.message['desc'])
        assert False

    try:
        topology.standalone.delete_s(USER3_RDN_DN)
    except ldap.LDAPError, e:
        log.error('Failed to delete test entry3: ' + e.message['desc'])
        assert False

    log.info('test_basic_ops: PASSED')


def test_basic_import_export(topology, import_example_ldif):
    """Test online and offline LDIF imports & exports"""

    log.info('Running test_basic_import_export...')

    tmp_dir = topology.standalone.getDir(__file__, TMP_DIR)

    #
    # Test online/offline LDIF imports
    #

    # Generate a test ldif (50k entries)
    import_ldif = tmp_dir + '/basic_import.ldif'
    try:
        topology.standalone.buildLDIF(50000, import_ldif)
    except OSError as e:
        log.fatal('test_basic_import_export: failed to create test ldif,\
                  error: %s - %s' % (e.errno, e.strerror))
        assert False

    # Online
    try:
        topology.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                             input_file=import_ldif,
                                             args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_import_export: Online import failed')
        assert False

    # Offline
    if not topology.standalone.ldif2db(DEFAULT_BENAME, None, None, None, import_ldif):
        log.fatal('test_basic_import_export: Offline import failed')
        assert False

    #
    # Test online and offline LDIF export
    #

    # Online export
    export_ldif = tmp_dir + 'export.ldif'
    exportTask = Tasks(topology.standalone)
    try:
        args = {TASK_WAIT: True}
        exportTask.exportLDIF(DEFAULT_SUFFIX, None, export_ldif, args)
    except ValueError:
        log.fatal('test_basic_import_export: Online export failed')
        assert False

    # Offline export
    if not topology.standalone.db2ldif(DEFAULT_BENAME, (DEFAULT_SUFFIX,),
                                       None, None, None, export_ldif):
        log.fatal('test_basic_import_export: Failed to run offline db2ldif')
        assert False

    #
    # Cleanup - Import the Example LDIF for the other tests in this suite
    #
    import_ldif = '%s/Example.ldif' % get_data_dir(topology.standalone.prefix)
    try:
        topology.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                             input_file=import_ldif,
                                             args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_import_export: Online import failed')
        assert False

    log.info('test_basic_import_export: PASSED')


def test_basic_backup(topology, import_example_ldif):
    """Test online and offline back and restore"""

    log.info('Running test_basic_backup...')

    backup_dir = '%sbasic_backup/' % topology.standalone.getDir(__file__, TMP_DIR)

    # Test online backup
    try:
        topology.standalone.tasks.db2bak(backup_dir=backup_dir,
                                         args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_backup: Online backup failed')
        assert False

    # Test online restore
    try:
        topology.standalone.tasks.bak2db(backup_dir=backup_dir,
                                         args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_basic_backup: Online restore failed')
        assert False

    # Test offline backup
    if not topology.standalone.db2bak(backup_dir):
        log.fatal('test_basic_backup: Offline backup failed')
        assert False

    # Test offline restore
    if not topology.standalone.bak2db(backup_dir):
        log.fatal('test_basic_backup: Offline backup failed')
        assert False

    log.info('test_basic_backup: PASSED')


def test_basic_acl(topology, import_example_ldif):
    """Run some basic access control(ACL) tests"""

    log.info('Running test_basic_acl...')

    DENY_ACI = ('(targetattr = "*") (version 3.0;acl "deny user";deny (all)' +
                '(userdn = "ldap:///' + USER1_DN + '");)')

    #
    # Add two users
    #
    try:
        topology.standalone.add_s(Entry((USER1_DN,
                                         {'objectclass': "top extensibleObject".split(),
                                          'sn': '1',
                                          'cn': 'user 1',
                                          'uid': 'user1',
                                          'userpassword': PASSWORD})))
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Failed to add test user ' + USER1_DN
                  + ': error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((USER2_DN,
                                         {'objectclass': "top extensibleObject".split(),
                                          'sn': '2',
                                          'cn': 'user 2',
                                          'uid': 'user2',
                                          'userpassword': PASSWORD})))
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Failed to add test user ' + USER1_DN
                  + ': error ' + e.message['desc'])
        assert False

    #
    # Add an aci that denies USER1 from doing anything,
    # and also set the default anonymous access
    #
    try:
        topology.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', DENY_ACI)])
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Failed to add DENY ACI: error ' + e.message['desc'])
        assert False

    #
    # Make sure USER1_DN can not search anything, but USER2_dn can...
    #
    try:
        topology.standalone.simple_bind_s(USER1_DN, PASSWORD)
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Failed to bind as user1, error: ' + e.message['desc'])
        assert False

    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX,
                                               ldap.SCOPE_SUBTREE,
                                               '(uid=*)')
        if entries:
            log.fatal('test_basic_acl: User1 was incorrectly able to search the suffix!')
            assert False
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Search suffix failed(as user1): ' + e.message['desc'])
        assert False

    # Now try user2...  Also check that userpassword is stripped out
    try:
        topology.standalone.simple_bind_s(USER2_DN, PASSWORD)
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Failed to bind as user2, error: ' + e.message['desc'])
        assert False

    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX,
                                               ldap.SCOPE_SUBTREE,
                                               '(uid=user1)')
        if not entries:
            log.fatal('test_basic_acl: User1 incorrectly not able to search the suffix')
            assert False
        if entries[0].hasAttr('userpassword'):
            # The default anonymous access aci should have stripped out userpassword
            log.fatal('test_basic_acl: User2 was incorrectly able to see userpassword')
            assert False
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Search for user1 failed(as user2): ' + e.message['desc'])
        assert False

    # Make sure Root DN can also search (this also resets the bind dn to the
    # Root DN for future operations)
    try:
        topology.standalone.simple_bind_s(DN_DM, PW_DM)
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Failed to bind as ROotDN, error: ' + e.message['desc'])
        assert False

    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX,
                                               ldap.SCOPE_SUBTREE,
                                               '(uid=*)')
        if not entries:
            log.fatal('test_basic_acl: Root DN incorrectly not able to search the suffix')
            assert False
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Search for user1 failed(as user2): ' + e.message['desc'])
        assert False

    #
    # Cleanup
    #
    try:
        topology.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_DELETE, 'aci', DENY_ACI)])
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Failed to delete DENY ACI: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.delete_s(USER1_DN)
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Failed to delete test entry1: ' + e.message['desc'])
        assert False

    try:
        topology.standalone.delete_s(USER2_DN)
    except ldap.LDAPError, e:
        log.fatal('test_basic_acl: Failed to delete test entry2: ' + e.message['desc'])
        assert False

    log.info('test_basic_acl: PASSED')


def test_basic_searches(topology, import_example_ldif):
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
            entries = topology.standalone.search_s(DEFAULT_SUFFIX,
                                                   ldap.SCOPE_SUBTREE,
                                                   search_filter)
            if len(entries) != search_result:
                log.fatal('test_basic_searches: An incorrect number of entries\
                          was returned from filter (%s): (%d) expected (%d)' %
                          (search_filter, len(entries), search_result))
                assert False
        except ldap.LDAPError, e:
            log.fatal('Search failed: ' + e.message['desc'])
            assert False

    log.info('test_basic_searches: PASSED')


def test_basic_referrals(topology, import_example_ldif):
    """Set the server to referral mode,
    and make sure we recive the referal error(10)
    """

    log.info('Running test_basic_referrals...')

    SUFFIX_CONFIG = 'cn="dc=example,dc=com",cn=mapping tree,cn=config'

    #
    # Set the referral, adn the backend state
    #
    try:
        topology.standalone.modify_s(SUFFIX_CONFIG,
                                     [(ldap.MOD_REPLACE,
                                       'nsslapd-referral',
                                       'ldap://localhost.localdomain:389/o%3dnetscaperoot')])
    except ldap.LDAPError, e:
        log.fatal('test_basic_referrals: Failed to set referral: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.modify_s(SUFFIX_CONFIG, [(ldap.MOD_REPLACE,
                                                      'nsslapd-state', 'Referral')])
    except ldap.LDAPError, e:
        log.fatal('test_basic_referrals: Failed to set backend state: error '
                  + e.message['desc'])
        assert False

    #
    # Test that a referral error is returned
    #
    topology.standalone.set_option(ldap.OPT_REFERRALS, 0)  # Do not follow referral
    try:
        topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=top')
    except ldap.REFERRAL:
        pass
    except ldap.LDAPError, e:
        log.fatal('test_basic_referrals: Search failed: ' + e.message['desc'])
        assert False

    #
    # Make sure server can restart in referral mode
    #
    topology.standalone.restart(timeout=10)

    #
    # Cleanup
    #
    try:
        topology.standalone.modify_s(SUFFIX_CONFIG, [(ldap.MOD_REPLACE,
                                                      'nsslapd-state', 'Backend')])
    except ldap.LDAPError, e:
        log.fatal('test_basic_referrals: Failed to set backend state: error '
                  + e.message['desc'])
        assert False

    try:
        topology.standalone.modify_s(SUFFIX_CONFIG, [(ldap.MOD_DELETE,
                                                      'nsslapd-referral', None)])
    except ldap.LDAPError, e:
        log.fatal('test_basic_referrals: Failed to delete referral: error '
                  + e.message['desc'])
        assert False
    topology.standalone.set_option(ldap.OPT_REFERRALS, 1)

    log.info('test_basic_referrals: PASSED')


def test_basic_systemctl(topology, import_example_ldif):
    """Test systemctl can stop and start the server.  Also test that start reports an
    error when the instance does not start.  Only for RPM builds
    """

    log.info('Running test_basic_systemctl...')

    # We can only use systemctl on RPM installations
    if topology.standalone.prefix and topology.standalone.prefix != '/':
        return

    data_dir = topology.standalone.getDir(__file__, DATA_DIR)
    tmp_dir = topology.standalone.getDir(__file__, TMP_DIR)
    config_dir = topology.standalone.confdir
    start_ds = 'sudo systemctl start dirsrv@' + topology.standalone.serverid + '.service'
    stop_ds = 'sudo systemctl stop dirsrv@' + topology.standalone.serverid + '.service'
    is_running = 'sudo systemctl is-active dirsrv@' + topology.standalone.serverid + '.service'

    #
    # Stop the server
    #
    log.info('Stopping the server...')
    rc = os.system(stop_ds)
    log.info('Check the status...')
    if rc != 0 or os.system(is_running) == 0:
        log.fatal('test_basic_systemctl: Failed to stop the server')
        assert False
    log.info('Stopped the server.')

    #
    # Start the server
    #
    log.info('Starting the server...')
    rc = os.system(start_ds)
    log.info('Check the status...')
    if rc != 0 or os.system(is_running) != 0:
        log.fatal('test_basic_systemctl: Failed to start the server')
        assert False
    log.info('Started the server.')

    #
    # Stop the server, break the dse.ldif so a start fails,
    # and verify that systemctl detects the failed start
    #
    log.info('Stopping the server...')
    rc = os.system(stop_ds)
    log.info('Check the status...')
    if rc != 0 or os.system(is_running) == 0:
        log.fatal('test_basic_systemctl: Failed to stop the server')
        assert False
    log.info('Stopped the server before breaking the dse.ldif.')

    shutil.copy(config_dir + '/dse.ldif', tmp_dir)
    shutil.copy(data_dir + 'basic/dse.ldif.broken', config_dir + '/dse.ldif')

    log.info('Attempting to start the server with broken dse.ldif...')
    rc = os.system(start_ds)
    log.info('Check the status...')
    if rc == 0 or os.system(is_running) == 0:
        log.fatal('test_basic_systemctl: The server incorrectly started')
        assert False
    log.info('Server failed to start as expected')

    #
    # Fix the dse.ldif, and make sure the server starts up,
    # and systemctl correctly identifies the successful start
    #
    shutil.copy(tmp_dir + 'dse.ldif', config_dir)
    log.info('Starting the server...')
    rc = os.system(start_ds)
    time.sleep(10)
    log.info('Check the status...')
    if rc != 0 or os.system(is_running) != 0:
        log.fatal('test_basic_systemctl: Failed to start the server')
        assert False
    log.info('Server started after fixing dse.ldif.')
    time.sleep(1)

    log.info('test_basic_systemctl: PASSED')


def test_basic_ldapagent(topology, import_example_ldif):
    """Test that the ldap agent starts"""

    log.info('Running test_basic_ldapagent...')

    tmp_dir = topology.standalone.getDir(__file__, TMP_DIR)
    var_dir = topology.standalone.prefix + '/var'
    config_file = tmp_dir + '/agent.conf'
    cmd = 'sudo %s/ldap-agent %s' % (get_sbin_dir(prefix=topology.standalone.prefix),
                                     config_file)

    agent_config_file = open(config_file, 'w')
    agent_config_file.write('agentx-master ' + var_dir + '/agentx/master\n')
    agent_config_file.write('agent-logdir ' + var_dir + '/log/dirsrv\n')
    agent_config_file.write('server slapd-' + topology.standalone.serverid + '\n')
    agent_config_file.close()

    rc = os.system(cmd)
    if rc != 0:
        log.fatal('test_basic_ldapagent: Failed to start snmp ldap agent: error %d' % rc)
        assert False

    log.info('snmp ldap agent started')

    #
    # Cleanup - kill the agent
    #
    pid = check_output(['pidof', '-s', 'ldap-agent-bin'])
    log.info('Cleanup - killing agent: ' + pid)
    rc = os.system('sudo kill -9 ' + pid)

    log.info('test_basic_ldapagent: PASSED')


def test_basic_dse(topology, import_example_ldif):
    """Test that the dse.ldif is not wipped out
    after the process is killed (bug 910581)
    """

    log.info('Running test_basic_dse...')

    dse_file = topology.standalone.confdir + '/dse.ldif'
    pid = check_output(['pidof', '-s', 'ns-slapd'])
    os.system('sudo kill -9 ' + pid)
    if os.path.getsize(dse_file) == 0:
        log.fatal('test_basic_dse: dse.ldif\'s content was incorrectly removed!')
        assert False

    topology.standalone.start(timeout=10)
    log.info('dse.ldif was not corrupted, and the server was restarted')

    log.info('test_basic_dse: PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
