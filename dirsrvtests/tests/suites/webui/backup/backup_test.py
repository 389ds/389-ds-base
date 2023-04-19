# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest

from lib389.cli_idm.account import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from .. import setup_page, check_frame_assignment, setup_login

pytestmark = pytest.mark.skipif(os.getenv('WEBUI') is None, reason="These tests are only for WebUI environment")
pytest.importorskip('playwright')

SERVER_ID = 'standalone1'


@pytest.mark.xfail(reason="Will fail because of bz2189181")
def test_no_backup_dir(topology_st, page, browser_name):
    """ Test that instance is able to load when backup directory doesn't exist.

        :id: a1fb9e70-c110-4578-ba1f-4b593cc0a047
        :setup: Standalone instance
        :steps:
             1. Set Backup Directory (nsslapd-bakdir) to non existing directory.
             2. Check if element on Server tab is loaded.
        :expectedresults:
             1. Success
             2. Element is visible.
        """

    topology_st.standalone.config.set('nsslapd-bakdir', '/DOES_NOT_EXIST')

    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Check if server settings tabs are loaded.')
    frame.get_by_role('tab', name='General Settings', exact=True).wait_for()
    assert frame.get_by_role('tab', name='General Settings').is_visible()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
