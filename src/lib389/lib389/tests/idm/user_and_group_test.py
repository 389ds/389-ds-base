# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.group import Groups

from lib389.topologies import topology_st as topology

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING is not False:
    DEBUGGING = True

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


def test_users_and_groups(topology):
    """
    Ensure that user and group management works as expected.
    """
    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    users = UserAccounts(topology.standalone, DEFAULT_SUFFIX)
    groups = Groups(topology.standalone, DEFAULT_SUFFIX)

    # No user should exist

    assert(len(users.list()) == 0)

    # Create a user

    users.create(properties=TEST_USER_PROPERTIES)

    assert(len(users.list()) == 1)

    testuser = users.get('testuser')

    # Set password
    testuser.set('userPassword', 'password')
    # bind

    conn = testuser.bind('password')
    conn.unbind_s()

    # create group
    group_properties = {
        'cn' : 'group1',
        'description' : 'testgroup'
    }

    group = groups.create(properties=group_properties)

    # user shouldn't be a member
    assert(not group.is_member(testuser.dn))

    # add user as member
    group.add_member(testuser.dn)
    # check they are a member
    assert(group.is_member(testuser.dn))
    # remove user from group
    group.remove_member(testuser.dn)
    # check they are not a member
    assert(not group.is_member(testuser.dn))

    group.delete()
    testuser.delete()

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

