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
from .. import setup_page, check_frame_assignment, setup_login, create_entry, delete_entry, load_ldap_browser_tab

pytestmark = pytest.mark.skipif(os.getenv('WEBUI') is None, reason="These tests are only for WebUI environment")
pytest.importorskip('playwright')

SERVER_ID = 'standalone1'

entry_data = {'User': {'cn': 'John Smith',
                       'displayName': 'John Smith',
                       'gidNumber': '1204',
                       'homeDirectory': 'user/jsmith',
                       'uid': '1204',
                       'uidNumber': '1204',
                       'suffixTreeEntry': 'cn=John Smith'},
              'Group': {'group_name': 'testgroup',
                        'suffixTreeEntry': 'cn=testgroup'},
              'Organizational Unit': {'ou_name': 'testou',
                                      'suffixTreeEntry': 'ou=testou'},
              'Role': {'role_name': 'testrole',
                       'suffixTreeEntry': 'cn=testrole'},
              'custom Entry': {'uid': '1234',
                               'entry_name': 'test_entry',
                               'suffixTreeEntry': 'uid=1234'}}


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


def test_create_and_delete_user(topology_st, page, browser_name):
    """ Test to create and delete user

    :id: eb08c1d7-cbee-4a37-b724-429e1cdfe092
    :setup: Standalone instance
    :steps:
         1. Call load_LDAP_browser_tab function.
         2. Click on ou=people.
         3. Call create_entry function to create new user.
         4. Check that new user is successfully created.
         5. Click on newly created user.
         6. Call delete_entry function to delete user.
         7. Check that newly created user is deleted.
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. User is created
         5. Success
         6. Success
         7. User is deleted
    """

    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    entry_type = 'User'
    test_data = entry_data.get(entry_type)
    load_ldap_browser_tab(frame)

    log.info('Click on ou=people.')
    frame.get_by_role('button').filter(has_text='ou=people').click()
    frame.get_by_role('columnheader', name='Attribute').wait_for()
    time.sleep(1)

    log.info('Create a new user named John Smith by calling create_entry function.')
    create_entry(frame, entry_type, test_data)
    assert frame.get_by_role("button").filter(has_text=f"cn={test_data['displayName']}").is_visible()

    log.info('Click on cn=John Smith and call delete_entry function to delete it.')
    frame.get_by_role("button").filter(has_text=f"cn={test_data['displayName']}").click()
    time.sleep(1)
    delete_entry(frame)
    assert frame.get_by_role("button").filter(has_text=f"cn={test_data['displayName']}").count() == 0


def test_create_and_delete_group(topology_st, page, browser_name):
    """ Test to create and delete group

    :id: dcd61b3a-b6bc-4255-8a38-c1f98b435ad9
    :setup: Standalone instance
    :steps:
         1. Call load_LDAP_browser_tab function.
         2. Click on ou=groups.
         3. Call create_entry function to create new group.
         4. Check that new group is successfully created.
         5. Click on newly created group.
         6. Call delete_entry function to delete group.
         7. Check that newly created group is deleted.
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Group is created
         5. Success
         6. Success
         7. Group is deleted
    """

    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    entry_type = 'Group'
    test_data = entry_data.get(entry_type)
    load_ldap_browser_tab(frame)

    log.info('Click on groups.')
    frame.get_by_role('button').filter(has_text='ou=groups').click()
    frame.get_by_role('columnheader', name='Attribute').wait_for()
    time.sleep(1)

    log.info('Call create_entry function to create a new group.')
    create_entry(frame, entry_type, test_data)
    assert frame.get_by_role("button").filter(has_text=f"cn={test_data['group_name']}").is_visible()

    log.info('Click on cn=testgroup and call delete_entry function to delete it.')
    frame.get_by_role("button").filter(has_text=f"cn={test_data['group_name']}").click()
    time.sleep(1)
    delete_entry(frame)
    assert frame.get_by_role("button").filter(has_text=f"cn={test_data['group_name']}").count() == 0


