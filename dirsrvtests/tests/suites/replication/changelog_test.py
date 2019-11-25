# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import ldap
import ldif
import pytest
from lib389.properties import TASK_WAIT
from lib389.replica import Replicas
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_m2 as topo
from lib389._constants import *
from lib389.plugins import RetroChangelogPlugin
from lib389.dseldif import DSEldif
from lib389.tasks import *
from lib389.utils import *

TEST_ENTRY_NAME = 'replusr'
NEW_RDN_NAME = 'cl5usr'
CHANGELOG = 'cn=changelog5,cn=config'
RETROCHANGELOG = 'cn=Retro Changelog Plugin,cn=plugins,cn=config'
MAXAGE = 'nsslapd-changelogmaxage'
TRIMINTERVAL = 'nsslapd-changelogtrim-interval'
COMPACTDBINTERVAL = 'nsslapd-changelogcompactdb-interval'
FILTER = '(cn=*)'

DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _perform_ldap_operations(topo):
    """Add a test user, modify description, modrdn user and delete it"""

    log.info('Adding user {}'.format(TEST_ENTRY_NAME))
    users = UserAccounts(topo.ms['master1'], DEFAULT_SUFFIX)
    user_properties = {
        'uid': TEST_ENTRY_NAME,
        'cn': TEST_ENTRY_NAME,
        'sn': TEST_ENTRY_NAME,
        'uidNumber': '1001',
        'gidNumber': '2001',
        'userpassword': PASSWORD,
        'description': 'userdesc',
        'homeDirectory': '/home/{}'.format(TEST_ENTRY_NAME)}
    tuser = users.create(properties=user_properties)
    tuser.replace('description', 'newdesc')
    log.info('Modify RDN of user {}'.format(tuser.dn))
    try:
        topo.ms['master1'].modrdn_s(tuser.dn, 'uid={}'.format(NEW_RDN_NAME), 0)
    except ldap.LDAPError as e:
        log.fatal('Failed to modrdn entry {}'.format(tuser.dn))
        raise e
    tuser = users.get(NEW_RDN_NAME)
    log.info('Deleting user: {}'.format(tuser.dn))
    tuser.delete()


def _create_changelog_dump(topo):
    """Dump changelog using nss5task and check if ldap operations are logged"""

    log.info('Dump changelog using nss5task and check if ldap operations are logged')
    changelog_dir = topo.ms['master1'].get_changelog_dir()
    replicas = Replicas(topo.ms["master1"])
    replica = replicas.get(DEFAULT_SUFFIX)
    log.info('Remove ldif files, if present in: {}'.format(changelog_dir))
    for files in os.listdir(changelog_dir):
        if files.endswith('.ldif'):
            changelog_file = os.path.join(changelog_dir, files)
            try:
                os.remove(changelog_file)
            except OSError as e:
                log.fatal('Failed to remove ldif file: {}'.format(changelog_file))
                raise e
            log.info('Existing changelog ldif file: {} removed'.format(changelog_file))
    else:
        log.info('No existing changelog ldif files present')

    log.info('Running nsds5task to dump changelog database to a file')
    replica.begin_task_cl2ldif()

    log.info('Check if changelog ldif file exist in: {}'.format(changelog_dir))
    for files in os.listdir(changelog_dir):
        if files.endswith('.ldif'):
            changelog_ldif = os.path.join(changelog_dir, files)
            log.info('Changelog ldif file exist: {}'.format(changelog_ldif))
            return changelog_ldif
    else:
        log.fatal('Changelog ldif file does not exist in: {}'.format(changelog_dir))
        assert False


