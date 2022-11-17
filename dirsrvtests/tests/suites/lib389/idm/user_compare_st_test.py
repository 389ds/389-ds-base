# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import pytest
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.group import Groups
from lib389.idm.user import UserAccounts, UserAccount
from lib389.topologies import topology_st as topology

pytestmark = pytest.mark.tier1

def test_user_compare(topology):
    """
    Testing compare function

    :id: 26f2dea9-be1e-48ca-bcea-79592823390c

    :setup: Standalone instance

    :steps:
        1. Testing comparison of two different users.
        2. Testing comparison of 'str' object with itself.
        3. Testing comparison of user with similar user (different object id).
        4. Testing comparison of user with group.

    :expectedresults:
        1. Should fail to compare
        2. Should raise value error
        3. Should be the same despite uuid difference
        4. Should fail to compare
    """
    users = UserAccounts(topology.standalone, DEFAULT_SUFFIX)
    groups = Groups(topology.standalone, DEFAULT_SUFFIX)
    # Create 1st user
    user1_properties = {
        'uid': 'testuser1',
        'cn': 'testuser1',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/testuser1'
    }

    users.create(properties=user1_properties)
    testuser1 = users.get('testuser1')
    # Create 2nd user
    user2_properties = {
        'uid': 'testuser2',
        'cn': 'testuser2',
        'sn': 'user',
        'uidNumber': '1001',
        'gidNumber': '2002',
        'homeDirectory': '/home/testuser2'
    }

    users.create(properties=user2_properties)
    testuser2 = users.get('testuser2')
    # create group
    group_properties = {
        'cn' : 'group1',
        'description' : 'testgroup'
    }

    testuser1_copy = users.get("testuser1")
    group = groups.create(properties=group_properties)

    assert UserAccount.compare(testuser1, testuser2) is False

    with pytest.raises(ValueError):
        UserAccount.compare("test_str_object","test_str_object")

    assert UserAccount.compare(testuser1, testuser1_copy)
    assert UserAccount.compare(testuser1, group) is False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
