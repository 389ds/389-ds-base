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

TEST_ENTRY_NAME = 'replusr'
NEW_RDN_NAME = 'cl5usr'

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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main('-s {}'.format(CURRENT_FILE))
