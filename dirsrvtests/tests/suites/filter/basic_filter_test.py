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
from lib389.idm.domain import Domain

pytestmark = pytest.mark.tier0

class CreateUsers():
    """
    Will create users with different testUserAccountControl, testUserStatus
    """
    def __init__(self, *args):
        self.args = args

    def user_create(self):
        """
         Will create users with different testUserAccountControl, testUserStatus
        """
        import pdb
        #pdb.set_trace()
        #f"dc=syntaxes,{DEFAULT_SUFFIX}")
        self.args[0].create(rdn=f"uid={str(self.args[1])}", properties={
            'uid': str(self.args[1]),
            'sn': str(self.args[1]),
            'cn': str(self.args[1]),
            'userpassword': 'password',
            'displayName': self.args[2],
            'legalName': self.args[2],
            'objectclass': 'top nsPerson account organizationalPerson inetOrgPerson posixAccount'.split(),
            'uidNumber': str(self.args[4]),
            'gidNumber': str(self.args[4]),
            'homeDirectory': self.args[3],
            'loginShell': self.args[3]
        })

    def create_users_other(self):
        """
         Will create users with different testUserAccountControl(8388608)
        """
        self.args[0].create(properties={
            'telephoneNumber': '98989819{}'.format(self.args[1]),
            'uid': 'anuj_{}'.format(self.args[1]),
            'sn': 'testwise_{}'.format(self.args[1]),
            'cn': 'bit testwise{}'.format(self.args[1]),
            'userpassword': PW_DM,
            'givenName': 'anuj_{}'.format(self.args[1]),
            'mail': 'anuj_{}@example.com'.format(self.args[1]),
            'objectclass': 'top account posixaccount organizationalPerson '
                           'inetOrgPerson testperson'.split(),
            'testUserAccountControl': '8388608',
            'testUserStatus': 'PasswordExpired',
            'uidNumber': str(self.args[1]),
            'gidNumber': str(self.args[1]),
            'homeDirectory': '/home/' + 'testwise_{}'.format(self.args[1])
        })

    def user_create_52(self):
        """
        Will create users with different testUserAccountControl(16777216)
        """
        self.args[0].create(properties={
            'telephoneNumber': '98989819{}'.format(self.args[1]),
            'uid': 'bditwfilter52_test{}'.format(self.args[1]),
            'sn': 'bditwfilter52_test{}'.format(self.args[1]),
            'cn': 'bit bditwfilter52_test{}'.format(self.args[1]),
            'userpassword': PW_DM,
            'givenName': 'bditwfilter52_test{}'.format(self.args[1]),
            'mail': 'bditwfilter52_test{}@example.com'.format(self.args[1]),
            'objectclass': 'top account posixaccount organizationalPerson '
                           'inetOrgPerson testperson'.split(),
            'testUserAccountControl': '16777216',
            'testUserStatus': 'PasswordExpired',
            'uidNumber': str(self.args[1]),
            'gidNumber': str(self.args[1]),
            'homeDirectory': '/home/' + 'bditwfilter52_test{}'.format(self.args[1])
        })

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

