# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import distro
import os
import time

from lib389.utils import *
from lib389.topologies import topology_st

pytest.importorskip('playwright')

RHEL = 'Red Hat Enterprise Linux'


# in some cockpit versions the selectors got renamed, these functions help to check the versions
def check_cockpit_version_is_higher(version):
    f = os.popen("rpm -q --queryformat '%{VERSION}' cockpit")
    installed_version = f.readline()

    return installed_version >= version


def check_cockpit_version_is_lower(version):
    f = os.popen("rpm -q --queryformat '%{VERSION}' cockpit")
    installed_version = f.readline()

    return installed_version <= version


# the iframe selection differs for chromium and firefox browser
def determine_frame_selection(page, browser_name):
    if browser_name == 'firefox':
        frame = page.query_selector('iframe[name=\"cockpit1:localhost/389-console\"]').content_frame()
    else:
        frame = page.frame('cockpit1:localhost/389-console')

    return frame


# sometimes on a slow machine the iframe is not loaded yet, so we check for it until
# it is available or the timeout is exhausted
def check_frame_assignment(page, browser_name):
    timeout = 80
    count = 0
    frame = determine_frame_selection(page, browser_name)

    while (frame is None) and (count != timeout):
        log.info('Waiting 0.5 seconds for iframe availability')
        time.sleep(0.5)
        count += 0.5
        frame = determine_frame_selection(page, browser_name)

    return frame


def remove_instance_through_lib(topology):
    log.info('Check and remove instance before starting tests')
    if topology.standalone.exists():
        topology.standalone.delete()
        time.sleep(1)


def remove_instance_through_webui(topology, page, browser_name):
    frame = check_frame_assignment(page, browser_name)

    log.info('Check if instance exist')
    if topology.standalone.exists():
        log.info('Delete instance')
        frame.wait_for_selector('#ds-action')
        frame.click('#ds-action')
        frame.click('#remove-ds')
        frame.check('#modalChecked')
        frame.click('//button[normalize-space(.)=\'Remove Instance\']')
        frame = check_frame_assignment(page, browser_name)
        frame.is_visible("#no-inst-create-btn")
        time.sleep(1)
        log.info('Instance deleted')


def setup_login(page):
    password = ensure_str(os.getenv('PASSWD'))
    page.set_viewport_size({"width": 1920, "height": 1080})

    # increase default timeout to wait enough time on a slow machine for selector availability
    # (it will wait just enough time for the selector to be available,
    # it won't stop for 80 000 miliseconds each time it is called)
    page.set_default_timeout(65000)

    page.goto("http://localhost:9090/")

    # We are at login page
    page.fill('#login-user-input', 'root')
    page.fill('#login-password-input', password)
    page.click("#login-button")
    time.sleep(2)

    if RHEL in distro.linux_distribution():
        page.wait_for_selector('text=Red Hat Directory Server')
        page.click('text=Red Hat Directory Server')
    else:
        page.wait_for_selector('text=389 Directory Server')
        page.click('text=389 Directory Server')


@pytest.fixture(scope="function")
def setup_page(topology_st, page, browser_name, request):
    # remove instance if it exists before starting tests
    remove_instance_through_lib(topology_st)
    setup_login(page)

    def fin():
        remove_instance_through_webui(topology_st, page, browser_name)

    request.addfinalizer(fin)


def enable_replication(frame):
    log.info('Check if replication is enabled, if not enable it in order to proceed further with test.')
    frame.get_by_role('tab', name='Replication').click()
    time.sleep(2)
    if frame.get_by_role('button', name='Enable Replication').is_visible():
        frame.get_by_role('button', name='Enable Replication').click()
        frame.fill('#enableBindPW', 'redhat')
        frame.fill('#enableBindPWConfirm', 'redhat')
        frame.get_by_role("dialog", name="Enable Replication").get_by_role("button",
                                                                           name="Enable Replication").click()
        frame.get_by_role('button', name='Add Replication Manager').wait_for()
        assert frame.get_by_role('button', name='Add Replication Manager').is_visible()

