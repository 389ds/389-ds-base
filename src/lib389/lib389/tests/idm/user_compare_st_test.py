import os
import sys
import time
import ldap
import logging
import pytest
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

from lib389.idm.group import Groups
from lib389.idm.user import UserAccounts, UserAccount

from lib389.topologies import topology_st as topology

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING is not False:
    DEBUGGING = True

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


def test_user_compare(topology):
    """
    Testing compare function
    1. Testing comparison of two different users.
    2. Testing comparison of 'str' object with itself, should raise 'ValueError'.
    3. Testing comparison of user with similar user (different object id).
    4. Testing comparison of user with group.
    """
    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

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

    assert(UserAccount.compare(testuser1, testuser2) == False)

    with pytest.raises(ValueError):
        UserAccount.compare("test_str_object","test_str_object")

    assert(UserAccount.compare(testuser1, testuser1_copy) == True)
    assert(UserAccount.compare(testuser1, group) == False)

    log.info("Test PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
