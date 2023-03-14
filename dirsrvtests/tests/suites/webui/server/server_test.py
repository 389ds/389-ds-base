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


def test_server_settings_availability(topology_st, page, browser_name):
    """ Test visibility of Server Settings in server tab

    :id: e87a3c6f-3fda-49fa-91c4-a8ca418f32c2
    :setup: Standalone instance
    :steps:
         1. Check if General Settings tab is visible.
    :expectedresults:
         1. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Check if server settings tabs are loaded.')
    frame.get_by_role('tab', name='General Settings', exact=True).wait_for()
    assert frame.get_by_role('tab', name='General Settings').is_visible()


def test_server_settings_tabs_availability(topology_st, page, browser_name):
    """ Test visibility of individual tabs under Server Settings

    :id: 08cd0f84-e233-4a94-8230-a0cc54636595
    :setup: Standalone instance
    :steps:
         1. Check if Server Hostname is visible
         2. Click on Directory manager tab and check if Directory Manager DN is visible.
         3. Click on Disk Monitoring tab, click on checkbox and check if Disk Monitoring Threshold label is visible.
         4. Click on Advanced Settings tab and check if Anonymous Resource Limits DN text input is visible.
    :expectedresults:
         1. Element is visible.
         2. Element is visible.
         3. Element is visible.
         4. Element is visible.
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Check if General Settings tab is loaded.')
    frame.locator('#nsslapd-localhost').wait_for()
    assert frame.locator('#nsslapd-localhost').is_visible()

    log.info('Click on Directory Manager tab and check if element is loaded.')
    frame.get_by_role('tab', name='Directory Manager').click()
    assert frame.locator('#nsslapd-rootdn').is_visible()

    log.info('Click on Disk Monitoring tab and check if element is loaded.')
    frame.get_by_role('tab', name='Disk Monitoring').click()
    frame.locator('#nsslapd-disk-monitoring').click()
    assert frame.get_by_text('Disk Monitoring Threshold').is_visible()

    log.info('Click on Advanced Settings tab and check if element is loaded.')
    frame.get_by_role('tab', name='Advanced Settings').click()
    assert frame.locator('#nsslapd-anonlimitsdn').is_visible()


def test_tuning_and_limits_availability(topology_st, page, browser_name):
    """ Test visibility of Tuning & Limits settings

    :id: c09af833-0359-46ad-a701-52b67f315f70
    :setup: Standalone instance
    :steps:
         1. Click on Tuning & Limits button on the side panel and check if Number Of Worker Threads is visible.
         2. Click on Show Advanced Settings button.
         3. Check if Outbound IO Timeout label is visible.
    :expectedresults:
         1. Element is visible
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Tuning & Limits button and check if element is loaded.')
    frame.locator('#tuning-config').click()
    frame.get_by_text("Number Of Worker Threads").wait_for()
    assert frame.get_by_text("Number Of Worker Threads").is_visible()

    log.info('Open expandable section and check if element is loaded.')
    frame.get_by_role('button', name='Show Advanced Settings').click()
    frame.get_by_text('Outbound IO Timeout').wait_for()
    assert frame.get_by_text('Outbound IO Timeout').is_visible()


def test_security_availability(topology_st, page, browser_name):
    """ Test Security Settings tabs visibility

    :id: 6cd72564-798c-4524-89d3-aa2691535905
    :setup: Standalone instance
    :steps:
         1. Click on Security button on the side panel and check if Security Configuration tab is visible.
         2. Click on Certificate Management tab and check if Add CA Certificate button is visible.
         3. Click on Cipher Preferences and check if Enabled Ciphers heading is visible.
    :expectedresults:
         1. Element is visible
         2. Element is visible
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Security button and check if element is loaded.')
    frame.locator('#security-config').click()
    frame.get_by_role('tab', name='Security Configuration').wait_for()
    assert frame.get_by_role('tab', name='Security Configuration').is_visible()

    log.info('Click on Certificate Management tab and check if element is loaded.')
    frame.get_by_role('tab', name='Certificate Management').click()
    assert frame.get_by_role('button', name='Add CA Certificate').is_visible()

    log.info('Click on Cipher Preferences tab and check if element is loaded.')
    frame.get_by_role('tab', name='Cipher Preferences').click()
    assert frame.get_by_role('heading', name='Enabled Ciphers').is_visible()


def test_sasl_settings_and_mappings_availability(topology_st, page, browser_name):
    """ Test SASL Settings & Mappings visibility

    :id: 88954828-7533-4ac9-bfc0-e9c68f95278f
    :setup: Standalone instance
    :steps:
         1. Click on SASL Settings & Mappings button on the side panel.
         2. Check if Max SASL Buffer size text input field is visible.
    :expectedresults:
         1. Success
         2. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on SASL Settings & Mappings and check if element is loaded.')
    frame.locator('#sasl-config').click()
    frame.locator('#maxBufSize').wait_for()
    assert frame.locator('#maxBufSize').is_visible()


def test_ldapi_and_autobind_availability(topology_st, page, browser_name):
    """ Test LDAPI & AutoBind settings visibility

    :id: 505f1e3b-5d84-4734-8c64-fbb8b2805d6b
    :setup: Standalone instance
    :steps:
         1. Click on LDAPI & Autobind button on the side panel.
         2. Check if LDAPI Socket File Path is visible.
    :expectedresults:
         1. Success
         2. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on LDAPI & Autobind and check if element is loaded.')
    frame.locator('#ldapi-config').click()
    frame.locator('#nsslapd-ldapifilepath').wait_for()
    assert frame.locator('#nsslapd-ldapifilepath').is_visible()


