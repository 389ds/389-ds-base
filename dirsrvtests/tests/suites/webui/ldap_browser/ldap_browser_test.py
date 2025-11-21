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
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
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


def test_group_member_management(topology_st, page, browser_name):
    """ Test group member management with checkboxes
    
    :id: f8a9b1c2-d3e4-5f67-8901-a2b3c4d5e6f7
    :setup: Standalone instance
    :steps:
         1. Create 3 test users (demo_user1, demo_user2, demo_user3).
         2. Create a group with all 3 users as members.
         3. Navigate to LDAP Browser and open the group.
         4. Click Actions menu and select "Edit ...".
         5. In Current Members tab, select checkbox for first member.
         6. Click "Remove Selected Members" button.
         7. Verify member count decreased to 2.
         8. Select checkboxes for remaining 2 members.
         9. Click "Remove Selected Members" button.
         10. Verify member count decreased to 0 and shows "No Members".
    :expectedresults:
         1. Users created successfully
         2. Group created with 3 members
         3. Group is visible
         4. Group editor opens
         5. Checkbox can be clicked (not overlapped by text)
         6. Single member removed successfully
         7. Member count shows (2)
         8. Multiple checkboxes can be selected
         9. Multiple members removed successfully
         10. No members remain in group
    """
    
    setup_login(page)
    time.sleep(1)
    frame = check_frame_assignment(page, browser_name)
    
    users = UserAccounts(topology_st.standalone, 'dc=example,dc=com')
    groups = Groups(topology_st.standalone, 'dc=example,dc=com')
    user_names = ['demo_user1', 'demo_user2', 'demo_user3']
    group_name = 'demo_group'
    
    log.info('Setup test users and group')
    for username in user_names:
        users.ensure_state(properties={
            'uid': username,
            'cn': username,
            'sn': username,
            'uidNumber': '1000',
            'gidNumber': '1000',
            'homeDirectory': f'/home/{username}',
            'userPassword': 'password'
        })
    
    group = groups.ensure_state(properties={
        'cn': group_name,
        'member': [
            'uid=demo_user1,ou=People,dc=example,dc=com',
            'uid=demo_user2,ou=People,dc=example,dc=com',
            'uid=demo_user3,ou=People,dc=example,dc=com'
        ]
    })
    log.info(f'Group created/verified: {group.dn}')

    load_ldap_browser_tab(frame)
    
    log.info('Navigate to ou=Groups')
    frame.get_by_role('button').filter(has_text='ou=groups').click()
    frame.get_by_role('columnheader', name='Attribute').wait_for()
    time.sleep(1)
    
    log.info(f'Click on group: {group_name}')
    group_button = frame.get_by_role('button').filter(has_text=f'cn={group_name}')
    assert group_button.is_visible(), f"Group {group_name} not found in UI"
    group_button.click()
    time.sleep(1)
    
    log.info('Open Actions menu and click Edit')
    actions_button = frame.get_by_role("tabpanel", name="Tree View").get_by_role("button", name="Actions")
    actions_button.wait_for()
    actions_button.click()
    time.sleep(0.5)
    edit_menu = frame.get_by_role("menuitem", name="Edit ...")
    edit_menu.wait_for()
    edit_menu.click()
    time.sleep(2)
    
    log.info('Verify group editor shows 3 members')
    assert frame.get_by_text('Current Members (3)').is_visible()
    frame.get_by_text('Current Members', exact=False).click()
    time.sleep(1)

    log.info('Test single member deletion via checkbox')
    first_row_checkbox = frame.get_by_role("checkbox", name="Select row 0")
    assert first_row_checkbox.is_visible()
    first_row_checkbox.click()
    time.sleep(0.5)
    assert first_row_checkbox.is_checked()
    frame.get_by_role('button', name='Remove Selected Members').click()
    
    log.info('Wait for and handle confirmation modal')
    frame.get_by_text('Are you sure you want to remove these members?').wait_for(timeout=5000)
    frame.check('#modalChecked')
    frame.click('//button[normalize-space(.)="Delete Members"]')
    time.sleep(2)
    
    log.info('Verify UI and backend show 2 members')
    frame.get_by_text('Current Members (2)').wait_for(timeout=5000)
    group = groups.get(group_name)
    assert len(group.get_attr_vals_utf8('member')) == 2

    log.info('Test multiple member deletion via checkboxes')
    row0_checkbox = frame.get_by_role("checkbox", name="Select row 0")
    row1_checkbox = frame.get_by_role("checkbox", name="Select row 1")
    assert row0_checkbox.is_visible()
    assert row1_checkbox.is_visible()
    row0_checkbox.click()
    time.sleep(0.3)
    row1_checkbox.click()
    time.sleep(0.3)
    assert row0_checkbox.is_checked()
    assert row1_checkbox.is_checked()
    frame.get_by_role('button', name='Remove Selected Members').click()
    
    log.info('Wait for and handle confirmation modal')
    frame.get_by_text('Are you sure you want to remove these members?').wait_for(timeout=5000)
    frame.check('#modalChecked')
    frame.click('//button[normalize-space(.)="Delete Members"]')
    time.sleep(2)
    
    log.info('Verify all members removed')
    assert frame.get_by_text('No Members').is_visible()
    
    frame.get_by_role('button', name='Close', exact=False).click()
    time.sleep(1)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
