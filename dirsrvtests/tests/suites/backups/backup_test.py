# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import shutil
import time
import glob
from datetime import datetime
from lib389._constants import DEFAULT_SUFFIX, INSTALL_LATEST_CONFIG
from lib389.properties import BACKEND_SAMPLE_ENTRIES, TASK_WAIT
from lib389.topologies import topology_st as topo, topology_m2 as topo_m2
from lib389.backend import Backends, Backend
from lib389.dbgen import dbgen_users
from lib389.tasks import BackupTask, RestoreTask
from lib389.config import BDB_LDBMConfig
from lib389 import DSEldif
from lib389.utils import ds_is_older, get_default_db_lib
from lib389.replica import ReplicationManager
import tempfile

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)



BESTRUCT = [
    { "bename" : "be1", "suffix": "dc=be1", "nbusers": 1000 },
    { "bename" : "be2", "suffix": "dc=be2", "nbusers": 1000 },
    { "bename" : "be3", "suffix": "dc=be3", "nbusers": 1000 },
]


@pytest.fixture(scope="function")
def mytopo(topo, request):
    bes = []

    def fin():
        for be in bes:
            be.delete()
        for dir in glob.glob(f'{inst.ds_paths.backup_dir}/*'):
            shutil.rmtree(dir)

    if not DEBUGGING:
        request.addfinalizer(fin)

    inst = topo.standalone

    ldif_files = {}
    for d in BESTRUCT:
        bename = d['bename']
        suffix = d['suffix']
        nbusers = d['nbusers']
        log.info(f'Adding suffix: {suffix} and backend: {bename}...')
        backends = Backends(inst)
        try:
            be = backends.create(properties={'nsslapd-suffix': suffix, 'name': bename})
            # Insert at list head so that children backends get deleted before parent one.
            bes.insert(0, be)
        except ldap.UNWILLING_TO_PERFORM as e:
            if str(e) == "Mapping tree for this suffix exists!":
                pass
            else:
                raise e

        ldif_dir = inst.get_ldif_dir()
        ldif_files[bename] = os.path.join(ldif_dir, f'default_{bename}.ldif')
        dbgen_users(inst, nbusers, ldif_files[bename], suffix)
    inst.stop()
    for d in BESTRUCT:
        bename = d['bename']
        inst.ldif2db(bename, None, None, None, ldif_files[bename])
    inst.start()
    return topo


def test_missing_backend(topo):
    """Test that an error is returned when a restore is performed for a
    backend that is no longer present.

    :id: 889b8028-35cf-41d7-91f6-bc5193683646
    :setup: Standalone Instance
    :steps:
        1. Create a second backend
        2. Perform a back up
        3. Remove one of the backends from the config
        4. Perform a restore
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Failure
    """

    # Create a new backend
    BE_NAME = 'backupRoot'
    BE_SUFFIX = 'dc=back,dc=up'
    props = {
        'cn': BE_NAME,
        'nsslapd-suffix': BE_SUFFIX,
        BACKEND_SAMPLE_ENTRIES: INSTALL_LATEST_CONFIG
    }
    be = Backend(topo.standalone)
    backend_entry = be.create(properties=props)

    # perform backup
    backup_dir_name = "backup-%s" % datetime.now().strftime("%Y_%m_%d_%H_%M_%S")
    archive = os.path.join(topo.standalone.ds_paths.backup_dir, backup_dir_name)
    backup_task = BackupTask(topo.standalone)
    task_properties = {'nsArchiveDir': archive}
    backup_task.create(properties=task_properties)
    backup_task.wait()
    assert backup_task.get_exit_code() == 0

    # Remove new backend
    backend_entry.delete()

    # Restore the backup - it should fail
    restore_task = RestoreTask(topo.standalone)
    task_properties = {'nsArchiveDir': archive}
    restore_task.create(properties=task_properties)
    restore_task.wait()
    assert restore_task.get_exit_code() != 0


@pytest.mark.skipif(ds_is_older('1.4.1'), reason="Not implemented")
@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
def test_db_home_dir_online_backup(topo):
    """Test that if the dbhome directory is set causing an online backup to fail,
    the dblayer_backup function should go to error processing section.

    :id: cfc495d6-2a58-4e4e-aa40-39a15c71f973
    :setup: Standalone Instance
    :steps:
        1. Change the dbhome to directory to eg-/tmp/test
        2. Perform an online back-up
        3. Check for the correct errors in the log
    :expectedresults:
        1. Success
        2. Failure
        3. Success
    """
    bdb_ldbmconfig = BDB_LDBMConfig(topo.standalone)
    dseldif = DSEldif(topo.standalone)
    topo.standalone.stop()
    with tempfile.TemporaryDirectory() as backup_dir:
        dseldif.replace(bdb_ldbmconfig.dn, 'nsslapd-db-home-directory', f'{backup_dir}')
        topo.standalone.start()
        topo.standalone.tasks.db2bak(backup_dir=f'{backup_dir}', args={TASK_WAIT: True})
        assert topo.standalone.ds_error_log.match(f".*Failed renaming {backup_dir}.bak back to {backup_dir}")

