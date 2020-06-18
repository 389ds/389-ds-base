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

from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.plugins import POSIXWinsyncPlugin
from lib389.cli_conf.plugins.posix_winsync import do_fixup

pytestmark = pytest.mark.tier0

LOG_FILE = '/tmp/fixup.log'
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def set_log_file_and_ldif(topology_st, request):
    MYLDIF = 'example1k_posix.ldif'
    global ldif_file

    fh = logging.FileHandler(LOG_FILE)
    fh.setLevel(logging.DEBUG)
    log.addHandler(fh)

    data_dir_path = topology_st.standalone.getDir(__file__, DATA_DIR)
    ldif_file = f"{data_dir_path}ticket48212/{MYLDIF}"
    ldif_dir = topology_st.standalone.get_ldif_dir()
    shutil.copy(ldif_file, ldif_dir)
    ldif_file = ldif_dir + '/' + MYLDIF

    def fin():
        log.info('Delete files')
        os.remove(LOG_FILE)
        os.remove(ldif_file)

    request.addfinalizer(fin)


@pytest.mark.ds50545
@pytest.mark.bz1739718
@pytest.mark.skipif(ds_is_older("1.4.1"), reason="Not implemented")
def test_posix_winsync_fixup(topology_st, set_log_file_and_ldif):
    """Test posix-winsync fixup that was ported from legacy tools

    :id: ce691017-cbd2-49ed-ac2d-8c3ea78050f6
    :setup: Standalone instance
    :steps:
         1. Create DS instance
         2. Enable PosixWinsync plugin
         3. Run fixup task
         4. Check log for output
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone
    output_list = ['Attempting to add task entry', 'Successfully added task entry']

    log.info('Enable POSIXWinsyncPlugin')
    posix = POSIXWinsyncPlugin(standalone)
    posix.enable()

    log.info('Stopping the server and importing posix accounts')
    standalone.stop()
    assert standalone.ldif2db(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], encrypt=None, excludeSuffixes=None,
                              import_file=ldif_file)
    standalone.start()

    args = FakeArgs()
    args.DN = DEFAULT_SUFFIX
    args.filter = None

    log.info('Run Fixup task')
    do_fixup(standalone, DEFAULT_SUFFIX, log, args)

    log.info('Check log if fixup task was successful')
    with open(LOG_FILE, 'r') as f:
        file_content = f.read()
        for item in output_list:
            assert item in file_content


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