def test_foo(topo):

    # Creating suffix
    #suffix = Domain(topo.standalone, DEFAULT_SUFFIX).create(properties={'dc': 'syntaxes'})
    syntaxes = Domain(topo.standalone, f"dc=syntaxes,{DEFAULT_SUFFIX}").create(properties={'dc': 'syntaxes'})

    # Creating 12 users to avoid shortcut in candidate list
    users = UserAccounts(topo.standalone, syntaxes.dn, rdn=None)

    # args 1: uid, sn, cn (all indexed)
    # args 2: displayName, legalName
    # args 3: homeDirectory,  loginShell
    # args 4: uidNumber, gidNumber
    for user in [('V-1 uid', ['name'], ['/bin/bash'], 101),
                 ('V-2 uid', ['name'], ['/bin/bash'], 102),
                 ('V-3 uid', ['name'], ['/bin/bash'], 103),
                 ('V-4 uid', ['name'], ['/bin/bash'], 104),
                 ('V-5 uid', ['name'], ['/bin/bash'], 105),
                 ('V-6 uid', ['name'], ['/bin/bash'], 106),
                 ('V-7 uid', ['name'], ['/bin/bash'], 107),
                 ('V-8 uid', ['name'], ['/bin/bash'], 108),
                 ('V-9 uid', ['name'], ['/bin/bash'], 109),
                 ('V-10 uid', ['name'], ['/bin/bash'], 110),
                 ('V-11 uid', ['name'], ['/bin/bash'], 111)]:
        CreateUsers(users, user[0], user[1], user[2], user[3]).user_create()

    FILTER=0
    BASE=1
    SCOPE=2
    COUNT=3
    #
    # uid, cn, sn, displayName, legalName are CIS
    # homeDirectory ExactIA5
    for srch_filter in [('(uid=V-1 uid)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),     # equality
                        ('(uid=V-1 uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),   # equality
                        ('(uid=v-1 uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),   # equality
                        ('(uid=V-10 uid)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),    # equality
                        ('(uid=V-10 uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),  # equality
                        ('(uid=v-10 uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),  # equality
                        ('(uid=V-1 *)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),       # initial
                        ('(uid=V-1 *)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),     # initial
                        ('(uid=v-1 *)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),     # initial
                        ('(uid=V-1*)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 3),        # initial
                        ('(uid=V-1*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3),      # initial
                        ('(uid=v-1*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3),      # initial
                        ('(uid= V-1*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3),     # initial
                        ('(uid=   V-1*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3),   # initial
                        ('(uid=   v-1*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3),   # initial
                        ('(uid=V-1  *)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),      # initial
                        ('(uid=V-1  *)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),    # initial
                        ('(uid=v-1  *)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),    # initial
                        ('(uid= V-1  *)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),   # initial
                        ('(uid= v-1  *)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),   # initial
                        ('(uid=   V-1  *)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1), # initial
                        ('(uid=   v-1  *)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1), # initial
                        ('(uid=*1  *)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 2),       # any
                        ('(uid=*1  *)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 2),     # any
                        ('(uid=*1 u*)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 2),       # any
                        ('(uid=*1 u*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 2),     # any
                        ('(uid=*1 U*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 2),     # any
                        ('(uid=*1   u*)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 2),     # any
                        ('(uid=*1   U*)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 2),     # any
                        ('(uid=V-1   u*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),  # final
                        ('(uid=V-1   u*)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),    # final
                        ('(uid=v-1   u*)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),    # final
                        ('(uid=V-1 u*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),    # final
                        ('(uid=V-1 u*)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),      # final
                        ('(uid=v-1 u*)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),      # final
                        ('(uid=   V-1*  ui*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3), # initial + any
                        ('(uid=   V-1*  ui*)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 3),   # initial + any
                        ('(uid=   V-1*ui*)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3),   # initial + any
                        ('(uid=   V-1*ui*)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 3),     # initial + any
                        ('(uid=*-1 * uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 0),    # any + final
                        ('(uid=*-1 * uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 0),    # any + final
                        ('(uid=*-1 *uid)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),       # any + final
                        ('(uid=*-1 *uid)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),       # any + final
                        ('(uid=*-1* uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3),     # any + final
                        ('(uid=*-1* uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3),     # any + final
                        ('(uid=*-1    *uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 1),  # any + final
                        ('(uid=*-1    *uid)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 1),    # any + final
                        ('(uid=*-1*   uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3),   # any + final
                        ('(uid=*-1*   uid)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 3),     # any + final
                        ('(uid=*-1*    uid)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 3),  # any + final
                        ('(uid=v-*1 u*id)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 2),    # inital + any + final
                        ('(uid=v-*1 u*id)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 2),      # inital + any + final
                        ('(uid=v-*1    u*id)', DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 2), # inital + any + final
                        ('(uid=v-*1    u*id)', syntaxes.dn, ldap.SCOPE_ONELEVEL, 2),   # inital + any + final
                        ]:
        assert len(UserAccounts(topo.standalone, srch_filter[BASE], rdn=None).filter(srch_filter[FILTER], scope=srch_filter[SCOPE])) == srch_filter[COUNT]


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
