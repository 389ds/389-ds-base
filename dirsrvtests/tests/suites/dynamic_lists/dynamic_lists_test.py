# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import ldap
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.config import LDBMConfig


pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def dynamic_lists_setup(topo, request):
    log.info("Creating test users")
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user_dns = []
    num_users = 5

    for i in range(1, num_users + 1):
        user = users.create(properties={
            'uid': f'testuser{i}',
            'cn': f'testuser{i}',
            'sn': f'User{i}',
            'uidNumber': f'{1000 + i}',
            'gidNumber': f'{1000 + i}',
            'homeDirectory': f'/home/testuser{i}',
        })
        user_dns.append(user.dn)
        log.info(f"Created user: {user.dn}")

    def fin():
        for user in users.list():
            user.delete()
    request.addfinalizer(fin)

    return user_dns


def test_dynamic_lists_memberurl_all_users(topo, dynamic_lists_setup):
    """Test dynamic lists plugin with memberURL filter (uid=testuser*) to list all users

    :id: 6913fc98-fb22-4f21-817d-4cc19be1f1cd
    :setup: Standalone Instance
    :steps:
        1. Configure dynamic lists with objectclass, url_attr, and list_attr
        2. Create a group with memberURL attribute containing LDAP URL with filter (uid=testuser*)
        3. Search for the group and verify all users are listed as members
        4. Search using member as a filter and see if the group is returned
    :expectedresults:
        1. Success
        2. Group should be created with memberURL successfully
        3. All users should be listed as members of the group
        4. Group should be returned when searching using member as a filter
    """

    inst = topo.standalone

    user_dns = dynamic_lists_setup

    # Step 1: Configure dynamic lists with objectclass, url_attr, and list_attr
    ldbmconfig = LDBMConfig(inst)
    ldbmconfig.replace('nsslapd-dynamic-lists-enabled', b'on')
    ldbmconfig.replace('nsslapd-dynamic-lists-attr', b'member')
    ldbmconfig.replace('nsslapd-dynamic-lists-oc', b'groupOfUrls')
    ldbmconfig.replace('nsslapd-dynamic-lists-url-attr', b'memberURL')

    # Step 2: Create a group with memberURL attribute
    log.info("Creating group with memberURL")
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={
        'cn': 'test_dynamic_group',
        'objectClass': ['groupOfUrls','groupOfNames'],
        'memberURL': f'ldap:///ou=people,{DEFAULT_SUFFIX}??sub?(uid=testuser*)',
    })
    group_dn = group.dn
    log.info(f"Created group: {group_dn} with memberURL: ldap:///{DEFAULT_SUFFIX}??sub?(uid=*)")

    # Step 3: Search for the group to trigger the plugin and verify all users
    # are listed as members. The plugin processes memberURL during search
    # operations and dynamically adds member attributes
    log.info("Searching for group to trigger dynamic lists plugin")
    members = group.list_members()
    log.info(f"Found {len(members)} members in group")
    log.info(f"Members: {members}")

    # Verify all users are members
    assert len(members) == 5, f"Expected {5} members, got {len(members)}. Members: {members}"
    for user_dn in user_dns:
        assert user_dn in members, f"User {user_dn} should be a member of the group. Members: {members}"

    # Step 4: Now search using member as a filter and see if the group is returned
    results = groups.filter(f'(member={members[2]})')
    assert len(results) == 1, f"Expected 1 group, got {len(results)}. Results: {results}"
    assert results[0].dn == group_dn, (f"Group {results[0].dn} should be returned when searching "
                                       "using member as a filter. Results: {results}")

    log.info("Test completed successfully - all users are listed as members "
             "and the group is returned when searching using member as a filter")


