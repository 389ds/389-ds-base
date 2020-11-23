import logging
import pytest
import os
from datetime import datetime
from lib389._constants import DEFAULT_SUFFIX, INSTALL_LATEST_CONFIG
from lib389.properties import BACKEND_SAMPLE_ENTRIES, TASK_WAIT
from lib389.topologies import topology_st as topo
from lib389.backend import Backend
from lib389.tasks import BackupTask, RestoreTask
from lib389.config import BDB_LDBMConfig
from lib389 import DSEldif
from lib389.utils import ds_is_older
import tempfile

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

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


@pytest.mark.bz1851967
@pytest.mark.ds4112
@pytest.mark.skipif(ds_is_older('1.4.1'), reason="Not implemented")
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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
