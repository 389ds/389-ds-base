# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 RED Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

import pytest, os

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.topologies import topology_st as topo

from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.account import Accounts

pytestmark = pytest.mark.tier0

def test_search_attr(topo):
    """
    Test filter can search attributes
    :id: 9a1b0a4b-111c-4105-866d-4288f143ee07
    :setup: server
    :steps:
        1. Add test entry
        2. make search
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
    """
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for i in range(1, 5):
        user1 = user.create_test_user(uid=i)
        user1.set("mail", "AnujBorah{}@ok.com".format(i))

    # Testing filter is working for any king of attr

    user = Accounts(topo.standalone, DEFAULT_SUFFIX)

    assert len(user.filter('(mail=*)')) == 4
    assert len(user.filter('(uid=*)')) == 5

    # Testing filter is working for other filters
    assert len(user.filter("(objectclass=inetOrgPerson)")) == 4


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
