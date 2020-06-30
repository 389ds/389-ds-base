import logging
import pytest
import os
from datetime import datetime
from lib389._constants import DEFAULT_SUFFIX, INSTALL_LATEST_CONFIG
from lib389.properties import BACKEND_SAMPLE_ENTRIES, TASK_WAIT
from lib389.topologies import topology_st as topo
from lib389.backend import Backend
from lib389.tasks import BackupTask, RestoreTask

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
    

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

