# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import pytest
from lib389.idm.user import UserAccounts, Account
from lib389.topologies import topology_st as topo
from lib389._constants import DEFAULT_SUFFIX


def test_account_delete(topo):
    """
    Test that delete function is working with Accounts/Account

    :id: 9b036f14-5144-4862-b18c-a6d91b7a1620

    :setup: Standalone instance

    :steps:
        1. Create a test user.
        2. Delete the test user using Account class object.

    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
    """
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    users.create_test_user(uid=1001)
    account = Account(topo.standalone, f'uid=test_user_1001,ou=People,{DEFAULT_SUFFIX}')
    account.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
