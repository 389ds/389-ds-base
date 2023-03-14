# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
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


def test_schema_tab_visibility(topology_st, page, browser_name):
    """ Test Schema tab visibility

    :id: 4cbca624-b7be-49db-93f6-f9a9df79a9b2
    :setup: Standalone instance
    :steps:
         1. Click on Schema tab and check if Add Object Class button is visible.
         2. Click on Attributes tab and check if Add Attribute button is visible.
         3. Click on Matching Rules tab and check if Matching Rule columnheader is visible.
    :expectedresults:
         1. Element is visible
         2. Element is visible
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Schema tab and check if element is loaded.')
    frame.get_by_role('tab', name='Schema', exact=True).click()
    frame.get_by_role('button', name='Add ObjectClass').wait_for()
    assert frame.get_by_role('button', name='Add ObjectClass').is_visible()

    log.info('Click on Attributes tab and check if element is loaded.')
    frame.get_by_role('tab', name='Attributes').click()
    frame.get_by_role('button', name='Add Attribute').wait_for()
    assert frame.get_by_role('button', name='Add Attribute').is_visible()

    log.info('Click on Matching Rules tab and check if element is loaded.')
    frame.get_by_role('tab', name='Matching Rules').click()
    frame.get_by_role('columnheader', name='Matching Rule').wait_for()
    assert frame.get_by_role('columnheader', name='Matching Rule').is_visible()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
