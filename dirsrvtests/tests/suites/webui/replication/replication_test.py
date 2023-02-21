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


def test_replication_availability(topology_st, page, browser_name):
    """ Test replication tab of Red Hat Directory Server when instance is created

    :id: f3451124-9764-4da1-8efb-4e3d2749e465
    :setup: Standalone instance
    :steps:
         1. Go to Red Hat Directory server side tab page
         2. Click on replication tab
         3. Check there is Enable Replication button
    :expectedresults:
         1. Success
         2. Success
         3. Button is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Replication tab and check if Enable Replication button is visible')
    frame.get_by_role('tab', name='Replication').click()
    frame.get_by_role('button', name='Enable Replication').wait_for()
    assert frame.get_by_role('button', name='Enable Replication').is_visible()


def test_enable_replication(topology_st, page, browser_name):
    """ Test functionality of Enable Replication button

        :id: 87d8f3c0-1dae-4240-826c-f633abb85cda
        :setup: Standalone instance
        :steps:
             1. Go to Red Hat Directory server side tab page
             2. Click on replication tab
             3. Click on Enable Replication button
             4. Fill password and confirm password
             5. Click on cancel
             6. Click on Enable Replication button and fill passwords again.
             7. Click on Enable Replication button and wait until Add Replication Manager is visible.
        :expectedresults:
             1. Success
             2. Success
             3. Success
             4. Success
             5. Success
             6. Success
             7. Success
        """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Replication tab and click on Enable Replication button ')
    frame.get_by_role('tab', name='Replication').click()
    frame.get_by_role('button', name='Enable Replication').wait_for()
    frame.get_by_role('button', name='Enable Replication').click()

    log.info('Fill password, fill confirm password and click on cancel.')
    frame.fill('#enableBindPW', 'redhat')
    frame.fill('#enableBindPWConfirm', 'redhat')
    frame.get_by_role('button', name='Cancel').click()

    assert frame.get_by_role('button', name='Enable Replication').is_visible()

    log.info('Fill password, fill confirm password, click on enable replication'
             ' and check Add Replication Manager button is visible.')
    frame.get_by_role('button', name='Enable Replication').click()
    frame.fill('#enableBindPW', 'redhat')
    frame.fill('#enableBindPWConfirm', 'redhat')
    frame.get_by_role("dialog", name="Enable Replication").get_by_role("button", name="Enable Replication").click()
    frame.get_by_role('button', name='Add Replication Manager').wait_for()

    assert frame.get_by_role('button', name='Add Replication Manager').is_visible()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
