# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest

from lib389.cli_ctl.dbtasks import dbtasks_verify
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs

pytestmark = pytest.mark.tier0

LOG_FILE = '/tmp/dbverify.log'
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def set_log_file(request):
    fh = logging.FileHandler(LOG_FILE)
    fh.setLevel(logging.DEBUG)
    log.addHandler(fh)

    def fin():
        log.info('Delete log file')
        log.removeHandler(fh)
        os.remove(LOG_FILE)

    request.addfinalizer(fin)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsctl_dbverify(topology_st, set_log_file):
    """Test dbverify tool, that was ported from legacy tools to dsctl

    :id: 1b22b363-a6e5-4922-ad42-ae80446d69fe
    :setup: Standalone instance
    :steps:
         1. Create DS instance
         2. Run dbverify
         3. Check if dbverify was successful
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    message = 'dbverify successful'

    args = FakeArgs()
    args.backend = DEFAULT_BENAME

    log.info('Run dbverify')
    standalone.stop()
    dbtasks_verify(standalone, log, args)

    log.info('Check dbverify was successful')
    with open(LOG_FILE, 'r+') as f:
        file_content = f.read()
        assert message in file_content


def run_dbverify(inst, bename, rc=0):
    # Run dsctl inst dbverify bename and check it returns rc
    cmd = [ 'dsctl', inst.serverid, 'dbverify', bename ]
    log.info(f'Run: {cmd}')
    result =  subprocess.run(cmd, encoding='utf-8', capture_output=True, text=True)
    log.info(f'Returned: {result.returncode} stdout: {result.stdout} stderr: {result.stderr}')
    assert result.returncode == rc
    return result



def test_dbverify_bad_bename(topology_st, set_log_file):
    """Test dbverify tool when backend does not exists

    :id: da79a4cc-956c-11f0-8220-c85309d5c3e3
    :setup: Standalone instance
    :steps:
         1. Create DS instance
         2. Run dbverify
         3. Check if dbverify provides the expected messages
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    if get_default_db_lib() == 'bdb':
        message = "dbverify failed"
        logmessage = "Backend 'userFoot' does not exist."
        rc = 1
    else:
        message = "dbverify successful"
        logmessage = "db_verify feature is meaningless"
        rc = 0

    standalone.stop()
    result = run_dbverify(standalone, 'userFoot', rc)
    assert message in result.stdout
    assert logmessage in result.stderr



if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