def _check_changelog_ldif(topo, changelog_ldif):
    """Check changelog ldif file for required ldap operations"""

    log.info('Checking changelog ldif file for ldap operations')
    assert os.stat(changelog_ldif).st_size > 0, 'Changelog file has no contents'
    with open(changelog_ldif, 'r') as fh:
        content = fh.read()
    ldap_operations = set()
    log.info('Checking if all required changetype operations are present')
    for entry_ldif in content.split('\n\n'):
        for line in entry_ldif.split('\n'):
            if line.startswith('changetype: '):
                ldap_operations.add(line.split(': ')[1])
    valid_operations = set(ldif.valid_changetype_dict.keys())
    log.info('Valid ldap operations: {}'.format(valid_operations))
    log.info('Ldap operations found: {}'.format(ldap_operations))
    assert ldap_operations == valid_operations, 'Changelog ldif file does not contain all \
            changetype operations'


def get_ldap_error_msg(e, type):
    return e.args[0][type]


@pytest.fixture(scope="module")
def changelog_init(topo):
    """Initialize the test environment by changing log dir and
    enabling cn=Retro Changelog Plugin,cn=plugins,cn=config
     """
    log.info('Testing Ticket 47669 - Test duration syntax in the changelogs')

    # bind as directory manager
    topo.ms["master1"].log.info("Bind as %s" % DN_DM)
    topo.ms["master1"].simple_bind_s(DN_DM, PASSWORD)

    try:
        changelogdir = os.path.join(os.path.dirname(topo.ms["master1"].dbdir), 'changelog')
        topo.ms["master1"].modify_s(CHANGELOG, [(ldap.MOD_REPLACE, 'nsslapd-changelogdir',
                                                                    ensure_bytes(changelogdir))])
    except ldap.LDAPError as e:
        log.error('Failed to modify ' + CHANGELOG + ': error {}'.format(get_ldap_error_msg(e,'desc')))
        assert False

    try:
        topo.ms["master1"].modify_s(RETROCHANGELOG, [(ldap.MOD_REPLACE, 'nsslapd-pluginEnabled', b'on')])
    except ldap.LDAPError as e:
        log.error('Failed to enable ' + RETROCHANGELOG + ': error {}'.format(get_ldap_error_msg(e, 'desc')))
        assert False

    # restart the server
    topo.ms["master1"].restart(timeout=10)


def add_and_check(topo, plugin, attr, val, isvalid):
    """
    Helper function to add/replace attr: val and check the added value
    """
    if isvalid:
        log.info('Test %s: %s -- valid' % (attr, val))
        try:
            topo.ms["master1"].modify_s(plugin, [(ldap.MOD_REPLACE, attr, ensure_bytes(val))])
        except ldap.LDAPError as e:
            log.error('Failed to add ' + attr + ': ' + val + ' to ' + plugin + ': error {}'.format(get_ldap_error_msg(e,'desc')))
            assert False
    else:
        log.info('Test %s: %s -- invalid' % (attr, val))
        if plugin == CHANGELOG:
            try:
                topo.ms["master1"].modify_s(plugin, [(ldap.MOD_REPLACE, attr, ensure_bytes(val))])
            except ldap.LDAPError as e:
                log.error('Expectedly failed to add ' + attr + ': ' + val +
                          ' to ' + plugin + ': error {}'.format(get_ldap_error_msg(e,'desc')))
        else:
            try:
                topo.ms["master1"].modify_s(plugin, [(ldap.MOD_REPLACE, attr, ensure_bytes(val))])
            except ldap.LDAPError as e:
                log.error('Failed to add ' + attr + ': ' + val + ' to ' + plugin + ': error {}'.format(get_ldap_error_msg(e,'desc')))

    try:
        entries = topo.ms["master1"].search_s(plugin, ldap.SCOPE_BASE, FILTER, [attr])
        if isvalid:
            if not entries[0].hasValue(attr, val):
                log.fatal('%s does not have expected (%s: %s)' % (plugin, attr, val))
                assert False
        else:
            if plugin == CHANGELOG:
                if entries[0].hasValue(attr, val):
                    log.fatal('%s has unexpected (%s: %s)' % (plugin, attr, val))
                    assert False
            else:
                if not entries[0].hasValue(attr, val):
                    log.fatal('%s does not have expected (%s: %s)' % (plugin, attr, val))
                    assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (plugin, e.message['desc']))
        assert False


