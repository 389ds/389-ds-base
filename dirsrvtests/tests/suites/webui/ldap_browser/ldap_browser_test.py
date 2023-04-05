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


def test_ldap_browser_tab_visibility(topology_st, page, browser_name):
    """ Test LDAP Browser tab visibility

    :id: cb5f04dc-99ff-4ef6-928c-5f41272c51af
    :setup: Standalone instance
    :steps:
         1. Click on LDAP Browser tab.
         2. Check if Tree View tab is visible.
         3. Click on dc=example,dc=com button.
         4. Check if Attribute columnheader is visible.
         5. Click on Table View tab.
         6. Check if Database Suffixes columnheader is visible.
         7. Click on Search tab and click on Show Search Criteria button.
         8. Check if Search Base text input field is visible.
    :expectedresults:
         1. Success
         2. Element is visible
         3. Success
         4. Element is visible
         5. Success
         6. Element is visible
         7. Success
         8. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on LDAP Browser tab and check if element is loaded.')
    frame.get_by_role('tab', name='LDAP Browser', exact=True).click()
    frame.get_by_role('tab', name='Tree View').wait_for()
    assert frame.get_by_role('tab', name='Tree View').is_visible()

    log.info('Click on dc=example,dc=com button and check if element is loaded.')
    frame.get_by_role('button').filter(has_text='dc=example,dc=com').click()
    frame.get_by_role('columnheader', name='Attribute').wait_for()
    assert frame.get_by_role('columnheader', name='Attribute').is_visible()

    log.info('Click on Table View tab and check if element is loaded')
    frame.get_by_role('tab', name='Table View').click()
    assert frame.get_by_role('columnheader', name='Database Suffixes').is_visible()

    log.info('Click on Search tab and check if element is loaded')
    frame.get_by_role('tab', name='Search').click()
    frame.get_by_text('Show Search Criteria').click()
    assert frame.locator('#searchBase').is_visible()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
