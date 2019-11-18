# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import subprocess
from lib389.utils import *
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

SYS_TEST_USER = 'dirsrv_testuser'


@pytest.fixture(scope="module")
def sys_test_user(request):
    """Creates and deletes a system test user"""

    cmd = ['/usr/sbin/useradd', SYS_TEST_USER]

    log.info('Add system test user - {}'.format(SYS_TEST_USER))
    try:
        subprocess.call(cmd)
    except subprocess.CalledProcessError as e:
        log.exception('Failed to add user {} error {}'.format(SYS_TEST_USER, e.output))

    def fin():
        cmd = ['/usr/sbin/userdel', SYS_TEST_USER]

        log.info('Delete system test user - {}'.format(SYS_TEST_USER))
        try:
            subprocess.call(cmd)
        except subprocess.CalledProcessError as e:
            log.exception('Failed to delete user {} error {}'.format(SYS_TEST_USER, e.output))

    request.addfinalizer(fin)


def test_user_permissions(topo, sys_test_user):
    """Check permissions for usual user operations in log dir

    :id: 4e423cd5-300c-4df0-ab40-aec7e51c3be8
    :feature: ds-logpipe
    :setup: Standalone instance
    :steps: 1. Add a new user to the system
            2. Try to create a logpipe in the log directory with '-u' option specifying the user
            3. Delete the user
    :expectedresults: Permission denied error happens
    """

    ds_logpipe_path = os.path.join(topo.standalone.ds_paths.bin_dir, 'ds-logpipe.py')
    fakelogpipe_path = os.path.join(topo.standalone.ds_paths.log_dir, 'fakelog.pipe')

    # I think we need to add a function for this to lib389, when we will port the full test suite
    cmd = [ds_logpipe_path, fakelogpipe_path, '-u', SYS_TEST_USER]

    log.info('Try to create a logpipe in the log directory with "-u" option specifying the user')
    with pytest.raises(subprocess.CalledProcessError) as cp:
        result = subprocess.check_output(cmd)
        assert 'Permission denied' in result


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