def test_dynamic_lists_non_dn_attribute(topo, dynamic_lists_setup):
    """Test dynamic lists plugin with memberURL filter (uid=testuser*) to list
    all users homeDirectory attribute

    :id: c44ebc24-4fd5-45f7-9699-b481527821f0
    :setup: Standalone Instance
    :steps:
        1. Configure dynamic lists with objectclass, url_attr, and list_attr
        2. Create a group with memberURL attribute containing LDAP URL with
           filter (uid=testuser*) and requested attribute (homeDirectory)
        3. Search for the group and verify all users homeDirectory attribute
           are listed in the entry
    :expectedresults:
        1. Success
        2. Group should be created with memberURL successfully
        3. All users homeDirectory attribute should be listed in the entry
    """

    inst = topo.standalone

    # Step 1: Configure dynamic lists with objectclass, url_attr, and list_attr
    ldbmconfig = LDBMConfig(inst)
    ldbmconfig.replace('nsslapd-dynamic-lists-enabled', b'on')
    ldbmconfig.replace('nsslapd-dynamic-lists-attr', b'member')
    ldbmconfig.replace('nsslapd-dynamic-lists-oc', b'groupOfUrls')
    ldbmconfig.replace('nsslapd-dynamic-lists-url-attr', b'memberURL')

    # Step 2: Create a group with memberURL attribute
    log.info("Creating group with memberURL")
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={
        'cn': 'test_dynamic_group_2',
        'objectClass': 'groupOfUrls',
        'memberURL': f'ldap:///{DEFAULT_SUFFIX}?homeDirectory?sub?(uid=testuser*)',
    })
    group_dn = group.dn
    log.info(f"Created group: {group_dn} with memberURL: ldap:///{DEFAULT_SUFFIX}?"
             "homeDirectory?sub?(uid=testuser*)")

    # Step 3: Search for the group to trigger the plugin and verify all
    # homeDirectories are listed in the entry
    values = group.get_attr_vals_utf8_l('homeDirectory')
    assert len(values) == 5, f"Expected {5} values, got {len(values)}. Values: {values}"
    for value in ['/home/testuser1', '/home/testuser2', '/home/testuser3',
                  '/home/testuser4', '/home/testuser5']:
        assert value in values, f"Value {value} should be listed in the group. Values: {values}"


def test_dynamic_lists_invalid_values(topo):
    """Test dynamic lists plugin with memberURL filter (uid=testuser*) to list
    all users homeDirectory attribute

    :id: bbe98cdc-2255-45af-a40d-442d3995a388
    :setup: Standalone Instance
    :steps:
        1. Configure dynamic lists with objectclass, url_attr, and list_attr
        2. Configure dynamic lists with non-existing objectclass
        3. Configure dynamic lists with non-existing url_attr
        4. Configure dynamic lists with non-existing list_attr
        5. Configure dynamic lists with non-dn syntax attribute for list-attr
    :expectedresults:
        1. Plugin should be enabled successfully
        2. Plugin should not be configured successfully
        3. Plugin should not be configured successfully
        4. Plugin should not be configured successfully
        5. Plugin should not be configured successfully
        6. Plugin should not be configured successfully
    """

    inst = topo.standalone

    # Step 1: Configure dynamic lists with objectclass, url_attr, and list_attr
    ldbmconfig = LDBMConfig(inst)
    ldbmconfig.replace('nsslapd-dynamic-lists-enabled', b'on')
    ldbmconfig.replace('nsslapd-dynamic-lists-attr', b'member')
    ldbmconfig.replace('nsslapd-dynamic-lists-oc', b'groupOfUrls')
    ldbmconfig.replace('nsslapd-dynamic-lists-url-attr', b'memberURL')

    # Step 2: Configure dynamic lists with non-existing objectclass
    log.info("Configuring dynamic lists plugin")
    with pytest.raises(ldap.LDAPError) as e:
        ldbmconfig.replace('nsslapd-dynamic-lists-oc', b'doesNotExist')

    # Step 3: Configure dynamic lists with non-existing url_attr
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        ldbmconfig.replace('nsslapd-dynamic-lists-url-attr', b'doesNotExist')

    # Step 4: Configure dynamic lists with non-existing list_attr
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
       ldbmconfig.replace('nsslapd-dynamic-lists-attr', b'doesNotExist')

    # Step 5: Configure dynamic lists with non-dn syntax attribute for list-attr
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        ldbmconfig.replace('nsslapd-dynamic-lists-attr', b'cn')

    # Step 6: Configure dynamic lists with where url_attr and list_attr are the same
    ldbmconfig.replace('nsslapd-dynamic-lists-attr', b'uniquemember')
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        ldbmconfig.replace('nsslapd-dynamic-lists-url-attr', b'uniquemember')


