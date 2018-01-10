# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap

import logging
import sys
import time

import pytest

from lib389 import DirSrv
from lib389._constants import *
from lib389.properties import *

from lib389.topologies import topology_st

from lib389.idm.user import nsUserAccounts
from lib389.idm.posixgroup import PosixGroups
from lib389.idm.group import Groups

from lib389.utils import ds_is_older
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")

REQUIRED_DNS = [
    'dc=example,dc=com',
    'ou=groups,dc=example,dc=com',
    'ou=people,dc=example,dc=com',
    'ou=services,dc=example,dc=com',
    'ou=permissions,dc=example,dc=com',
    'uid=demo_user,ou=people,dc=example,dc=com',
    'cn=demo_group,ou=groups,dc=example,dc=com',
    'cn=group_admin,ou=permissions,dc=example,dc=com',
    'cn=group_modify,ou=permissions,dc=example,dc=com',
    'cn=user_admin,ou=permissions,dc=example,dc=com',
    'cn=user_modify,ou=permissions,dc=example,dc=com',
    'cn=user_passwd_reset,ou=permissions,dc=example,dc=com',
    'cn=user_private_read,ou=permissions,dc=example,dc=com',
]

def test_install_sample_entries(topology_st):
    """Assert that our entries match."""

    entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE)

    for entry in entries:
        assert(entry.dn in REQUIRED_DNS)
        # We can make this assert the full object content, plugins and more later.

