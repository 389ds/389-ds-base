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
import distro

from lib389.cli_idm.account import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from .. import setup_page, remove_instance_through_lib, check_cockpit_version_is_higher, check_frame_assignment, \
    setup_login, check_cockpit_version_is_lower

pytestmark = pytest.mark.skipif(os.getenv('WEBUI') is None, reason="These tests are only for WebUI environment")
pytest.importorskip('playwright')

RHEL = 'Red Hat Enterprise Linux'


def test_login_no_instance(topology_st, page, browser_name):
    """ Test login to WebUI is successful

    :id: 0a85c1ec-20f3-41ae-8203-3951e74f34e7
    :setup: Standalone instance
    :steps:
         1. Go to cockpit login page
         2. Fill user and password
         3. Click on login button
         4. Go to Red Hat Directory server side tab page
         5. Check there is Create New Instance button when no instance exists
    :expectedresults:
         1. Page redirection to login page successful
         2. Success
         3. Login successful
         4. Page redirection successful
         5. Button is visible
    """

    remove_instance_through_lib(topology_st)
    password = ensure_str(os.getenv('PASSWD'))

    # if we use setup_page from __init__.py we would be logged in already
    page.set_viewport_size({"width": 1920, "height": 1080})

    # increase default timeout to wait enough time on a slow machine for selector availability
    # (it will wait just enough time for the selector to be available,
    # it won't stop for 60 000 miliseconds each time it is called)
    page.set_default_timeout(60000)

    page.goto("http://localhost:9090/")
    assert page.url == 'http://localhost:9090/'
    page.wait_for_selector('#login-user-input')

    # We are at login page
    log.info('Let us log in')
    page.fill('#login-user-input', 'root')
    page.fill('#login-password-input', password)
    page.click('#login-button')
    time.sleep(2)

    if RHEL in distro.linux_distribution():
        page.wait_for_selector('text=Red Hat Directory Server')
        assert page.is_visible('text=Red Hat Directory Server')
        log.info('Let us go to RHDS side tab page')
        page.click('text=Red Hat Directory Server')
    else:
        page.wait_for_selector('text=389 Directory Server')
        assert page.is_visible('text=389 Directory Server')
        log.info('Let us go to RHDS side tab page')
        page.click('text=389 Directory Server')

    log.info('Login successful')
    assert page.url == 'http://localhost:9090/389-console'

    log.info('Check there is Create New Instance button')
    frame = check_frame_assignment(page, browser_name)
    frame.wait_for_selector('#no-inst-create-btn')
    assert frame.is_visible("#no-inst-create-btn")


def test_logout(topology_st, page):
    """ Test logout from WebUI is successful

    :id: a7e71179-3ef0-4e4e-baca-a36beeef71b6
    :setup: Standalone instance
    :steps:
         1. Go to cockpit login page
         2. Fill user and password
         3. Click on login button
         4. Click on root user and choose Log Out option
         5. Check we have been redirected to the login page
    :expectedresults:
         1. Page redirection to login page successful
         2. Success
         3. Login successful
         4. Page redirection successful
         5. We are at login page
    """

    setup_login(page)
    assert page.url == "http://localhost:9090/389-console"

    # checking cockpit versions because selector ids got renamed in between
    log.info('Let us log out')
    if check_cockpit_version_is_higher('258'):
        page.click('#toggle-menu')
        page.click('#logout')
    elif check_cockpit_version_is_higher('250') and check_cockpit_version_is_lower('257'):
        page.click('#navbar-dropdown')
        page.click('#go-logout')
    else:
        page.click('#content-user-name')
        page.click('#go-logout')

    page.wait_for_selector('#login-user-input')
    assert page.is_visible('#login-user-input')
    log.info('Log out successful')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

