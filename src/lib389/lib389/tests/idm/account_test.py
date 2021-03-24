# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#


import os
import pytest
import ldap

from lib389.idm.user import UserAccounts, nsUserAccounts, Account
from lib389.topologies import topology_st as topology
from lib389._constants import DEFAULT_SUFFIX

def test_account_locking(topology):
    """
    Ensure that user and group management works as expected.
    """
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

def test_account_reset_pw(topology):
    users = nsUserAccounts(topology.standalone, DEFAULT_SUFFIX)
    testuser = users.create_test_user(uid=1001)

    # Make sure they are unlocked.
    testuser.unlock()

    testuser.reset_password("test_password")

    # Assert we can bind as the new PW
    c = testuser.bind('test_password')
    c.unbind_s()


def test_account_change_pw(topology):
    # This test requires a secure connection
    topology.standalone.enable_tls()

    users = nsUserAccounts(topology.standalone, DEFAULT_SUFFIX)
    testuser = users.create_test_user(uid=1002)

    # Make sure they are unlocked.
    testuser.unlock()

    testuser.reset_password('password')
    testuser.change_password('password', "test_password")

    # Assert we can bind as the new PW
    c = testuser.bind('test_password')
    c.unbind_s()


def test_account_delete(topology):
    #This test requires a test user and account object

    users = UserAccounts(topology.standalone, DEFAULT_SUFFIX)
    users.create_test_user(uid=1003)

    account = Account(topology.standalone, f'uid=test_user_1003,ou=People,{DEFAULT_SUFFIX}')
    account.delete()