def test_dynamic_lists_multiple_memberurl(topo, dynamic_lists_setup):
    """Test dynamic lists plugin with multiple memberURL attributes pointing to
    different organizational units

    :id: a1b2c3d4-e5f6-7890-abcd-ef1234567890
    :setup: Standalone Instance
    :steps:
        1. Configure dynamic lists with objectclass, url_attr, and list_attr
        2. Create a new organizational unit with one entry under it
        3. Create a group (demo_group) with memberURL attribute pointing to DEFAULT_SUFFIX
        4. Add an additional memberURL attribute to demo_group pointing to the new OU
           with the same filter as the first memberURL
        5. Search for the group and verify all users from both locations are listed as members
        6. Search for the group again and verify all users from both locations are listed as members
    :expectedresults:
        1. Success
        2. Organizational unit and entry should be created successfully
        3. Group should be created with first memberURL successfully
        4. Second memberURL should be added successfully
        5. All users from both locations should be listed as members of the group
        6. All users from both locations should be listed as members of the group
    """

    inst = topo.standalone
    user_dns = dynamic_lists_setup

    # Step 1: Configure dynamic lists with objectclass, url_attr, and list_attr
    ldbmconfig = LDBMConfig(inst)
    ldbmconfig.replace('nsslapd-dynamic-lists-enabled', b'on')
    ldbmconfig.replace('nsslapd-dynamic-lists-attr', b'member')
    ldbmconfig.replace('nsslapd-dynamic-lists-oc', b'groupOfUrls')
    ldbmconfig.replace('nsslapd-dynamic-lists-url-attr', b'memberURL')

    # Step 2: Create a new organizational unit with one entry under it
    log.info("Creating new organizational unit")
    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    test_ou = ous.create(properties={
        'ou': 'TestOU',
        'description': 'Test Organizational Unit for dynamic lists',
    })
    ou_dn = test_ou.dn
    log.info(f"Created organizational unit: {ou_dn}")

    # Create a user entry under the new OU
    log.info("Creating user entry under the new OU")
    ou_users = UserAccounts(inst, DEFAULT_SUFFIX, rdn="ou=TestOU")
    ou_user = ou_users.create(properties={
        'uid': 'testuser6',
        'cn': 'testuser6',
        'sn': 'User6',
        'uidNumber': '1006',
        'gidNumber': '1006',
        'homeDirectory': '/home/testuser6',
    })
    ou_user_dn = ou_user.dn
    log.info(f"Created user under OU: {ou_user_dn}")

    # Step 3: Create a group (demo_group) with memberURL attribute pointing to DEFAULT_SUFFIX
    log.info("Creating demo_group with first memberURL")
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    demo_group = groups.create(properties={
        'cn': 'demo_group_multiple',
        'objectClass': 'groupOfUrls',
        'memberURL': f'ldap:///ou=people,{DEFAULT_SUFFIX}??sub?(uid=testuser*)',
    })
    log.info(f"Created demo_group: {demo_group.dn} with memberURL: ldap:///{DEFAULT_SUFFIX}??sub?(uid=testuser*)")

    # Step 4: Add an additional memberURL attribute to demo_group pointing to the new OU
    # with the same filter as the first memberURL
    log.info("Adding second memberURL to demo_group pointing to new OU")
    second_memberurl = f'ldap:///{ou_dn}??sub?(uid=testuser*)'
    demo_group.add('memberURL', second_memberurl)
    log.info(f"Added second memberURL: {second_memberurl}")

    # Step 5: Search for the group to trigger the plugin and verify all users
    # from both locations are listed as members
    log.info("Searching for group to trigger dynamic lists plugin")
    members = demo_group.list_members()
    log.info(f"Found {len(members)} members in group")
    log.info(f"Members: {members}")

    # Verify all users from DEFAULT_SUFFIX are members
    assert len(members) == 6, f"Expected {6} members, got {len(members)}. Members: {members}"

    # Verify all users from DEFAULT_SUFFIX are members
    for user_dn in user_dns:
        assert user_dn in members, f"User {user_dn} should be a member of the group. Members: {members}"

    # Verify the user from the new OU is a member
    assert ou_user_dn in members, f"User {ou_user_dn} should be a member of the group. Members: {members}"

    # Now test that multiple seaches still return the same number of members
    members = demo_group.list_members()
    log.info(f"Found {len(members)} members in group")
    log.info(f"Members: {members}")
    assert len(members) == 6, f"Expected {6} members, got {len(members)}. Members: {members}"

    log.info("Test completed successfully - all users from both locations are listed as members")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