def test_access_log_availability(topology_st, page, browser_name):
    """ Test Access Log tabs visibility

    :id: 48f8e778-b28b-45e1-8946-29456a53cf58
    :setup: Standalone instance
    :steps:
         1. Click on Access Log button on the side panel and check if Access Log Location input field is visible.
         2. Click on Rotation Policy tab and check if Maximum Number Of Logs label is visible.
         3. Click on Deletion Policy and check if Log Archive Exceeds label is visible.
    :expectedresults:
         1. Element is visible
         2. Element is visible
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Access Log button and check if element is loaded.')
    frame.locator('#access-log-config').click()
    frame.locator('#nsslapd-accesslog').wait_for()
    assert frame.locator('#nsslapd-accesslog').is_visible()

    log.info('Click on Rotation Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Rotation Policy').click()
    assert frame.get_by_text('Maximum Number Of Logs').is_visible()

    log.info('Click on Deletion Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Deletion Policy').click()
    assert frame.get_by_text('Log Archive Exceeds (in MB)').is_visible()


def test_audit_log_availability(topology_st, page, browser_name):
    """ Test Audit Log tabs visibility

    :id: a1539010-22b8-4e6b-b377-666a10c20573
    :setup: Standalone instance
    :steps:
         1. Click on Audit Log button on the side panel and check if Audit Log Location input field is visible.
         2. Click on Rotation Policy tab and check if Maximum Number Of Logs label is visible.
         3. Click on Deletion Policy and check if Log Archive Exceeds label is visible.
    :expectedresults:
         1. Element is visible
         2. Element is visible
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Audit Log button and check if element is loaded.')
    frame.locator('#audit-log-config').click()
    frame.locator('#nsslapd-auditlog').wait_for()
    assert frame.locator('#nsslapd-auditlog').is_visible()

    log.info('Click on Rotation Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Rotation Policy').click()
    assert frame.get_by_text('Maximum Number Of Logs').is_visible()

    log.info('Click on Deletion Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Deletion Policy').click()
    assert frame.get_by_text('Log Archive Exceeds (in MB)').is_visible()


def test_audit_failure_log_availability(topology_st, page, browser_name):
    """ Test Audit Failure Log tabs visibility

    :id: 0adcd31f-98a0-4b70-9efa-e810bc971f77
    :setup: Standalone instance
    :steps:
         1. Click on Audit Failure Log button on the side panel and check if Audit Log Location input field is visible.
         2. Click on Rotation Policy tab and check if Maximum Number Of Logs label is visible.
         3. Click on Deletion Policy and check if Log Archive Exceeds label is visible.
    :expectedresults:
         1. Element is visible
         2. Element is visible
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Audit Failure Log button and check if element is loaded.')
    frame.locator('#auditfail-log-config').click()
    frame.locator('#nsslapd-auditfaillog').wait_for()
    assert frame.locator('#nsslapd-auditfaillog').is_visible()

    log.info('Click on Rotation Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Rotation Policy').click()
    assert frame.get_by_text('Maximum Number Of Logs').is_visible()

    log.info('Click on Deletion Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Deletion Policy').click()
    assert frame.get_by_text('Log Archive Exceeds (in MB)').is_visible()


def test_errors_log_availability(topology_st, page, browser_name):
    """ Test Errors Log tabs visibility

    :id: 52cac1fd-a0cd-4c6e-8963-16d764955b86
    :setup: Standalone instance
    :steps:
         1. Click on Errors Log button in the side panel and check if Errors Log Location input field is visible.
         2. Click on Rotation Policy tab and check if Maximum Number Of Logs label is visible.
         3. Click on Deletion Policy and check if Log Archive Exceeds label is visible.
    :expectedresults:
         1. Element is visible
         2. Element is visible
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Errors Log button and check if element is loaded.')
    frame.locator('#error-log-config').click()
    frame.locator('#nsslapd-errorlog').wait_for()
    assert frame.locator('#nsslapd-errorlog').is_visible()

    log.info('Click on Rotation Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Rotation Policy').click()
    assert frame.get_by_text('Maximum Number Of Logs').is_visible()

    log.info('Click on Deletion Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Deletion Policy').click()
    assert frame.get_by_text('Log Archive Exceeds (in MB)').is_visible()


def test_security_log_availability(topology_st, page, browser_name):
    """ Test Security Log tabs visibility

    :id: 1b851fa2-38c9-4865-9e24-f762ef80825f
    :setup: Standalone instance
    :steps:
         1. Click on Security Log button in the side panel and check if Security Log Location input field is visible.
         2. Click on Rotation Policy tab and check if Maximum Number Of Logs label is visible.
         3. Click on Deletion Policy and check if Log Archive Exceeds label is visible.
    :expectedresults:
         1. Element is visible
         2. Element is visible
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Security Log button and check if element is loaded.')
    frame.locator('#security-log-config').click()
    frame.locator('#nsslapd-securitylog').wait_for()
    assert frame.locator('#nsslapd-securitylog').is_visible()

    log.info('Click on Rotation Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Rotation Policy').click()
    assert frame.get_by_text('Maximum Number Of Logs').is_visible()

    log.info('Click on Deletion Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Deletion Policy').click()
    assert frame.get_by_text('Log Archive Exceeds (in MB)').is_visible()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