def load_ldap_browser_tab(frame):
    frame.get_by_role('tab', name='LDAP Browser', exact=True).click()
    frame.get_by_role('button').filter(has_text='dc=example,dc=com').click()
    frame.get_by_role('columnheader', name='Attribute').wait_for()
    time.sleep(1)


def prepare_page_for_entry(frame, entry_type):
    frame.get_by_role("tabpanel", name="Tree View").get_by_role("button", name="Actions").click()
    frame.get_by_role("menuitem", name="New ...").click()
    frame.get_by_label(f"Create a new {entry_type}").check()
    frame.get_by_role("button", name="Next").click()


def finish_entry_creation(frame, entry_type, entry_data):
    frame.get_by_role("button", name="Next").click()
    if entry_type == "User":
        frame.get_by_role("contentinfo").get_by_role("button", name="Create User").click()
    elif entry_type == "custom Entry":
        frame.get_by_role("button", name="Create Entry").click()
    else:
        frame.get_by_role("button", name="Create", exact=True).click()
    frame.get_by_role("button", name="Finish").click()
    frame.get_by_role("button").filter(has_text=entry_data['suffixTreeEntry']).wait_for()


def create_entry(frame, entry_type, entry_data):
    prepare_page_for_entry(frame, entry_type)

    if entry_type == 'User':
        frame.get_by_role("button", name="Options menu").click()
        frame.get_by_role("option", name="Posix Account").click()
        frame.get_by_role("button", name="Next", exact=True).click()
        frame.get_by_role("button", name="Next", exact=True).click()

        for row, value in enumerate(entry_data.values()):
            if row > 5:
                break
            frame.get_by_role("button", name=f"Place row {row} in edit mode").click()
            frame.get_by_role("textbox", name="_").fill(value)
            frame.get_by_role("button", name=f"Save row edits for row {row}").click()

    elif entry_type == 'Group':
        frame.get_by_role("button", name="Next").click()
        frame.locator("#groupName").fill(entry_data["group_name"])
        frame.get_by_role("button", name="Next").click()

    elif entry_type == 'Organizational Unit':
        frame.get_by_role("button", name="Next", exact=True).click()
        frame.get_by_role("button", name="Place row 0 in edit mode").click()
        frame.get_by_role("textbox", name="_").fill(entry_data['ou_name'])
        frame.get_by_role("button", name="Save row edits for row 0").click()

    elif entry_type == 'Role':
        frame.locator("#namingVal").fill(entry_data['role_name'])
        frame.get_by_role("button", name="Next").click()
        frame.get_by_role("button", name="Next", exact=True).click()

    elif entry_type == 'custom Entry':
        frame.get_by_role("checkbox", name="Select row 0").check()
        frame.get_by_role("button", name="Next", exact=True).click()
        frame.get_by_role("checkbox", name="Select row 1").check()
        frame.get_by_role("button", name="Next", exact=True).click()
        frame.get_by_role("button", name="Place row 0 in edit mode").click()
        frame.get_by_role("textbox", name="_").fill(entry_data['uid'])
        frame.get_by_role("button", name="Save row edits for row 0").click()
        frame.get_by_role("button", name="Place row 1 in edit mode").click()
        frame.get_by_role("textbox", name="_").fill(entry_data['entry_name'])
        frame.get_by_role("button", name="Save row edits for row 1").click()

    finish_entry_creation(frame, entry_type, entry_data)

def delete_entry(frame):
    frame.get_by_role("tabpanel", name="Tree View").get_by_role("button", name="Actions").click()
    frame.get_by_role("menuitem", name="Delete ...").click()
    frame.get_by_role("button", name="Next").click()
    frame.get_by_role("button", name="Next").click()
    frame.get_by_text("No, don't delete.").click()
    frame.get_by_role("button", name="Delete").click()
    frame.get_by_role("button", name="Finish").click()
    time.sleep(1)
