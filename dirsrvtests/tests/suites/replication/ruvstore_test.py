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
import pytest
from ldif import LDIFParser
from lib389.replica import Replicas
from lib389.backend import Backends
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_m2 as topo
from lib389._constants import *

pytestmark = pytest.mark.tier1

TEST_ENTRY_NAME = 'rep2lusr'
NEW_RDN_NAME = 'ruvusr'
ATTRIBUTES = ['objectClass', 'nsUniqueId', 'nsds50ruv', 'nsruvReplicaLastModified']
USER_PROPERTIES = {
    'uid': TEST_ENTRY_NAME,
    'cn': TEST_ENTRY_NAME,
    'sn': TEST_ENTRY_NAME,
    'uidNumber': '1001',
    'gidNumber': '2001',
    'userpassword': PASSWORD,
    'description': 'userdesc',
    'homeDirectory': '/home/testuser'
}

DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


class MyLDIF(LDIFParser):
    def __init__(self, input):
        LDIFParser.__init__(self, input)

    def handle(self, dn, entry):
        if 'nsuniqueid=' + REPLICA_RUV_UUID in dn:
            for attr in ATTRIBUTES:
                assert entry.get(attr), 'Failed to find attribute: {}'.format(attr)
                log.info('Attribute found in RUV: {}'.format(attr))


def _perform_ldap_operations(topo):
    """Add a test user, modify description, modrdn user and delete it"""

    users = UserAccounts(topo.ms['supplier1'], DEFAULT_SUFFIX)
    log.info('Adding user to supplier1')
    tuser = users.create(properties=USER_PROPERTIES)
    tuser.replace('description', 'newdesc')
    log.info('Modify RDN of user: {}'.format(tuser.dn))
    try:
        topo.ms['supplier1'].modrdn_s(tuser.dn, 'uid={}'.format(NEW_RDN_NAME), 0)
    except ldap.LDAPError as e:
        log.fatal('Failed to modrdn entry: {}'.format(tuser.dn))
        raise e
    tuser = users.get(NEW_RDN_NAME)
    log.info('Deleting user: {}'.format(tuser.dn))
    tuser.delete()


def _compare_memoryruv_and_databaseruv(topo, operation_type):
    """Compare the memoryruv and databaseruv for ldap operations"""

    log.info('Checking memory ruv for ldap: {} operation'.format(operation_type))
    replicas = Replicas(topo.ms['supplier1'])
    replica = replicas.list()[0]
    memory_ruv = replica.get_attr_val_utf8('nsds50ruv')

    log.info('Checking database ruv for ldap: {} operation'.format(operation_type))
    entry = replicas.get_ruv_entry(DEFAULT_SUFFIX)
    database_ruv = entry.getValues('nsds50ruv')[0]
    assert memory_ruv == database_ruv


def test_ruv_entry_backup(topo):
    """Check if db2ldif stores the RUV details in the backup file

    :id: cbe2c473-8578-4caf-ac0a-841140e41e66
    :setup: Replication with two suppliers.
    :steps: 1. Add user to server.
            2. Perform ldap modify, modrdn and delete operations.
            3. Stop the server and backup the database using db2ldif task.
            4. Start the server and check if correct RUV is stored in the backup file.
    :expectedresults:
            1. Add user should PASS.
            2. Ldap operations should PASS.
            3. Database backup using db2ldif task should PASS.
            4. Backup file should contain the correct RUV details.
    """

    log.info('LDAP operations add, modify, modrdn and delete')
    _perform_ldap_operations(topo)

    output_file = os.path.join(topo.ms['supplier1'].get_ldif_dir(), 'supplier1.ldif')
    log.info('Stopping the server instance to run db2ldif task to create backup file')
    topo.ms['supplier1'].stop()
    topo.ms['supplier1'].db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], excludeSuffixes=[],
                               encrypt=False, repl_data=True, outputfile=output_file)
    log.info('Starting the server after backup')
    topo.ms['supplier1'].start()

    log.info('Checking if backup file contains RUV and required attributes')
    with open(output_file, 'r') as ldif_file:
        parser = MyLDIF(ldif_file)
        parser.parse()


@pytest.mark.xfail(reason="No method to safety access DB ruv currently exists online.")
def test_memoryruv_sync_with_databaseruv(topo):
    """Check if memory ruv and database ruv are synced

    :id: 5f38ac5f-6353-460d-bf60-49cafffda5b3
    :setup: Replication with two suppliers.
    :steps: 1. Add user to server and compare memory ruv and database ruv.
            2. Modify description of user and compare memory ruv and database ruv.
            3. Modrdn of user and compare memory ruv and database ruv.
            4. Delete user and compare memory ruv and database ruv.
    :expectedresults:
            1. For add user, the memory ruv and database ruv should be the same.
            2. For modify operation, the memory ruv and database ruv should be the same.
            3. For modrdn operation, the memory ruv and database ruv should be the same.
            4. For delete operation, the memory ruv and database ruv should be the same.
    """

    log.info('Adding user: {} to supplier1'.format(TEST_ENTRY_NAME))
    users = UserAccounts(topo.ms['supplier1'], DEFAULT_SUFFIX)
    tuser = users.create(properties=USER_PROPERTIES)
    _compare_memoryruv_and_databaseruv(topo, 'add')

    log.info('Modify user: {} description'.format(TEST_ENTRY_NAME))
    tuser.replace('description', 'newdesc')
    _compare_memoryruv_and_databaseruv(topo, 'modify')

    log.info('Modify RDN of user: {}'.format(tuser.dn))
    try:
        topo.ms['supplier1'].modrdn_s(tuser.dn, 'uid={}'.format(NEW_RDN_NAME), 0)
    except ldap.LDAPError as e:
        log.fatal('Failed to modrdn entry: {}'.format(tuser.dn))
        raise e
    _compare_memoryruv_and_databaseruv(topo, 'modrdn')

    tuser = users.get(NEW_RDN_NAME)
    log.info('Delete user: {}'.format(tuser.dn))
    tuser.delete()
    _compare_memoryruv_and_databaseruv(topo, 'delete')


def test_ruv_after_reindex(topo):
    """Test that the tombstone RUV entry is not corrupted after a reindex task

    :id: 988c0fab-1905-4dc5-a45d-fbf195843a33
    :setup: 2 suppliers
    :steps:
        1. Reindex database
        2. Perform some updates
        3. Check error log does not have "_entryrdn_insert_key" errors
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    inst = topo.ms['supplier1']
    suffix = Domain(inst, "ou=people," + DEFAULT_SUFFIX)
    backends = Backends(inst)
    backend = backends.get(DEFAULT_BENAME)

    # Reindex nsuniqueid
    backend.reindex(attrs=['nsuniqueid'], wait=True)

    # Do some updates
    for idx in range(0, 5):
        suffix.replace('description', str(idx))

    # Check error log for RUV entryrdn errors.  Stopping instance forces RUV
    # to be written and quickly exposes the error
    inst.stop()
    assert not inst.searchErrorsLog("entryrdn_insert_key")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main('-s {}'.format(CURRENT_FILE))
