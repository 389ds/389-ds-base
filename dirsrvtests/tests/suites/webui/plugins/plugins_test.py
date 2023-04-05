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


def test_plugins_tab_visibility(topology_st, page, browser_name):
    """ Test visibility of Plugins tab.

    :id: 5b80bd5d-9294-4521-af0e-cd37ce9264a6
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Check if search input is visible
    :expectedresults:
         1. Success
         2. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Check if Plugins tab is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_placeholder('Search Plugins').wait_for()
    assert frame.get_by_placeholder('Search Plugins').is_visible()


def test_account_policy_plugin_visibility(topology_st, page, browser_name):
    """ Test Account Policy Plugin visibility.

    :id: 6e8a27cb-32a2-46f1-918e-4c3f91c8f34e
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on Account Policy button on the side panel.
         3. Check if Shared Config Entry text input field is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on Account Policy plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Plugin is disabledAccount Policy', exact=True).wait_for()
    frame.get_by_text('Plugin is disabledAccount Policy', exact=True).click()
    frame.locator('#configArea').wait_for()
    assert frame.locator('#configArea').is_visible()


def test_attribute_uniqueness_plugin_visibility(topology_st, page, browser_name):
    """ Test Attribute Uniqueness plugin visibility.

    :id: f6e49e13-7820-40fa-b2ae-d5e48dd03d2c
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on Attribute Uniqueness button on the side panel.
         3. Check if Add Config button is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on Attribute Uniqueness plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Plugin is disabledAttribute Uniqueness', exact=True).wait_for()
    frame.get_by_text('Plugin is disabledAttribute Uniqueness', exact=True).click()
    frame.get_by_role('button', name='Add Config').wait_for()
    assert frame.get_by_role('button', name='Add Config').is_visible()


def test_auto_membership_plugin_visibility(topology_st, page, browser_name):
    """ Test Auto Membership plugin visibility

    :id: 5c05617a-8a23-46cb-83ce-3bdd30388e0b
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on Auto Membership button on the side panel.
         3. Check if Add Definition button is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on Auto Membership plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Plugin is enabledAuto Membership').wait_for()
    frame.get_by_text('Plugin is enabledAuto Membership').click()
    frame.get_by_role('button', name='Add Definition').wait_for()
    assert frame.get_by_role('button', name='Add Definition').is_visible()


def test_dna_plugin_visibility(topology_st, page, browser_name):
    """ Test DNA plugin visibility.

    :id: b246682b-c41d-4ae9-9c64-38bd8e665a71
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on DNA button on the side panel.
         3. Check if Add Config button is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on DNA plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Plugin is disabledDNA').wait_for()
    frame.get_by_text('Plugin is disabledDNA').click()
    frame.get_by_role('button', name='Add Config').wait_for()
    assert frame.get_by_role('button', name='Add Config').is_visible()


def test_linked_attributes_plugin_visibility(topology_st, page, browser_name):
    """ Test Linked Attributes plugin visibility

    :id: 21cb6021-dc6f-4f26-a6a3-f4311c9afe2e
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on Linked Attributes button on the side panel.
         3. Check if Add Config button is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on Linked Attributes plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Linked Attributes').wait_for()
    frame.get_by_text('Linked Attributes').click()
    frame.get_by_role('button', name='Add Config').wait_for()
    assert frame.get_by_role('button', name='Add Config').is_visible()


def test_managed_entries_plugin_visibility(topology_st, page, browser_name):
    """ Test Managed Entries plugin visibility

    :id: fd2dcaf9-422b-4d17-85f2-bc12427adc1c
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on Managed Entries button on the side panel.
         3. Check if Create Template button is visible.
         4. Click on Definitions tab.
         5. Check if Add Definition button is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
         4. Success
         5. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on Managed Entries plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Plugin is enabledManaged Entries').wait_for()
    frame.get_by_text('Plugin is enabledManaged Entries').click()
    frame.get_by_role('button', name='Create Template').wait_for()
    assert frame.get_by_role('button', name='Create Template').is_visible()

    log.info('Click on Definitions tab and check if element is loaded.')
    frame.get_by_role('tab', name='Definitions').click()
    assert frame.get_by_role('button', name='Add Definition').is_visible()


def test_memberof_plugin_visibility(topology_st, page, browser_name):
    """ Test MemberOf plugin visibility

    :id: 865db69f-6e6b-4beb-b456-8e055fc0b14b
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on MemberOf button on the side panel.
         3. Check if Shared Config Entry text input field is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on MemberOf plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Plugin is disabledMemberOf').wait_for()
    frame.get_by_text('Plugin is disabledMemberOf').click()
    frame.locator('#memberOfConfigEntry').wait_for()
    assert frame.locator('#memberOfConfigEntry').is_visible()


