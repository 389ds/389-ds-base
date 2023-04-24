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


def test_monitoring_tab_visibility(topology_st, page, browser_name):
    """ Test Monitoring tab visibility

    :id: e16be05a-4465-4a2b-bfe2-7c5aafb55c91
    :setup: Standalone instance
    :steps:
         1. Click on Monitoring tab.
         2. Check if Resource Charts tab is visible.
    :expectedresults:
         1. Success
         2. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Check if Monitoring tab is loaded.')
    frame.get_by_role('tab', name='Monitoring', exact=True).click()
    frame.get_by_role('tab', name='Resource Charts').wait_for()
    assert frame.get_by_role('tab', name='Resource Charts').is_visible()


def test_server_statistics_visibility(topology_st, page, browser_name):
    """ Test Server Statistics monitoring visibility

    :id: 90e964e8-99d7-45e5-ad20-520099db054e
    :setup: Standalone instance
    :steps:
         1. Click on Monitoring tab and check if Connections heading is visible.
         2. Click on Server Stats tab and check if Server Instance label is visible.
         3. Click on Connection Table tab and check if Client Connections heading is visible.
         4. Click on Disk Space tab and check if Refresh button is visible.
         5. Click on SNMP Counters and check if Bytes Sent label is visible.
    :expectedresults:
         1. Element is visible
         2. Element is visible
         3. Element is visible
         4. Element is visible
         5. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Monitoring tab and check if element in Server Statistics is loaded.')
    frame.get_by_role('tab', name='Monitoring', exact=True).click()
    frame.get_by_role('heading', name='Connections').wait_for()
    assert frame.get_by_role('heading', name='Connections').is_visible()

    log.info('Click on Server Stats tab and check if element is loaded.')
    frame.get_by_role('tab', name='Server Stats').click()
    assert frame.get_by_text('Server Instance').is_visible()

    log.info('Click on Connection Table tab and check if element is loaded.')
    frame.get_by_role('tab', name='Connection Table').click()
    assert frame.get_by_role('heading', name='Client Connections').is_visible()

    log.info('Click on Disk Space tab and check if element is loaded.')
    frame.get_by_role('tab', name='Disk Space').click()
    assert frame.get_by_role('button', name='Refresh').is_visible()

    log.info('Click on SNMP Counters tab and check if element is loaded.')
    frame.get_by_role('tab', name='SNMP Counters').click()
    assert frame.get_by_text('Bytes Sent', exact=True).is_visible()


