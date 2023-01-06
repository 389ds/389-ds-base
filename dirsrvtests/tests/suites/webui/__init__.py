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
