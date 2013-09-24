# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#


import os
import logging
import pytest
import ldap

from lib389.idm.user import UserAccounts
from lib389.topologies import topology_st as topology
from lib389._constants import DEFAULT_SUFFIX

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING is not False:
    DEBUGGING = True

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


def test_account_locking(topology):
    """
    Ensure that user and group management works as expected.
    """
    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    users = UserAccounts(topology.standalone, DEFAULT_SUFFIX)

    user_properties = {
        'uid': 'testuser',
        'cn' : 'testuser',
        'sn' : 'user',
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/testuser',
        'userPassword' : 'password'
    }
    testuser = users.create(properties=user_properties)

    assert(testuser.is_locked() is False)
    testuser.lock()
    assert(testuser.is_locked() is True)

    # Check a bind fails
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        conn = testuser.bind('password')
        conn.unbind_s()

    testuser.unlock()
    assert(testuser.is_locked() is False)

    # Check the bind works.
    conn = testuser.bind('password')
    conn.unbind_s()

    log.info('Test PASSED')