def test_create_and_delete_organizational_unit(topology_st, page, browser_name):
    """ Test to create and delete organizational unit

    :id: ce42b85d-6eab-459b-a61d-b77e7979be73
    :setup: Standalone instance
    :steps:
         1. Call load_LDAP_browser_tab function.
         2. Call create_entry function to create new organizational unit.
         3. Check that new ou is successfully created.
         4. Click on newly created ou.
         5. Call delete_entry function to delete ou.
         6. Check that newly created ou is deleted.
    :expectedresults:
         1. Success
         2. Success
         3. New organizational unit is created
         4. Success
         5. Success
         6. New organizational unit is deleted.
    """

    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    entry_type = 'Organizational Unit'
    test_data = entry_data.get(entry_type)
    load_ldap_browser_tab(frame)

    log.info('Call create_entry function to create new organizational unit named testou.')
    create_entry(frame, entry_type, test_data)
    assert frame.get_by_role("button").filter(has_text=f"ou={test_data['ou_name']}").is_visible()

    log.info('Click on ou=testou and call delete_entry function to delete it.')
    frame.get_by_role("button").filter(has_text=f"ou={test_data['ou_name']}").click()
    time.sleep(1)
    delete_entry(frame)
    assert frame.get_by_role("button").filter(has_text=f"ou={test_data['ou_name']}").count() == 0


def test_create_and_delete_role(topology_st, page, browser_name):
    """ Test to create and delete role

    :id: 39d54c08-5841-403c-9d88-0179f57c27b1
    :setup: Standalone instance
    :steps:
         1. Call load_LDAP_browser_tab function.
         2. Call create_entry function to create new role.
         3. Check that new role is successfully created.
         4. Click on newly created role.
         5. Call delete_entry function to delete role.
         6. Check that newly created role is deleted.
    :expectedresults:
         1. Success
         2. Success
         3. New role is created
         4. Success
         5. Success
         6. New role is deleted
    """

    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    entry_type = 'Role'
    test_data = entry_data.get(entry_type)
    load_ldap_browser_tab(frame)

    log.info('Call create_entry function to create a new role named testrole.')
    create_entry(frame, entry_type, test_data)
    assert frame.get_by_role("button").filter(has_text=f"cn={test_data['role_name']}").is_visible()

    log.info('Click on cn=testrole and call delete_entry function to delete it.')
    frame.get_by_role("button").filter(has_text=f"cn={test_data['role_name']}").click()
    time.sleep(1)
    delete_entry(frame)
    assert frame.get_by_role("button").filter(has_text=f"cn={test_data['role_name']}").count() == 0


def test_create_and_delete_custom_entry(topology_st, page, browser_name):
    """ Test to create and delete custom entry

    :id: 21906d0f-f097-4f30-8308-16085519159a
    :setup: Standalone instance
    :steps:
         1. Call load_LDAP_browser_tab function.
         2. Call create_entry function to create new custom entry.
         3. Check that new custom entry is successfully created.
         4. Click on newly created custom entry.
         5. Call delete_entry function to delete custom entry.
         6. Check that newly created custom entry is deleted.
    :expectedresults:
         1. Success
         2. Success
         3. New custom entry is created
         4. Success
         5. Success
         6. New custom entry is created
    """

    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)

    entry_type = 'custom Entry'
    test_data = entry_data.get(entry_type)
    load_ldap_browser_tab(frame)

    log.info('Call create_entry function to create new custom entry.')
    create_entry(frame, entry_type, test_data)
    assert frame.get_by_role("button").filter(has_text=f"uid={test_data['uid']}").is_visible()

    log.info('Click on uid=1234 and call delete_entry function to delete it.')
    frame.get_by_role("button").filter(has_text=f"uid={test_data['uid']}").click()
    time.sleep(1)
    delete_entry(frame)
    assert frame.get_by_role("button").filter(has_text=f"uid={test_data['uid']}").count() == 0


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
