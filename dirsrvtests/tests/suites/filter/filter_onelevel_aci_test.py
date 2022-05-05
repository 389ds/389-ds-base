# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

import pytest, os, ldap

from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st

from lib389.idm.account import Anonymous
from lib389.idm.user import UserAccount, UserAccounts

pytestmark = pytest.mark.tier0

def test_search_attr(topology_st):
    """Test filter can search attributes

    :id: 99104b2d-fe12-40d7-b977-a04fa184cfac
    :setup: Standalone instance
    :steps:
        1. Add test entry
        2. Search with onelevel
    :expectedresults:
        1. Success
        2. Success
    """
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user(uid=1000)

    # Bind as anonymous
    conn = Anonymous(topology_st.standalone).bind()
    anon_users = UserAccounts(conn, DEFAULT_SUFFIX)
    # Subtree, works.
    res1 = anon_users.filter("(uid=test_user_1000)", scope=ldap.SCOPE_SUBTREE, strict=True)
    assert len(res1) == 1

    # Search with a one-level search.
    # This previously hit a case with filter optimisation in how parent id values were added.
    res2 = anon_users.filter("(uid=test_user_1000)", scope=ldap.SCOPE_ONELEVEL, strict=True)
    # We must get at least one result!
    assert len(res2) == 1

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