def test_verify_changelog(topo):
    """Check if changelog dump file contains required ldap operations

    :id: 15ead076-8c18-410b-90eb-c2fe9eab966b
    :setup: Replication with two masters.
    :steps: 1. Add user to server.
            2. Perform ldap modify, modrdn and delete operations.
            3. Dump the changelog to a file using nsds5task.
            4. Check if changelog is updated with ldap operations.
    :expectedresults:
            1. Add user should PASS.
            2. Ldap operations should PASS.
            3. Changelog should be dumped successfully.
            4. Changelog dump file should contain ldap operations
    """

    log.info('LDAP operations add, modify, modrdn and delete')
    _perform_ldap_operations(topo)
    changelog_ldif = _create_changelog_dump(topo)
    _check_changelog_ldif(topo, changelog_ldif)


def test_verify_changelog_online_backup(topo):
    """Check ldap operations in changelog dump file after online backup

    :id: 4001c34f-35b4-439e-8c2d-fa7e30375219
    :setup: Replication with two masters.
    :steps: 1. Add user to server.
            2. Take online backup using db2bak task.
            3. Restore the database using bak2db task.
            4. Perform ldap modify, modrdn and delete operations.
            5. Dump the changelog to a file using nsds5task.
            6. Check if changelog is updated with ldap operations.
    :expectedresults:
            1. Add user should PASS.
            2. Backup of database should PASS.
            3. Restore of database should PASS.
            4. Ldap operations should PASS.
            5. Changelog should be dumped successfully.
            6. Changelog dump file should contain ldap operations
    """

    backup_dir = os.path.join(topo.ms['master1'].get_bak_dir(), 'online_backup')
    log.info('Run db2bak script to take database backup')
    try:
        topo.ms['master1'].tasks.db2bak(backup_dir=backup_dir, args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_changelog5: Online backup failed')
        assert False

    backup_checkdir = os.path.join(backup_dir, '.repl_changelog_backup', DEFAULT_CHANGELOG_DB)
    if os.path.exists(backup_checkdir):
        log.info('Database backup is created successfully')
    else:
        log.fatal('test_changelog5: backup directory does not exist : {}'.format(backup_checkdir))
        assert False

    log.info('Run bak2db to restore directory server')
    try:
        topo.ms['master1'].tasks.bak2db(backup_dir=backup_dir, args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_changelog5: Online restore failed')
        assert False

    log.info('LDAP operations add, modify, modrdn and delete')
    _perform_ldap_operations(topo)
    changelog_ldif = _create_changelog_dump(topo)
    _check_changelog_ldif(topo, changelog_ldif)


def test_verify_changelog_offline_backup(topo):
    """Check ldap operations in changelog dump file after offline backup

    :id: feed290d-57dd-46e4-9ab3-422c77589867
    :setup: Replication with two masters.
    :steps: 1. Add user to server.
            2. Stop server and take offline backup using db2bak.
            3. Restore the database using bak2db.
            4. Perform ldap modify, modrdn and delete operations.
            5. Start the server and dump the changelog using nsds5task.
            6. Check if changelog is updated with ldap operations.
    :expectedresults:
            1. Add user should PASS.
            2. Backup of database should PASS.
            3. Restore of database should PASS.
            4. Ldap operations should PASS.
            5. Changelog should be dumped successfully.
            6. Changelog dump file should contain ldap operations
    """

    backup_dir = os.path.join(topo.ms['master1'].get_bak_dir(), 'offline_backup')

    topo.ms['master1'].stop()
    log.info('Run db2bak to take database backup')
    try:
        topo.ms['master1'].db2bak(backup_dir)
    except ValueError:
        log.fatal('test_changelog5: Offline backup failed')
        assert False

    log.info('Run bak2db to restore directory server')
    try:
        topo.ms['master1'].bak2db(backup_dir)
    except ValueError:
        log.fatal('test_changelog5: Offline restore failed')
        assert False
    topo.ms['master1'].start()

    backup_checkdir = os.path.join(backup_dir, '.repl_changelog_backup', DEFAULT_CHANGELOG_DB)
    if os.path.exists(backup_checkdir):
        log.info('Database backup is created successfully')
    else:
        log.fatal('test_changelog5: backup directory does not exist : {}'.format(backup_checkdir))
        assert False

    log.info('LDAP operations add, modify, modrdn and delete')
    _perform_ldap_operations(topo)
    changelog_ldif = _create_changelog_dump(topo)
    _check_changelog_ldif(topo, changelog_ldif)


@pytest.mark.ds47669
def test_changelog_maxage(topo, changelog_init):
    """Check nsslapd-changelog max age values

    :id: d284ff27-03b2-412c-ac74-ac4f2d2fae3b
    :setup: Replication with two master, change nsslapd-changelogdir to
    '/var/lib/dirsrv/slapd-master1/changelog' and
    set cn=Retro Changelog Plugin,cn=plugins,cn=config to 'on'
    :steps:
        1. Set nsslapd-changelogmaxage in cn=changelog5,cn=config to values - '12345','10s','30M','12h','2D','4w'
        2. Set nsslapd-changelogmaxage in cn=changelog5,cn=config to values - '-123','xyz'

    :expectedresults:
        1. Operation should be successful
        2. Operation should be unsuccessful
     """
    log.info('1. Test nsslapd-changelogmaxage in cn=changelog5,cn=config')

    # bind as directory manager
    topo.ms["master1"].log.info("Bind as %s" % DN_DM)
    topo.ms["master1"].simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topo, CHANGELOG, MAXAGE, '12345', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '10s', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '30M', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '12h', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '2D', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '4w', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '-123', False)
    add_and_check(topo, CHANGELOG, MAXAGE, 'xyz', False)


@pytest.mark.ds47669
def test_ticket47669_changelog_triminterval(topo, changelog_init):
    """Check nsslapd-changelog triminterval values

    :id: 8f850c37-7e7c-49dd-a4e0-9344638616d6
    :setup: Replication with two master, change nsslapd-changelogdir to
    '/var/lib/dirsrv/slapd-master1/changelog' and
    set cn=Retro Changelog Plugin,cn=plugins,cn=config to 'on'
    :steps:
        1. Set nsslapd-changelogtrim-interval in cn=changelog5,cn=config to values -
           '12345','10s','30M','12h','2D','4w'
        2. Set nsslapd-changelogtrim-interval in cn=changelog5,cn=config to values - '-123','xyz'

    :expectedresults:
        1. Operation should be successful
        2. Operation should be unsuccessful
     """
    log.info('2. Test nsslapd-changelogtrim-interval in cn=changelog5,cn=config')

    # bind as directory manager
    topo.ms["master1"].log.info("Bind as %s" % DN_DM)
    topo.ms["master1"].simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '12345', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '10s', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '30M', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '12h', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '2D', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '4w', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '-123', False)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, 'xyz', False)


