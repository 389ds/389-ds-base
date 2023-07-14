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
from lib389.pwpolicy import PwPolicyManager
from lib389.topologies import topology_st
from .. import setup_page, check_frame_assignment, setup_login

pytestmark = pytest.mark.skipif(os.getenv('WEBUI') is None, reason="These tests are only for WebUI environment")
pytest.importorskip('playwright')

SERVER_ID = 'standalone1'


def test_database_tab_availability(topology_st, page, browser_name):
    """ Test Database tab visibility

    :id: 863863e0-4ba7-4309-8f56-e6719cdf2bbe
    :setup: Standalone instance
    :steps:
         1. Click on Database tab.
         2. Check if Limits tab under Global Database Configuration is visible.
    :expectedresults:
         1. Success
         2. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Check if database tab contents are loaded.')
    frame.get_by_role('tab', name='Database', exact=True).click()
    frame.get_by_role('tab', name='Limits').wait_for()
    assert frame.get_by_role('tab', name='Limits').is_visible()


def test_global_database_configuration_availability(topology_st, page, browser_name):
    """ Test Global Database Configuration tabs visibility

        :id: d0efda45-4e8e-4703-b9c0-ab53249dafc3
        :setup: Standalone instance
        :steps:
             1. Click on Database tab and check if ID List Scan Limit label is visible.
             2. Click on Database Cache tab and check if Automatic Cache Tuning checkbox is visible.
             3. Click on Import Cache tab and check if Automatic Import Cache Tuning checkbox is visible.
             4. Click on NDN Cache tab and check if Normalized DN Cache Max Size label is visible.
             5. Click on Database Locks tab and check if Enable DB Lock Monitoring checkbox is visible.
             6. Click on Advanced Settings and check if Transaction Logs Directory input field is visible.
        :expectedresults:
             1. Element is visible
             2. Element is visible
             3. Element is visible
             4. Element is visible
             5. Element is visible
             6. Element is visible
        """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Check if element on Limits tab is loaded.')
    frame.get_by_role('tab', name='Database', exact=True).click()
    frame.get_by_text('ID List Scan Limit', exact=True).wait_for()
    assert frame.get_by_text('ID List Scan Limit', exact=True).is_visible()

    log.info('Click on Database Cache tab and check if element is loaded')
    frame.get_by_role('tab', name='Database Cache', exact=True).click()
    assert frame.locator('#db_cache_auto').is_visible()

    log.info('Click on Import Cache tab and check if element is loaded')
    frame.get_by_role('tab', name='Import Cache', exact=True).click()
    assert frame.locator('#import_cache_auto').is_visible()

    log.info('Click on NDN Cache tab and check if element is loaded')
    frame.get_by_role('tab', name='NDN Cache', exact=True).click()
    assert frame.get_by_text('Normalized DN Cache Max Size').is_visible()

    log.info('Click on Database Locks tab and check if element is loaded')
    frame.get_by_role('tab', name='Database Locks', exact=True).click()
    assert frame.locator('#dblocksMonitoring').is_visible()

    log.info('Click on Advanced Settings tab and check if element is loaded')
    frame.get_by_role('tab', name='Advanced Settings', exact=True).click()
    assert frame.locator('#txnlogdir').is_visible()


def test_chaining_configuration_availability(topology_st, page, browser_name):
    """ Test Chaining Configuration settings visibility

        :id: 1f936968-d2fc-4fee-beeb-caeeb5df8c3f
        :setup: Standalone instance
        :steps:
             1. Click on Database tab, click on Chaining Configuration button on the side panel.
             2. Check if Size Limit input field is visible.
             3. Click on Controls & Components tab and check if Forwarded LDAP Controls heading is visible.
        :expectedresults:
             1. Success
             2. Element is visible
             3. Element is visible
        """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Chaining Configuration and check if element is loaded.')
    frame.get_by_role('tab', name='Database', exact=True).click()
    frame.locator('#chaining-config').click()
    frame.locator('#defSizeLimit').wait_for()
    assert frame.locator('#defSizeLimit').is_visible()

    log.info('Click on Controls & Components tab and check if element is loaded')
    frame.get_by_role('tab', name='Controls & Components').click()
    assert frame.get_by_role('heading', name='Forwarded LDAP Controls').is_visible()


def test_backups_and_ldifs_availability(topology_st, page, browser_name):
    """ Test Backups & LDIFs settings visibility.

        :id: 90571e96-f3c9-4bec-83d6-04c61e8a0e78
        :setup: Standalone instance
        :steps:
             1. Click on Database tab, click on Backups & LDIFs button on the side panel.
             2. Check if Create Backup button is visible.
             3. Click on LDIFs tab and check if Create LDIF button is visible.
        :expectedresults:
             1. Success
             2. Element is visible
             3. Element is visible
        """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Backups & LDIFs button and check if element is loaded.')
    frame.get_by_role('tab', name='Database', exact=True).click()
    frame.locator('#backups').click()
    assert frame.get_by_role('button', name='Create Backup').is_visible()

    log.info('Click on LDIFs tab and check if element is loaded.')
    frame.get_by_role('tab', name='LDIFs').click()
    assert frame.get_by_role('button', name='Create LDIF').is_visible()


def test_global_policy_availability(topology_st, page, browser_name):
    """ Check if Global Policy settings is visible

        :id: 2bdd219d-c28d-411d-9758-18386f472ad2
        :setup: Standalone instance
        :steps:
             1. Click on Database tab, click on Global Policy button on the side panel.
             2. Check if Password Minimum Age input field is visible.
             3. Click on Expiration tab and click on Enforce Password Expiration checkbox.
             4. Check if Allowed Logins After Password Expires input field is visible.
             5. Click on Account Lockout tab and click on Enable Account Lockout checkbox.
             6. Check if Number of Failed Logins That Locks Out Account input field is visible.
             7. Click on Syntax Checking tab and click on Enable Password Syntax Checking checkbox.
             8. Check if Minimum Length input field is visible.
             9. Click on Temporary Password Rules tab and check if Password Max Use input field is visible.
        :expectedresults:
             1. Success
             2. Element is visible
             3. Success
             4. Element is visible
             5. Success
             6. Element is visible
             7. Success
             8. Element is visible
             9. Element is visible
        """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Global Policy button and check if element is loaded.')
    frame.get_by_role('tab', name='Database', exact=True).click()
    frame.locator('#pwpolicy').click()
    frame.locator('#passwordminage').wait_for()
    assert frame.locator('#passwordminage').is_visible()

    log.info('Click on Expiration tab and check if element is loaded.')
    frame.get_by_role('tab', name='Expiration').click()
    frame.get_by_text('Enforce Password Expiration').click()
    assert frame.locator('#passwordgracelimit').is_visible()

    log.info('Click on Account Lockout tab and check if element is loaded.')
    frame.get_by_role('tab', name='Account Lockout').click()
    frame.get_by_text('Enable Account Lockout').click()
    assert frame.locator('#passwordmaxfailure').is_visible()

    log.info('Click on Syntax Checking tab and check if element is loaded.')
    frame.get_by_role('tab', name='Syntax Checking').click()
    frame.get_by_text('Enable Password Syntax Checking').click()
    assert frame.locator('#passwordminlength').is_visible()

    log.info('Click on Temporary Password Rules tab and check if element is loaded.')
    frame.get_by_role('tab', name='Temporary Password Rules').click()
    assert frame.locator('#passwordtprmaxuse').is_visible()


def test_local_policy_availability(topology_st, page, browser_name):
    """ Test Local Policies settings visibility

        :id: f540e0fa-a4c6-4c88-b97a-d21ada68f627
        :setup: Standalone instance
        :steps:
             1. Click on Database tab, click on Local Policies button on side panel.
             2. Check if Local Password Policies columnheader is visible.
             3. Click on Edit Policy tab and check if Please choose a policy from the Local Policy Table heading is visible.
             4. Click on Create A Policy tab and check if Target DN input field is visible.
        :expectedresults:
             1. Success
             2. Element is visible
             3. Element is visible
             4. Element is visible
        """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Local Policies button and check if element is loaded.')
    frame.get_by_role('tab', name='Database', exact=True).click()
    frame.locator('#localpwpolicy').click()
    frame.get_by_role('columnheader', name='Local Password Policies').wait_for()
    assert frame.get_by_role('columnheader', name='Local Password Policies').is_visible()

    log.info('Click on Edit Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Edit Policy').click()
    assert frame.get_by_role('heading', name='Please choose a policy from the Local Policy Table.').is_visible()

    log.info('Click on Create A Policy tab and check if element is loaded.')
    frame.get_by_role('tab', name='Create A Policy').click()
    assert frame.locator('#policyDN').is_visible()


def test_suffixes_policy_availability(topology_st, page, browser_name):
    """ Test Suffixes settings visibility

        :id: b8399229-3b98-46d7-af15-f5ff0bcc6be9
        :setup: Standalone instance
        :steps:
             1. Click on Database tab, click on dc=example,dc=com button.
             2. Check if Entry Cache Size input field is visible.
             3. Click on Referrals tab and check if Referrals columnheader is visible.
             4. Click on Indexes tab and check if Database Indexes-sub tab is visible.
             5. Click on VLV Indexes and check if VLV Indexes columnheader is visible.
             6. Click on Encrypted Attributes and check if Encrypted Attribute columnheader is visible.
        :expectedresults:
             1. Success
             2. Element is visible
             3. Element is visible
             4. Element is visible
             5. Element is visible
             6. Element is visible
        """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Suffixes and check if element is loaded.')
    frame.get_by_role('tab', name='Database', exact=True).click()
    frame.locator('#dc\=example\,dc\=com').click()
    frame.locator('#cachememsize').wait_for()
    assert frame.locator('#cachememsize').is_visible()

    log.info('Click on Referrals tab and check if element is loaded.')
    frame.get_by_role('tab', name='Referrals').click()
    frame.get_by_role('columnheader', name='Referrals').wait_for()
    assert frame.get_by_role('columnheader', name='Referrals').is_visible()

    log.info('Click on Indexes tab and check if element is loaded.')
    frame.get_by_role('tab', name='Indexes', exact=True).click()
    frame.get_by_role('tab', name='Database Indexes').wait_for()
    assert frame.get_by_role('tab', name='Database Indexes').is_visible()

    log.info('Click on VLV Indexes tab and check if element is loaded.')
    frame.get_by_role('tab', name='VLV Indexes').click()
    frame.get_by_role('columnheader', name='VLV Indexes').wait_for()
    assert frame.get_by_role('columnheader', name='VLV Indexes').is_visible()

    log.info('Click on Encrypted Attributes tab and check if element is loaded.')
    frame.get_by_role('tab', name='Encrypted Attributes').click()
    frame.get_by_role('columnheader', name='Encrypted Attribute').wait_for()
    assert frame.get_by_role('columnheader', name='Encrypted Attribute').is_visible()


def test_dictionary_check_checkbox(topology_st, page, browser_name):
    """ Test that Dictionary Check checkbox in WebUI is changed after cli command

        :id: e1dcac6d-df45-4a89-a1f2-b18c65dfecba
        :setup: Standalone instance
        :steps:
             1. Enable PasswordDictCheck through cli.
             2. Open Database tab, Global Password Policies and click on Syntax Checking tab.
             3. Check that Dictionary Check checkbox is checked.
             4. Disable PasswordDictCheck through cli.
             5. Reload Syntax Checking tab.
             6. Check that Dictionary Check checkbox is unchecked.
        :expectedresults:
             1. Success
             2. Success
             3. Dictionary Check checkbox is checked
             4. Success
             5. Success
             6. Dictionary Check checkbox is unchecked
        """
    log.info('Enable password syntax checking and enable dictionary check.')
    ppm = PwPolicyManager(topology_st.standalone)
    ppm.set_global_policy({"passworddictcheck": "on"})

    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Database tab, click on Global Policy, '
             'click on Syntax Checking and check that Dictionary Check checkbox is checked.')
    frame.get_by_role('tab', name='Database', exact=True).click()
    frame.locator('#pwpolicy').click()
    frame.get_by_role('tab', name='Syntax Checking').click()
    frame.get_by_text('Enable Password Syntax Checking').click()
    assert frame.get_by_text('Dictionary Check').is_checked()

    log.info('Disable dictionary check, reload tab and check that Dictionary Check checkbox is unchecked.')
    ppm.set_global_policy({"passworddictcheck": "off"})
    ppm.set_global_policy({"passwordchecksyntax": "on"})
    frame.get_by_role('img', name="Refresh global password policy settings").click()
    assert not frame.get_by_text('Dictionary Check').is_checked()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
