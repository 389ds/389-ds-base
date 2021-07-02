# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest

from lib389.utils import *
from lib389.topologies import topology_st

pytest.importorskip('playwright')

def remove_instance_through_lib(topology):
    log.info('Check and remove instance before starting tests')
    if topology.standalone.exists():
        topology.standalone.delete()


def remove_instance_through_webui(topology, page):
    frame = page.frame('cockpit1:localhost/389-console')

    log.info('Check if instance exist')
    if topology.standalone.exists():
        log.info('Delete instance')
        frame.wait_for_selector('#ds-action')
        frame.click('#ds-action')
        frame.click('#remove-ds')
        frame.check('text=", I am sure." >> input[type="checkbox"]')
        frame.click('//button[normalize-space(.)=\'Remove Instance\']')
        frame.wait_for_selector("#no-inst-create-btn")
        log.info('Instance deleted')
    #frame.wait_for_selector("#no-inst-create-btn")

    #assert frame.is_visible("#no-inst-create-btn")


def setup_login(page):
    page.set_viewport_size({"width": 1920, "height": 1080})
    page.goto("http://localhost:9090/")

    # We are at login page
    page.fill('#login-user-input', 'root')
    page.fill('#login-password-input', 'redhat')
    page.click("#login-button")
    page.wait_for_selector('text=Red Hat Directory Server')
    page.click('text=Red Hat Directory Server')


@pytest.fixture(scope="function")
def setup_page(topology_st, page, request):
    # remove instance if it exists before starting tests
    remove_instance_through_lib(topology_st)
    setup_login(page)

    def fin():
        remove_instance_through_webui(topology_st, page)

    request.addfinalizer(fin)