@pytest.mark.ds47669
def test_changelog_compactdbinterval(topo, changelog_init):
    """Check nsslapd-changelog compactdbinterval values

    :id: 0f4b3118-9dfa-4c2a-945c-72847b42a48c
    :setup: Replication with two master, change nsslapd-changelogdir to
    '/var/lib/dirsrv/slapd-master1/changelog' and
    set cn=Retro Changelog Plugin,cn=plugins,cn=config to 'on'
    :steps:
        1. Set nsslapd-changelogcompactdb-interval in cn=changelog5,cn=config to values -
           '12345','10s','30M','12h','2D','4w'
        2. Set nsslapd-changelogcompactdb-interval in cn=changelog5,cn=config to values -
           '-123','xyz'

    :expectedresults:
        1. Operation should be successful
        2. Operation should be unsuccessful
     """
    log.info('3. Test nsslapd-changelogcompactdb-interval in cn=changelog5,cn=config')

    # bind as directory manager
    topo.ms["master1"].log.info("Bind as %s" % DN_DM)
    topo.ms["master1"].simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '12345', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '10s', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '30M', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '12h', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '2D', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '4w', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '-123', False)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, 'xyz', False)


@pytest.mark.ds47669
def test_retrochangelog_maxage(topo, changelog_init):
    """Check nsslapd-retrochangelog max age values

    :id: 0cb84d81-3e86-4dbf-84a2-66aefd8281db
    :setup: Replication with two master, change nsslapd-changelogdir to
    '/var/lib/dirsrv/slapd-master1/changelog' and
    set cn=Retro Changelog Plugin,cn=plugins,cn=config to 'on'
    :steps:
        1. Set nsslapd-changelogmaxage in cn=Retro Changelog Plugin,cn=plugins,cn=config to values -
           '12345','10s','30M','12h','2D','4w'
        2. Set nsslapd-changelogmaxage in cn=Retro Changelog Plugin,cn=plugins,cn=config to values -
           '-123','xyz'

    :expectedresults:
        1. Operation should be successful
        2. Operation should be unsuccessful
     """
    log.info('4. Test nsslapd-changelogmaxage in cn=Retro Changelog Plugin,cn=plugins,cn=config')

    # bind as directory manager
    topo.ms["master1"].log.info("Bind as %s" % DN_DM)
    topo.ms["master1"].simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topo, RETROCHANGELOG, MAXAGE, '12345', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '10s', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '30M', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '12h', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '2D', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '4w', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '-123', False)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, 'xyz', False)

    topo.ms["master1"].log.info("ticket47669 was successfully verified.")