def test_install_aci(topology_st):
    """Assert our default aci's work as expected."""

    # Create some users and groups.
    users = nsUserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    groups = PosixGroups(topology_st.standalone, DEFAULT_SUFFIX)

    user_basic = users.create(properties={
        'uid': 'basic',
        'cn': 'Basic',
        'displayName': 'Basic',
        'uidNumber': '100000',
        'gidNumber': '100000',
        'homeDirectory': '/home/basic',
        'userPassword': 'password',
        'legalName': 'Super Secret PII',
    })

    user_modify = users.create(properties={
        'uid': 'modify',
        'cn': 'Modify',
        'displayName': 'Modify',
        'uidNumber': '100001',
        'gidNumber': '100001',
        'homeDirectory': '/home/modify',
        'userPassword': 'password',
        'legalName': 'Super Secret PII',
    })

    user_admin = users.create(properties={
        'uid': 'admin',
        'cn': 'Admin',
        'displayName': 'Admin',
        'uidNumber': '100002',
        'gidNumber': '100002',
        'homeDirectory': '/home/admin',
        'userPassword': 'password',
        'legalName': 'Super Secret PII',
    })

    user_pw_reset = users.create(properties={
        'uid': 'pw_reset',
        'cn': 'Password Reset',
        'displayName': 'Password Reset',
        'uidNumber': '100003',
        'gidNumber': '100003',
        'homeDirectory': '/home/pw_reset',
        'userPassword': 'password',
        'legalName': 'Super Secret PII',
    })

    # Add users to various permissions.

    permissions = Groups(topology_st.standalone, DEFAULT_SUFFIX, rdn='ou=permissions')

    g_group_admin = permissions.get('group_admin')
    g_group_modify = permissions.get('group_modify')
    g_user_admin = permissions.get('user_admin')
    g_user_modify = permissions.get('user_modify')
    g_user_pw_reset = permissions.get('user_passwd_reset')

    g_group_admin.add_member(user_admin.dn)
    g_user_admin.add_member(user_admin.dn)

    g_group_modify.add_member(user_modify.dn)
    g_user_modify.add_member(user_modify.dn)

    g_user_pw_reset.add_member(user_pw_reset.dn)

    # Bind as a user and assert what we can and can not see
    c_user_basic = user_basic.bind(password='password')
    c_user_modify = user_modify.bind(password='password')
    c_user_admin = user_admin.bind(password='password')
    c_user_pw_reset = user_pw_reset.bind(password='password')

    c_user_basic_users = nsUserAccounts(c_user_basic, DEFAULT_SUFFIX)
    c_user_pw_reset_users = nsUserAccounts(c_user_pw_reset, DEFAULT_SUFFIX)
    c_user_modify_users = nsUserAccounts(c_user_modify, DEFAULT_SUFFIX)
    c_user_admin_users = nsUserAccounts(c_user_admin, DEFAULT_SUFFIX)

    # Should be able to see users, but not their legalNames
    user_basic_view_demo_user = c_user_basic_users.get('demo_user')
    assert user_basic_view_demo_user.get_attr_val_utf8('legalName') is None
    assert user_basic_view_demo_user.get_attr_val_utf8('uid') == 'demo_user'

    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user_basic_view_demo_user.replace('description', 'change value')

    user_pw_reset_view_demo_user = c_user_pw_reset_users.get('demo_user')
    assert user_pw_reset_view_demo_user.get_attr_val_utf8('legalName') is None
    assert user_pw_reset_view_demo_user.get_attr_val_utf8('uid') == 'demo_user'

    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user_pw_reset_view_demo_user.replace('description', 'change value')

    # The user admin and modify should be able to read it.
    user_modify_view_demo_user = c_user_modify_users.get('demo_user')
    assert user_modify_view_demo_user.get_attr_val_utf8('legalName') == 'Demo User Name'
    assert user_modify_view_demo_user.get_attr_val_utf8('uid') == 'demo_user'
    user_modify_view_demo_user.replace('description', 'change value')

    user_admin_view_demo_user = c_user_admin_users.get('demo_user')
    assert user_admin_view_demo_user.get_attr_val_utf8('legalName') == 'Demo User Name'
    assert user_admin_view_demo_user.get_attr_val_utf8('uid') == 'demo_user'
    user_admin_view_demo_user.replace('description', 'change value')

    # Assert only admin can create:

    test_user_properties = {
        'uid': 'test_user',
        'cn': 'Test User',
        'displayName': 'Test User',
        'uidNumber': '100005',
        'gidNumber': '100005',
        'homeDirectory': '/home/test_user',
        'legalName': 'Super Secret PII',
    }

    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        c_user_basic_users.create(properties=test_user_properties)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        c_user_pw_reset_users.create(properties=test_user_properties)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        c_user_modify_users.create(properties=test_user_properties)

    test_user = c_user_admin_users.create(properties=test_user_properties)
    test_user.delete()

    # Assert on pw_reset can unlock/pw

    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user_basic_view_demo_user.lock()
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user_modify_view_demo_user.lock()
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user_admin_view_demo_user.lock()
    user_pw_reset_view_demo_user.lock()

    # Group test
    c_user_basic_groups = PosixGroups(c_user_basic, DEFAULT_SUFFIX)
    c_user_pw_reset_groups = PosixGroups(c_user_pw_reset, DEFAULT_SUFFIX)
    c_user_modify_groups = PosixGroups(c_user_modify, DEFAULT_SUFFIX)
    c_user_admin_groups = PosixGroups(c_user_admin, DEFAULT_SUFFIX)

    # Assert that members can be read, but only modify/admin can edit.
    user_basic_view_demo_group = c_user_basic_groups.get('demo_group')
    assert user_basic_view_demo_group.get_attr_val_utf8('cn') == 'demo_group'
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user_basic_view_demo_group.add_member(user_basic.dn)

    user_pw_reset_view_demo_group = c_user_pw_reset_groups.get('demo_group')
    assert user_pw_reset_view_demo_group.get_attr_val_utf8('cn') == 'demo_group'
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user_pw_reset_view_demo_group.add_member(user_pw_reset.dn)

    user_modify_view_demo_group = c_user_modify_groups.get('demo_group')
    assert user_modify_view_demo_group.get_attr_val_utf8('cn') == 'demo_group'
    user_modify_view_demo_group.add_member(user_modify.dn)

    user_admin_view_demo_group = c_user_admin_groups.get('demo_group')
    assert user_admin_view_demo_group.get_attr_val_utf8('cn') == 'demo_group'
    user_admin_view_demo_group.add_member(user_admin.dn)

    # Assert that only admin can add new group.
    group_properties = {
        'cn': 'test_group',
        'gidNumber': '100009'
    }

    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        c_user_basic_groups.create(properties=group_properties)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        c_user_pw_reset_groups.create(properties=group_properties)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        c_user_modify_groups.create(properties=group_properties)
    c_user_admin_groups.create(properties=group_properties)

