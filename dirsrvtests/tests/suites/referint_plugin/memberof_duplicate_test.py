# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import logging
from lib389._constants import DEFAULT_SUFFIX
from test389.topologies import topology_m2
from lib389.plugins import MemberOfPlugin, ReferentialIntegrityPlugin
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.replica import ReplicationManager, Changelog5
from lib389._mapped_object import DSLdapObjects

log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier2


def _enable_plugins(inst):
    """Enable MemberOf and ReferentialIntegrity plugins"""
    log.info(f"Enabling plugins on {inst.serverid}")
    
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.set('memberOfAllBackends', 'on')
    
    referint = ReferentialIntegrityPlugin(inst)
    referint.enable()
    referint.set('referint-membership-attr', ['member', 'uniqueMember', 'memberOf'])
    
    inst.restart()
    log.info(f"Plugins enabled and {inst.serverid} restarted")


def test_memberof_no_duplicate_on_delete(topology_m2):
    """Test that deleting a user doesn't cause duplicate memberOf operations
    
    :id: 8f9a2b3c-4d5e-6f7a-8b9c-0d1e2f3a4b5c
    :setup: Two supplier replication topology
    :steps:
        1. Enable MemberOf and ReferentialIntegrity plugins on both suppliers
        2. Create multiple groups on supplier1
        3. Create a user on supplier1
        4. Add user to all groups
        5. Wait for replication to supplier2
        6. Delete the user on supplier1
        7. Wait for replication
        8. Verify user is removed from all groups
        9. Check replication changelog for duplicate operations
    :expectedresults:
        1. Plugins enabled successfully
        2. Groups created
        3. User created
        4. User is member of all groups
        5. Changes replicated
        6. User deleted
        7. Changes replicated
        8. User removed from all groups on both suppliers
        9. No duplicate member remove operations in changelog
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    
    log.info("Enable plugins on both suppliers")
    _enable_plugins(supplier1)
    _enable_plugins(supplier2)
    
    log.info("Create test groups")
    groups_s1 = Groups(supplier1, DEFAULT_SUFFIX)
    group_list = []
    group_names = []
    for idx in range(3):
        group_name = f'testgroup{idx}'
        log.info(f"Creating group: {group_name}")
        group = groups_s1.create(properties={'cn': group_name})
        group_list.append(group)
        group_names.append(group_name)
    
    log.info("Create test user")
    users_s1 = UserAccounts(supplier1, DEFAULT_SUFFIX)
    user = users_s1.create(properties={
        'uid': 'testuser',
        'cn': 'Test User',
        'sn': 'User',
        'uidNumber': '1000',
        'gidNumber': '1000',
        'homeDirectory': '/home/testuser'
    })
    log.info(f"Created user: {user.dn}")
    
    log.info("Add user to all groups")
    for group in group_list:
        group.add('member', user.dn)
        log.info(f"Added {user.dn} to {group.dn}")
    
    log.info("Wait for replication to supplier2")
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(supplier1, supplier2, timeout=30)
    
    log.info("Verify user exists on supplier2 and is member of groups")
    users_s2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    user_s2 = users_s2.get('testuser')
    assert user_s2.exists()
    
    groups_s2 = Groups(supplier2, DEFAULT_SUFFIX)
    for group_name in group_names:
        group_s2 = groups_s2.get(group_name)
        members = group_s2.get_attr_vals_utf8('member')
        assert user.dn in members
        log.info(f"Verified {user.dn} in {group_s2.dn} on supplier2")
    
    log.info("Delete user on supplier1")
    user.delete()
    log.info(f"Deleted user: {user.dn}")
    
    log.info("Wait for replication")
    repl.wait_for_replication(supplier1, supplier2, timeout=30)
    
    log.info("Verify user removed from all groups on both suppliers")
    for group_name in group_names:
        group_s1 = groups_s1.get(group_name)
        members_s1 = group_s1.get_attr_vals_utf8('member')
        assert user.dn not in members_s1
        log.info(f"Verified {user.dn} removed from {group_s1.dn} on supplier1")
        
        group_s2 = groups_s2.get(group_name)
        members_s2 = group_s2.get_attr_vals_utf8('member')
        assert user.dn not in members_s2
        log.info(f"Verified {user.dn} removed from {group_s2.dn} on supplier2")
    
    log.info("Verify no user entry exists on supplier2")
    assert not user_s2.exists()
    
    log.info("Check changelog for duplicate modifications")
    for group in group_list:
        log.info(f"Checking changelog entries for {group.dn}")
        
        cl1 = Changelog5(supplier1)
        cl_entries_s1 = DSLdapObjects(supplier1, basedn=cl1.dn)
        cl_list_s1 = cl_entries_s1.filter(f'(&(targetdn={group.dn})(changeType=modify))')
        
        delete_count_s1 = 0
        for entry in cl_list_s1:
            if entry.present('changes'):
                changes_str = str(entry.get_attr_vals_utf8('changes'))
                if 'delete: member' in changes_str and user.dn in changes_str:
                    delete_count_s1 += 1
        
        log.info(f"Found {delete_count_s1} member delete operations for {group.dn} on supplier1")
        assert delete_count_s1 == 0, f"Unexpected member delete in changelog on supplier1 for {group.dn}"
        
        cl2 = Changelog5(supplier2)
        cl_entries_s2 = DSLdapObjects(supplier2, basedn=cl2.dn)
        cl_list_s2 = cl_entries_s2.filter(f'(&(targetdn={group.dn})(changeType=modify))')
        
        delete_count_s2 = 0
        for entry in cl_list_s2:
            if entry.present('changes'):
                changes_str = str(entry.get_attr_vals_utf8('changes'))
                if 'delete: member' in changes_str and user.dn in changes_str:
                    delete_count_s2 += 1
        
        log.info(f"Found {delete_count_s2} member delete operations for {group.dn} on supplier2")
        assert delete_count_s2 == 0, f"Unexpected member delete in changelog on supplier2 for {group.dn}"
    
    log.info("Test passed: No duplicate member delete operations found in changelog")


def test_memberof_no_duplicate_on_rename(topology_m2):
    """Test that renaming a user doesn't cause duplicate memberOf operations
    
    :id: 9a0b1c2d-3e4f-5a6b-7c8d-9e0f1a2b3c4d
    :setup: Two supplier replication topology
    :steps:
        1. Enable MemberOf and ReferentialIntegrity plugins on both suppliers
        2. Create multiple groups on supplier1
        3. Create a user on supplier1
        4. Add user to all groups
        5. Wait for replication to supplier2
        6. Rename the user on supplier1
        7. Wait for replication
        8. Verify old DN removed and new DN added to all groups
        9. Check for duplicate operations in changelog
    :expectedresults:
        1. Plugins enabled successfully
        2. Groups created
        3. User created
        4. User is member of all groups
        5. Changes replicated
        6. User renamed
        7. Changes replicated
        8. Old DN removed, new DN added to groups on both suppliers
        9. No duplicate member operations in changelog
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    
    log.info("Enable plugins on both suppliers")
    _enable_plugins(supplier1)
    _enable_plugins(supplier2)
    
    log.info("Create test groups")
    groups_s1 = Groups(supplier1, DEFAULT_SUFFIX)
    group_list = []
    group_names = []
    for idx in range(3):
        group_name = f'renamegroup{idx}'
        log.info(f"Creating group: {group_name}")
        group = groups_s1.create(properties={'cn': group_name})
        group_list.append(group)
        group_names.append(group_name)
    
    log.info("Create test user")
    users_s1 = UserAccounts(supplier1, DEFAULT_SUFFIX)
    user = users_s1.create(properties={
        'uid': 'renameuser',
        'cn': 'Rename User',
        'sn': 'User',
        'uidNumber': '2000',
        'gidNumber': '2000',
        'homeDirectory': '/home/renameuser'
    })
    old_dn = user.dn
    log.info(f"Created user: {old_dn}")
    
    log.info("Add user to all groups")
    for group in group_list:
        group.add('member', old_dn)
        log.info(f"Added {old_dn} to {group.dn}")
    
    log.info("Wait for replication to supplier2")
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(supplier1, supplier2, timeout=30)
    
    log.info("Verify user exists on supplier2 with old DN")
    users_s2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    user_s2 = users_s2.get('renameuser')
    assert user_s2.exists()
    
    log.info("Rename user on supplier1")
    user.rename('uid=renameuser_new')
    new_dn = user.dn
    log.info(f"Renamed user from {old_dn} to {new_dn}")
    
    log.info("Wait for replication")
    repl.wait_for_replication(supplier1, supplier2, timeout=30)
    
    log.info("Verify old DN removed and new DN added to groups")
    for group_name in group_names:
        group_s1 = groups_s1.get(group_name)
        members_s1 = group_s1.get_attr_vals_utf8('member')
        members_s1_lower = [m.lower() for m in members_s1]
        assert old_dn.lower() not in members_s1_lower, f"Old DN still in {group_s1.dn}"
        assert new_dn.lower() in members_s1_lower, f"New DN not in {group_s1.dn}"
        log.info(f"Verified rename in {group_s1.dn} on supplier1")
        
        group_s2 = Groups(supplier2, DEFAULT_SUFFIX).get(group_name)
        members_s2 = group_s2.get_attr_vals_utf8('member')
        members_s2_lower = [m.lower() for m in members_s2]
        assert old_dn.lower() not in members_s2_lower, f"Old DN still in {group_s2.dn} on supplier2"
        assert new_dn.lower() in members_s2_lower, f"New DN not in {group_s2.dn} on supplier2"
        log.info(f"Verified rename in {group_s2.dn} on supplier2")
    
    log.info("Verify renamed user exists on supplier2")
    user_s2_new = users_s2.get('renameuser_new')
    assert user_s2_new.exists()
    
    log.info("Check changelog for duplicate modifications")
    for group in group_list:
        log.info(f"Checking changelog entries for {group.dn}")
        
        cl1 = Changelog5(supplier1)
        cl_entries_s1 = DSLdapObjects(supplier1, basedn=cl1.dn)
        cl_list_s1 = cl_entries_s1.filter(f'(&(targetdn={group.dn})(changeType=modify))')
        
        change_count_s1 = 0
        for entry in cl_list_s1:
            if entry.present('changes'):
                changes_str = str(entry.get_attr_vals_utf8('changes'))
                if ('delete: member' in changes_str and old_dn in changes_str) or \
                   ('add: member' in changes_str and new_dn in changes_str):
                    change_count_s1 += 1
        
        log.info(f"Found {change_count_s1} member change operations for {group.dn} on supplier1")
        assert change_count_s1 == 0, f"Unexpected member changes in changelog on supplier1 for {group.dn}"
        
        cl2 = Changelog5(supplier2)
        cl_entries_s2 = DSLdapObjects(supplier2, basedn=cl2.dn)
        cl_list_s2 = cl_entries_s2.filter(f'(&(targetdn={group.dn})(changeType=modify))')
        
        change_count_s2 = 0
        for entry in cl_list_s2:
            if entry.present('changes'):
                changes_str = str(entry.get_attr_vals_utf8('changes'))
                if ('delete: member' in changes_str and old_dn in changes_str) or \
                   ('add: member' in changes_str and new_dn in changes_str):
                    change_count_s2 += 1
        
        log.info(f"Found {change_count_s2} member change operations for {group.dn} on supplier2")
        assert change_count_s2 == 0, f"Unexpected member changes in changelog on supplier2 for {group.dn}"
    
    log.info("Test passed: No duplicate member change operations found in changelog")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