def test_replication(topo_m2):
    """Test that if the dbhome directory is set causing an online backup to fail,
    the dblayer_backup function should go to error processing section.

    :id: 9c826d36-b17d-11ee-855f-482ae39447e5
    :setup: Two suppliers
    :steps:
        1. Perform backup on S1
        2. Perform changes on both suppliers
        3. Wait until replication is in sync
        4. Stop S1
        5. Destroy S1 database
        6. Start S1
        7. Restore S1 from backup
        8. Wait until replication is in sync
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """
    S1 = topo_m2.ms["supplier1"]
    S2 = topo_m2.ms["supplier2"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    with tempfile.TemporaryDirectory(dir=S1.ds_paths.backup_dir) as backup_dir:
        # Step 1: Perform backup on S1
        # Use the offline method to have a cleanly stopped state in changelog.
        S1.stop()
        assert S1.db2bak(backup_dir)
        S1.start()

        # Step 2:  Perform changes on both suppliers and wait for replication
        # Note: wait_for_replication perform changes
        repl.wait_for_replication(S1, S2)
        repl.wait_for_replication(S2, S1)
        # Step 4: Stop S1
        S1.stop()
        # Step 5: Destroy S1 database
        if get_default_db_lib() == "mdb":
            os.remove(f'{S1.ds_paths.db_dir}/data.mdb')
        else:
            shutil.rmtree(f'{S1.ds_paths.db_dir}/userRoot')
        # Step 6: Start S1
        S1.start()
        # Step 7: Restore S1 from backup
        rc = S1.tasks.bak2db(backup_dir=f'{backup_dir}', args={TASK_WAIT: True})
        assert rc == 0
        # Step 8: Wait until replication is in sync
        # Must replicate first from S2 to S1 to resync S1
        repl.wait_for_replication(S2, S1)
        # To help to diagnose test failure, you may want to look first at:
        # grep -E 'Database RUV|replica_reload_ruv|task_restore_thread|_cl5ConstructRUVs' /var/log/dirsrv/slapd-supplier1/errors
        repl.wait_for_replication(S1, S2)


def test_backup_task_after_failure(mytopo):
    """Test that new backup task is successful after a failure.
    backend that is no longer present.

    :id: a6c24898-2cd9-11ef-8c09-482ae39447e5
    :setup: Standalone Instance with multiple backends
    :steps:
        1. Cleanup
        2. Perform a back up
        3. Rename the backup directory while waiting for backup completion.
        4. Check that backup failed.
        5. Perform a back up
        6. Check that backup succeed.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Backup should fail
        5. Success
        6. Backup should succeed
    """

    inst = mytopo.standalone
    tasks = inst.tasks
    archive_dir1 = f'{inst.ds_paths.backup_dir}/bak1'
    archive_dir1b = f'{inst.ds_paths.backup_dir}/bak1b'
    archive_dir2 = f'{inst.ds_paths.backup_dir}/bak2'

    # Sometime the backup complete too fast, so lets retry if first
    # backup is successful
    for retry in range(50):
        # Step 1. Perform cleanup
        for dir in glob.glob(f'{inst.ds_paths.backup_dir}/*'):
            shutil.rmtree(dir)
        # Step 2. Perform a backup
        tasks.db2bak(backup_dir=archive_dir1)
        # Step 3. Wait until task is completed, trying to rename backup directory
        done,exitCode,warningCode = (False, None, None)
        while not done:
            if os.path.isdir(archive_dir1):
                os.rename(archive_dir1, archive_dir1b)
            done,exitCode,warningCode = tasks.checkTask(tasks.entry)
            time.sleep(0.01)
        if exitCode != 0:
            break
    # Step 4. Check that backup failed.
    assert exitCode != 0
    # Step 5. Perform a seconf backup after backup failure
    exitCode = tasks.db2bak(backup_dir=archive_dir2, args={TASK_WAIT: True})
    # Step 6. Check it is successful
    assert exitCode == 0


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