@pytest.mark.ds50736
def test_retrochangelog_trimming_crash(topo, changelog_init):
    """Check that when retroCL nsslapd-retrocthangelog contains invalid
    value, then the instance does not crash at shutdown

    :id: 5d9bd7ca-e9bf-4be9-8fc8-902aa5513052
    :setup: Replication with two master, change nsslapd-changelogdir to
    '/var/lib/dirsrv/slapd-master1/changelog' and
    set cn=Retro Changelog Plugin,cn=plugins,cn=config to 'on'
    :steps:
        1. Set nsslapd-changelogmaxage in cn=Retro Changelog Plugin,cn=plugins,cn=config to value '-1'
           This value is invalid. To disable retroCL trimming it should be set to 0
        2. Do several restart
        3. check there is no 'Detected Disorderly Shutdown' message (crash)
        4. restore valid value for nsslapd-changelogmaxage '1w'

    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. Operation should be successful
     """
    log.info('1. Test retroCL trimming crash in cn=Retro Changelog Plugin,cn=plugins,cn=config')

    # set the nsslapd-changelogmaxage directly on dse.ldif
    # because the set value is invalid
    topo.ms["master1"].log.info("ticket50736 start verification")
    topo.ms["master1"].stop()
    retroPlugin = RetroChangelogPlugin(topo.ms["master1"])
    dse_ldif = DSEldif(topo.ms["master1"])
    dse_ldif.replace(retroPlugin.dn, 'nsslapd-changelogmaxage', '-1')
    topo.ms["master1"].start()

    # The crash should be systematic, but just in case do several restart
    # with a delay to let all plugin init
    for i in range(5):
        time.sleep(1)
        topo.ms["master1"].stop()
        topo.ms["master1"].start()

    assert not topo.ms["master1"].detectDisorderlyShutdown()

    topo.ms["master1"].log.info("ticket 50736 was successfully verified.")



if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