def test_replication_visibility(topology_st, page, browser_name):
    """ Test Replication monitoring visibility

    :id: 65b271e5-a172-461b-ad36-605706d68780
    :setup: Standalone instance
    :steps:
         1. Click on Replication Tab, Click on Enable Replication.
         2. Fill Password and Confirm password.
         3. Click on Enable Replication button and wait until Add Replication Manager is visible.
         4. Click on Monitoring tab, click on Replication button on the side panel.
         5. Check if Generate Report button is visible.
         6. Click on Agreements tab and check if Replication Agreements columnheader is visible.
         7. Click on Winsync tab and check if Winsync Agreements columnheader is visible.
         8. Click on Tasks tab and check if CleanAllRUV Tasks columnheader is visible.
         9. Click on Conflict Entries tab and check if Replication Conflict Entries columnheader is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
         5. Element is visible
         6. Element is visible
         7. Element is visible
         8. Element is visible
         9. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Enable replication in order to proceed with replication visibility testing.')
    frame.get_by_role('tab', name='Replication').click()
    frame.get_by_role('button', name='Enable Replication').click()
    frame.fill('#enableBindPW', 'redhat')
    frame.fill('#enableBindPWConfirm', 'redhat')
    frame.get_by_role("dialog", name="Enable Replication").get_by_role("button", name="Enable Replication").click()
    frame.get_by_role('button', name='Add Replication Manager').wait_for()

    log.info('Click on Monitoring tab and then on Replication in the menu and check if element is loaded.')
    frame.get_by_role('tab', name='Monitoring', exact=True).click()
    frame.locator('#replication-monitor').click()
    frame.get_by_role('button', name='Synchronization Report').wait_for()
    frame.locator('#sync-report').click()
    frame.get_by_role('tab', name='Prepare New Report').click()
    frame.get_by_role('button', name='Generate Report').wait_for()
    assert frame.get_by_role('button', name='Generate Report').is_visible()

    log.info('Click on Agreements tab and check if element is loaded.')
    assert frame.locator('#replication-suffix-dc\\=example\\,dc\\=com').is_visible()


def test_database_visibility(topology_st, page, browser_name):
    """ Test Database monitoring visibility

    :id: bf3f3e42-e748-41b8-bda2-a1856343a995
    :setup: Standalone instance
    :steps:
         1. Click on Monitoring tab, click on dc=example,dc=com button on the side panel.
         2. Check if Entry Cache Hit Ratio label is visible.
         3. Click on DN Cache tab and check if DN Cache Hit Ratio label is visible.
    :expectedresults:
         1. Success
         2. Element is visible
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Monitoring tab, then click on database button and check if element is loaded.')
    frame.get_by_role('tab', name='Monitoring', exact=True).click()
    frame.locator('#dc\\=example\\,dc\\=com').click()
    frame.get_by_text('Entry Cache Hit Ratio').wait_for()
    assert frame.get_by_text('Entry Cache Hit Ratio').is_visible()

    log.info('Click on DN Cache tab and check if element is loaded.')
    frame.get_by_role('tab', name='DN Cache').click()
    assert frame.get_by_text('DN Cache Hit Ratio').is_visible()


def test_logging_visibility(topology_st, page, browser_name):
    """ Test Logging monitoring visibility

    :id: c3e91cd4-569e-45e2-adc7-cbffb4ee7b6c
    :setup: Standalone instance
    :steps:
         1. Click on Monitoring tab, click on Access Log button on side panel.
         2. Check if Access Log text field is visible.
         3. Click on Audit Log button on side panel.
         4. Check if Audit Log text field is visible.
         5. Click on Audit Failure Log button on side panel.
         6. Check if Audit Failure Log text field is visible.
         7. Click on Errors Log button on side panel.
         8. Check if Errors Log text field is visible.
         9. Click on Security Log button on side panel.
         10. Check if Security Log text field is visible.
    :expectedresults:
         1. Success
         2. Element is visible
         3. Success
         4. Element is visible
         5. Success
         6. Element is visible
         7. Success
         8. Element is visible
         9. Success
         10. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Monitoring tab, then click on Access Log button and check if element is loaded.')
    frame.get_by_role('tab', name='Monitoring', exact=True).click()
    frame.locator('#access-log-monitor').click()
    frame.locator('#accesslog-area').wait_for()
    assert frame.locator('#accesslog-area').is_visible()

    log.info('Click on Audit Log button and check if element is loaded.')
    frame.locator('#audit-log-monitor').click()
    frame.locator('#auditlog-area').wait_for()
    assert frame.locator('#auditlog-area').is_visible()

    log.info('Click on Audit Failure Log button and check if element is loaded.')
    frame.locator('#auditfail-log-monitor').click()
    frame.locator('#auditfaillog-area').wait_for()
    assert frame.locator('#auditfaillog-area').is_visible()

    log.info('Click on Errors Log button and check if element is loaded.')
    frame.locator('#error-log-monitor').click()
    frame.locator('#errorslog-area').wait_for()
    assert frame.locator('#errorslog-area').is_visible()

    log.info('Click on Security Log button and check if element is loaded.')
    frame.locator('#security-log-monitor').click()
    frame.locator('#securitylog-area').wait_for()
    assert frame.locator('#securitylog-area').is_visible()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
