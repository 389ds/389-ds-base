# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 RED Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

import ldap
import pytest, os

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.topologies import topology_st as topo

from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.group import UniqueGroups
from lib389.idm.account import Accounts

pytestmark = pytest.mark.tier0

def test_search_attr(topo):
    """Test filter can search attributes

    :id: 9a1b0a4b-111c-4105-866d-4288f143ee07
    :setup: Standalone instance
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

def test_filter_assertion_not_normalized(topo, request):
    """Test ldapsearch with a filter with an assertion value
    that is not normalize

    :id: c23366aa-d134-4f95-86ee-44ad3e05480b
    :setup: Standalone instance
    :steps:
         1. Adding 20 member values to a group
         2. Checking the a search with not normalize
            assertion returns the group
    :expectedresults:
         1. This should pass
         2. This should pass
    """
    groups = UniqueGroups(topo.standalone, DEFAULT_SUFFIX)
    demo_group = groups.ensure_state(properties={'cn': 'demo_group'})

    # Adding more that 10 values to 'member' so that
    # the valueset will be sorted
    values = []
    for idx in range(0,20):
        values.append('uid=scarter%d, ou=People, dc=example,dc=com' % idx)
    demo_group.add('member', values)

    entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'member=uid=scarter0,   ou=People,dc=example,dc=com')
    assert len(entries) == 1

    def fin():
        demo_group.delete('member')

    request.addfinalizer(fin)

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
