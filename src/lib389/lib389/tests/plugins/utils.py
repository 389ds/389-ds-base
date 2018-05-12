# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016-2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

from lib389.idm.user import UserAccount
from lib389.idm.group import Group, UniqueGroup
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389._constants import DEFAULT_SUFFIX


# These globals are only used by the functions of this module.
# Each time we create a new test user/group/ou we increment the corresponding
# variable by 1, in order to ensure DN uniqueness.
test_user_id = 0
test_group_id = 0
test_ou_id = 0


def create_test_user(instance, cn=None, suffix=None):
    """
    Creates a new user for testing.

    It tries to create a user that doesn't already exist by using a different
    ID each time. However, if it is provided with an existing cn/suffix it
    will fail to create a new user and it will raise an LDAP error.

    Returns a UserAccount object.
    """
    global test_user_id

    if cn is None:
        cn = "testuser_" + str(test_user_id)
        test_user_id += 1

    if suffix is None:
        suffix = "ou=People," + DEFAULT_SUFFIX
    dn = "cn=" + cn + "," + suffix

    properties = {
        'uid': cn,
        'cn': cn,
        'sn': 'user',
        'uidNumber': str(1000+test_user_id),
        'gidNumber': '2000',
        'homeDirectory': '/home/' + cn
    }

    user = UserAccount(instance, dn)
    user.create(properties=properties)

    return user

def create_test_group(instance, cn=None, suffix=None, unique_group=False):
    """
    Creates a new group for testing.

    It tries to create a group that doesn't already exist by using a different
    ID each time. However, if it is provided with an existing cn/suffix it
    will fail to create a new group and it will raise an LDAP error.

    Returns a Group object.
    """
    global test_group_id

    if cn is None:
        cn = "testgroup_" + str(test_group_id)
        test_group_id += 1

    if suffix is None:
        suffix = "ou=Groups," + DEFAULT_SUFFIX
    dn = "cn=" + cn + "," + suffix

    properties = {
        'cn': cn,
        'ou': 'groups',
    }

    if unique_group:
        group = UniqueGroup(instance, dn)
    else:
        group = Group(instance, dn)

    group.create(properties=properties)

    return group

def create_test_ou(instance, ou=None, suffix=None):
    """
    Creates a new Organizational Unit for testing.

    It tries to create a ou that doesn't already exist by using a different
    ID each time. However, if it is provided with an existing ou/suffix it
    will fail to create a new ou and it will raise an LDAP error.

    Returns an OrganizationalUnit object.
    """
    global test_ou_id

    if ou is None:
        ou = "TestOU_" + str(test_ou_id)
        test_ou_id += 1

    if suffix is None:
        suffix = DEFAULT_SUFFIX
    dn = ou + "," + suffix
    dn = "ou=" + ou + "," + suffix

    properties = {
        'ou': ou,
    }

    ou = OrganizationalUnit(instance, dn)
    ou.create(properties=properties)

    return ou

def delete_objects(objects):
    for obj in objects:
        obj.delete()
