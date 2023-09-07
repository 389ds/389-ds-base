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
from lib389.cli_base import LogCapture
from lib389.dbgen import dbgen_users
from lib389.replica import Replicas, ReplicationManager
from lib389.backend import Backends
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccounts
from lib389.tasks import ImportTask
from lib389.topologies import create_topology
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


@pytest.fixture(scope="function")
def topo(request):
    """Create Replication Deployment with two suppliers"""

    topology = create_topology({ReplicaRole.SUPPLIER: 2}, request=request)

    topology.logcap = LogCapture()
    return topology


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


@pytest.mark.ds1317
@pytest.mark.xfail(reason='https://github.com/389ds/389-ds-base/issues/1317')
def test_ruv_after_import(topo):
    """Test the RUV behavior after an LDIF import operation.

    :id: 6843ab56-0291-425c-954b-3002b8352025
    :setup: 2 suppliers
    :steps:
        1. Export LDIF from supplier 1.
        2. Create 1000 test users in supplier 1.
        3. Wait for replication to complete from supplier 1 to supplier 2.
        4. Pause all replicas.
        5. Import LDIF back to supplier 1.
        6. Resume all replicas.
        7. Perform attribute updates.
    :expectedresults:
        1. LDIF export should complete successfully.
        2. Test users should be created successfully.
        3. Replication should complete successfully.
        4. All replicas should be paused.
        5. LDIF import should complete successfully.
        6. All replicas should be resumed.
        7. Attribute updates should complete successfully.
    """

    log.info('Getting supplier instances')
    s1 = topo.ms['supplier1']
    s2 = topo.ms['supplier2']

    log.info('Performing LDIF export on supplier 1')
    ldif_dir = s1.get_ldif_dir()
    export_ldif = ldif_dir + '/export.ldif'
    export_task = Backends(s1).export_ldif(be_names=DEFAULT_BENAME, ldif=export_ldif, replication=True)
    export_task.wait()

    log.info('Creating 1000 test users on supplier 1')
    users = UserAccounts(s1, DEFAULT_SUFFIX)
    for idx in range(0, 1000):
        users.create_test_user(uid=idx)

    log.info('Waiting for replication to complete')
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(s2, s1)

    log.info('Performing LDIF import on supplier 1')
    r = Backends(s1).get(DEFAULT_BENAME).import_ldif([export_ldif])
    s2.stop()
    r.wait()

    s2.start()

    log.info('Performing attribute updates')
    suffix = Domain(s1, "ou=people," + DEFAULT_SUFFIX)
    for idx in range(0, 5):
        suffix.replace('description', str(idx))

    repl.wait_for_replication(s1, s2)
    repl.wait_for_replication(s2, s1)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main('-s {}'.format(CURRENT_FILE))