def test_ldap_pass_through_auth_plugin_visibility(topology_st, page, browser_name):
    """ test LDAP Pass Through Auth plugin visibility

    :id: a47c4054-233f-4398-aaf6-eddfb442e53d
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on LDAP Pass Through Auth button on the side panel.
         3. Check if Add URL button is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on LDAP Pass Through Auth plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('LDAP Pass Through Auth').wait_for()
    frame.get_by_text('LDAP Pass Through Auth').click()
    frame.get_by_role('button', name='Add URL').wait_for()
    assert frame.get_by_role('button', name='Add URL').is_visible()


def test_pam_pass_through_auth_plugin_visibility(topology_st, page, browser_name):
    """ Test PAM Pass Through Auth visibility.

    :id: 99c72177-6c86-4b24-b754-38f9698bd70c
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on PAM Pass Through Auth button on the side panel.
         3. Check if Add Config button is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on PAM Pass Through Auth plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('PAM Pass Through Auth').wait_for()
    frame.get_by_text('PAM Pass Through Auth').click()
    frame.get_by_role('button', name='Add Config').wait_for()
    assert frame.get_by_role('button', name='Add Config').is_visible()


def test_posix_winsync_plugin_visibility(topology_st, page, browser_name):
    """ Test Posix Winsync plugin visibility.

    :id: 9998d23c-d550-4605-bcbe-d501f26d8a66
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on Posix Winsync button on the side panel.
         3. Check if Create MemberOf Task checkbox is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on Posix Winsync plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Posix Winsync').wait_for()
    frame.get_by_text('Posix Winsync').click()
    frame.locator('#posixWinsyncCreateMemberOfTask').wait_for()
    assert frame.locator('#posixWinsyncCreateMemberOfTask').is_visible()


def test_referential_integrity_plugin_visibility(topology_st, page, browser_name):
    """ Test Referential Integrity plugin visibility.

    :id: e868f520-a409-4ec8-b086-c86cf1f8855b
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on Referential Integrity button on the side panel.
         3. Check if Entry Scope text input field is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on Referential Integrity plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Referential Integrity').wait_for()
    frame.get_by_text('Referential Integrity').click()
    frame.locator('#entryScope').wait_for()
    assert frame.locator('#entryScope').is_visible()


def test_retro_changelog_plugin_visibility(topology_st, page, browser_name):
    """ Test Retro Changelog plugin visibility.

    :id: b1813138-25d9-4b73-ab99-1e27d51d0c53
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on Retro Changelog button on the side panel.
         3. Check if Is Replicated checkbox is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on Retro Changelog plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Retro Changelog').wait_for()
    frame.get_by_text('Retro Changelog').click()
    frame.locator('#isReplicated').wait_for()
    assert frame.locator('#isReplicated').is_visible()


def test_rootdn_access_control_plugin_visibility(topology_st, page, browser_name):
    """ Test RootDN Access Control plugin visibility

    :id: 3d57131f-a23e-4030-b51b-1dd3ebac95c9
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on RootDN Access Control button on the side panel.
         3. Check if Monday checkbox is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on RootDN Access Control plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('RootDN Access Control').wait_for()
    frame.get_by_text('RootDN Access Control').click()
    frame.locator('#allowMon').wait_for()
    assert frame.locator('#allowMon').is_visible()


def test_usn_plugin_visibility(topology_st, page, browser_name):
    """ Test USN plugin visibility

    :id: e1a60298-694e-4d04-ace9-164290a3786b
    :setup: Standalone instance
    :steps:
         1. Click on Plugins tab.
         2. Click on USN button on the side panel.
         3. Check if USN Global label is visible.
    :expectedresults:
         1. Success
         2. Success
         3. Element is visible
    """
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Plugins tab, click on USN Access Control plugin and check if element is loaded.')
    frame.get_by_role('tab', name='Plugins', exact=True).click()
    frame.get_by_text('Plugin is disabledUSN').wait_for()
    frame.get_by_text('Plugin is disabledUSN').click()
    frame.get_by_text('USN Global').wait_for()
    assert frame.get_by_text('USN Global').is_visible()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
