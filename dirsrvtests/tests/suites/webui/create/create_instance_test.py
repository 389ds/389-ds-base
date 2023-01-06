# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
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
from .. import setup_page, check_frame_assignment

pytestmark = pytest.mark.skipif(os.getenv('WEBUI') is None, reason="These tests are only for WebUI environment")
pytest.importorskip('playwright')

SERVER_ID = 'standalone1'


def test_no_instance(topology_st, page, browser_name, setup_page):
    """ Test page of Red Hat Directory Server when no instance is created

    :id: 04c962b1-df5e-470d-8e19-aa6b77988c75
    :setup: Standalone instance
    :steps:
         1. Go to Red Hat Directory server side tab page
         2. Check there is Create New Instance button when no instance exists
    :expectedresults:
         1. Success
         2. Button is visible
    """
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)
    log.info('Check the Create New instance button is present')
    frame.wait_for_selector('#noInsts')
    assert frame.is_visible('#noInsts')


def test_instance_button_disabled_passwd_short(topology_st, page, browser_name, setup_page):
    """ Test Create Instance button is disabled when password is too short

    :id: 9d413b70-7746-45ef-b389-9b67fcbc945a
    :setup: Standalone instance
    :steps:
         1. Click on Create New Instance button
         2. Fill serverID
         3. Fill password shorter than eight characters
         4. Fill passwordConfirm shorter than eight character
         5. Check the Create Instance button is disabled when password is too short
    :expectedresults:
         1. A pop-up window should appear with create instance details
         2. Success
         3. Success
         4. Success
         5. Button is disabled
    """
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Create New Instance button')
    frame.click("#no-inst-create-btn")
    frame.wait_for_selector('#createServerId')

    log.info('Fill serverID and short password')
    frame.fill('#createServerId', SERVER_ID)
    frame.fill('#createDMPassword', 'redhat')
    frame.fill('#createDMPasswordConfirm', 'redhat')

    log.info('Check Create Instance button is disabled')
    assert frame.is_disabled("text=Create Instance")


def test_create_instance_without_database(topology_st, page, browser_name, setup_page):
    """ Test create instance without database

    :id: 7390c009-cb0d-406a-962a-4a1f0f02cfe6
    :setup: Standalone instance
    :steps:
         1. Click on Create New Instance button
         2. Fill serverID
         3. Fill password longer than eight characters
         4. Fill passwordConfirm longer than eight character
         5. Click on the Create Instance button
    :expectedresults:
         1. A pop-up window should appear with create instance details
         2. Success
         3. Success
         4. Success
         5. Page redirection successful and instance is created
    """
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Create New Instance button')
    frame.click("#no-inst-create-btn")
    frame.wait_for_selector('#createServerId')

    log.info('Fill serverID and password longer than eight characters')
    frame.fill('#createServerId', SERVER_ID)
    frame.fill('#createDMPassword', 'password')
    frame.fill('#createDMPasswordConfirm', 'password')

    log.info('Click Create Instance button')
    frame.click("text=Create Instance")
    frame.wait_for_selector("#serverId")

    log.info('Check that created serverID is present')
    assert frame.is_visible("#serverId")


def test_create_instance_database_suffix_entry(topology_st, page, browser_name, setup_page):
    """ Test create instance with database and suffix entry

    :id: 52703fc9-2f5b-49f9-b80b-bd0703008118
    :setup: Standalone instance
    :steps:
         1. Click on Create New Instance button
         2. Fill serverID, user and password
         3. Check Create Database checkbox
         4. Fill database suffix and name
         5. Select Create Suffix Entry from drop-down list
         6. Click on the Create Instance button
    :expectedresults:
         1. A pop-up window should appear with create instance details
         2. Success
         3. Success
         4. Success
         5. Success
         6. Page redirection successful and instance is created
    """
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Create New Instance button')
    frame.click("#no-inst-create-btn")
    frame.wait_for_selector('#createServerId')

    log.info('Fill serverID and password longer than eight characters')
    frame.fill('#createServerId', SERVER_ID)
    frame.fill('#createDMPassword', 'password')
    frame.fill('#createDMPasswordConfirm', 'password')

    log.info('Choose to create database with suffix entry')
    if ds_is_older('2.0.10'):
        frame.check('text="Create Database" >> input[type="checkbox"]')
        frame.fill('input[placeholder="e.g. dc=example,dc=com"]', 'dc=example,dc=com')
        frame.fill('input[placeholder="e.g. userRoot"]', 'userRoot')
    else:
        frame.check('#createDBCheckbox')
        frame.fill('#createDBSuffix','dc=example,dc=com')
        frame.fill('#createDBName','userRoot')

    frame.select_option('#createInitDB', 'createSuffix')

    frame.click("text=Create Instance")
    frame.wait_for_selector("#serverId")

    log.info('Check that created serverID is present')
    assert frame.is_visible("#serverId")


def test_create_instance_database_sample_entries(topology_st, page, browser_name, setup_page):
    """ Test create instance with database and sample entries

    :id: d6d8cb10-8a5f-428d-b9a7-1c65b85986b7
    :setup: Standalone instance
    :steps:
         1. Click on Create New Instance button
         2. Fill serverID, user and password
         3. Check Create Database checkbox
         4. Fill database suffix and name
         5. Select Create Sample Entries from drop-down list
         6. Click on the Create Instance button
    :expectedresults:
         1. A pop-up window should appear with create instance details
         2. Success
         3. Success
         4. Success
         5. Success
         6. Page redirection successful and instance is created
    """
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    log.info('Click on Create New Instance button')
    frame.click("#no-inst-create-btn")
    frame.wait_for_selector('#createServerId')

    log.info('Fill serverID and password longer than eight characters')
    frame.fill('#createServerId', SERVER_ID)
    frame.fill('#createDMPassword', 'password')
    frame.fill('#createDMPasswordConfirm', 'password')

    log.info('Choose to create database with sample entries')
    if ds_is_older('2.0.10'):
        frame.check('text="Create Database" >> input[type="checkbox"]')
        frame.fill('input[placeholder="e.g. dc=example,dc=com"]', 'dc=example,dc=com')
        frame.fill('input[placeholder="e.g. userRoot"]', 'userRoot')
    else:
        frame.check('#createDBCheckbox')
        frame.fill('#createDBSuffix','dc=example,dc=com')
        frame.fill('#createDBName','userRoot')

    frame.select_option('#createInitDB', 'createSample')

    log.info('Click Create Instance button')
    frame.click("text=Create Instance")
    frame.wait_for_selector("#serverId")

    log.info('Check that created serverID is present')
    assert frame.is_visible("#serverId")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
