# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
This script will test different type of Filers.
"""

import os
import ldap
import pytest
from lib389.topologies import topology_st as topo
from lib389._constants import PW_DM
from lib389.idm.user import UserAccounts
from lib389.idm.account import Accounts
from lib389.plugins import BitwisePlugin
from lib389.schema import Schema
from lib389.backend import Backends
from lib389.idm.domain import Domain

pytestmark = pytest.mark.tier1

FILTER_TESTPERSON = "objectclass=testperson"
FILTER_TESTERPERSON = "objectclass=testerperson"
FILTER_CONTROL = f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=514))"
SUFFIX = 'dc=anuj,dc=com'


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
        self.args[0].create(properties={
            'sn': self.args[1],
            'uid': self.args[1],
            'cn': self.args[1],
            'userpassword': PW_DM,
            'givenName': 'bit',
            'mail': '{}@redhat.com'.format(self.args[1]),
            'objectclass': 'top account posixaccount organizationalPerson '
                           'inetOrgPerson testperson'.split(),
            'testUserAccountControl': [i for i in self.args[2]],
            'testUserStatus': [i for i in self.args[3]],
            'uidNumber': str(self.args[4]),
            'gidNumber': str(self.args[4]),
            'homeDirectory': self.args[1]
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


@pytest.fixture(scope="module")
def _create_schema(request, topo):
    Schema(topo.standalone).\
        add('attributetypes',
            ["( NAME 'testUserAccountControl' DESC 'Attribute Bitwise filteri-Multi-Valued'"
             "SYNTAX 1.3.6.1.4.1.1466.115.121.1.27 )",
             "( NAME 'testUserStatus' DESC 'State of User account active/disabled'"
             "SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 )"])

    Schema(topo.standalone).\
        add('objectClasses', "( NAME 'testperson' SUP top STRUCTURAL MUST "
                             "( sn $ cn $ testUserAccountControl $ "
                             "testUserStatus )MAY( userPassword $ telephoneNumber $ "
                             "seeAlso $ description ) X-ORIGIN 'BitWise' )")

    # Creating Backend
    backends = Backends(topo.standalone)
    backend = backends.create(properties={'nsslapd-suffix': SUFFIX, 'cn': 'AnujRoot'})

    # Creating suffix
    suffix = Domain(topo.standalone, SUFFIX).create(properties={'dc': 'anuj'})

    # Creating users
    users = UserAccounts(topo.standalone, suffix.dn, rdn=None)
    for user in [('btestuser1', ['514'], ['Disabled'], 100),
                 ('btestuser2', ['65536'], ['PasswordNeverExpired'], 101),
                 ('btestuser3', ['8388608'], ['PasswordExpired'], 102),
                 ('btestuser4', ['256'], ['TempDuplicateAccount'], 103),
                 ('btestuser5', ['16777216'], ['TrustedAuthDelegation'], 104),
                 ('btestuser6', ['528'], ['AccountLocked'], 105),
                 ('btestuser7', ['513'], ['AccountActive'], 106),
                 ('btestuser11', ['655236'], ['TestStatus1'], 107),
                 ('btestuser12', ['665522'], ['TestStatus2'], 108),
                 ('btestuser13', ['266552'], ['TestStatus3'], 109),
                 ('btestuser8', ['98536', '99512', '99528'],
                  ['AccountActive', 'PasswordExxpired', 'AccountLocked'], 110),
                 ('btestuser9', ['87536', '912', ], ['AccountActive',
                                                     'PasswordNeverExpired', ], 111),
                 ('btestuser10', ['89536', '97546', '96579'],
                  ['TestVerify1', 'TestVerify2', 'TestVerify3'], 112)]:
        CreateUsers(users, user[0], user[1], user[2], user[3]).user_create()

    def fin():
        """
        Deletes entries after the test.
        """
        for user in users.list():
            user.delete()

        suffix.delete()
        backend.delete()

    request.addfinalizer(fin)


def increasesizelimit(topo, size):
    """
    Will change  nsslapd-sizelimit to desire value
    """
    topo.standalone.config.set('nsslapd-sizelimit', str(size))


def test_bitwise_plugin_status(topo, _create_schema):
    """Checking bitwise plugin enabled or not, by default it should be enabled.
    If disabled, this test case would enable the plugin

    :id: 3ade097e-9ebd-11e8-b2e7-8c16451d917b
    :setup: Standalone
    :steps:
        1. Create Filter rules.
        2. Try to pass filter rules as per the condition .
    :expectedresults:
        1. It should pass
        2. It should pass
    """
    # Assert plugin BitwisePlugin is on
    assert BitwisePlugin(topo.standalone).status()


def test_search_disabled_accounts(topo, _create_schema):
    """Searching for integer Disabled Accounts.
    Bitwise AND operator should match each integer, so it should return one entry.

    :id: 467ef0ea-9ebd-11e8-a37f-8c16451d917b
    :setup: Standalone
    :steps:
        1. Create Filter rules.
        2. Try to pass filter rules as per the condition .
    :expectedresults:
        1. It should pass
        2. It should pass
    """
    assert len(Accounts(topo.standalone, SUFFIX).filter(FILTER_CONTROL)) == 2


def test_plugin_can_be_disabled(topo, _create_schema):
    """Verify whether plugin can be disabled

    :id: 4ed21588-9ebd-11e8-b862-8c16451d917b
    :setup: Standalone
    :steps:
            1. Create Filter rules.
            2. Try to pass filter rules as per the condition .
    :expectedresults:
            1. It should pass
            2. It should pass
    """
    bitwise = BitwisePlugin(topo.standalone)
    assert bitwise.status()
    # make BitwisePlugin off
    bitwise.disable()
    topo.standalone.restart()
    assert not bitwise.status()


def test_plugin_is_disabled(topo, _create_schema):
    """Testing Bitwise search when plugin is disabled
    Bitwise search filter should give proper error message

    :id: 54bebbfe-9ebd-11e8-8ca4-8c16451d917b
    :setup: Standalone
    :steps:
            1. Create Filter rules.
            2. Try to pass filter rules as per the condition .
    :expectedresults:
            1. It should pass
            2. It should pass
    """
    with pytest.raises(ldap.UNAVAILABLE_CRITICAL_EXTENSION):
        Accounts(topo.standalone, SUFFIX).filter(FILTER_CONTROL)


def test_enabling_works_fine(topo, _create_schema):
    """Enabling the plugin to make sure re-enabling works fine

    :id: 5a2fc2b8-9ebd-11e8-8e18-8c16451d917b
    :setup: Standalone
    :steps:
            1. Create Filter rules.
            2. Try to pass filter rules as per the condition .
    :expectedresults:
            1. It should pass
            2. It should pass
    """
    # make BitwisePlugin off
    bitwise = BitwisePlugin(topo.standalone)
    bitwise.disable()
    # make BitwisePlugin on again
    bitwise.enable()
    topo.standalone.restart()
    assert bitwise.status()
    assert len(Accounts(topo.standalone, SUFFIX).filter(FILTER_CONTROL)) == 2


@pytest.mark.parametrize("filter_name, value", [
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=513))", 1),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=16777216))", 1),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=8388608))", 1),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.804:=5))", 3),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.804:=8))", 3),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.804:=7))", 5),
    (f"(& ({FILTER_TESTERPERSON}) (testUserAccountControl:1.2.840.113556.1.4.804:=7))", 0),
    (f"(& ({FILTER_TESTPERSON}) (&(testUserAccountControl:1.2.840.113556.1.4.803:=98536)"
     "(testUserAccountControl:1.2.840.113556.1.4.803:=912)))", 0),
    (f"(& ({FILTER_TESTPERSON}) (&(testUserAccountControl:1.2.840.113556.1.4.804:=87)"
     "(testUserAccountControl:1.2.840.113556.1.4.804:=91)))", 8),
    (f"(& ({FILTER_TESTPERSON}) (&(testUserAccountControl:1.2.840.113556.1.4.803:=89536)"
     "(testUserAccountControl:1.2.840.113556.1.4.804:=79)))", 1),
    (f"(& ({FILTER_TESTPERSON}) (|(testUserAccountControl:1.2.840.113556.1.4.803:=89536)"
     "(testUserAccountControl:1.2.840.113556.1.4.804:=79)))", 8),
    (f"(& ({FILTER_TESTPERSON}) (|(testUserAccountControl:1.2.840.113556.1.4.803:=89)"
     "(testUserAccountControl:1.2.840.113556.1.4.803:=536)))", 0),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=x))", 13),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=&\\*#$%))", 13),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=-65536))", 0),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=-1))", 0),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=-))", 13),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=))", 13),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=\\*))", 13),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.804:=\\*))", 0),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=6552))", 0),
    (f"(& ({FILTER_TESTPERSON}\\))(testUserAccountControl:1.2.840.113556.1.4.804:=6552))", 0),
    (f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=65536))", 5)
])
def test_all_together(topo, _create_schema, filter_name, value):
    """Target_set_with_ldap_instead_of_ldap

    :id:  ba7f5106-9ebd-11e8-9ad6-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Create Filter rules.
        2. Try to pass filter rules as per the condition .
    :expectedresults:
        1. It should pass
        2. It should pass
    """
    assert len(Accounts(topo.standalone, SUFFIX).filter(filter_name)) == value


def test_5_entries(topo, _create_schema):
    """Bitwise filter test for 5 entries
    By default the size limit is 2000
    Inorder to perform stress tests, we need to icrease the nsslapd-sizelimit.
    IncrSizeLimit 52000

    :id: e939aa64-9ebd-11e8-815e-8c16451d917b
    :setup: Standalone
    :steps:
            1. Create Filter rules.
            2. Try to pass filter rules as per the condition .
    :expectedresults:
            1. It should pass
            2. It should pass
    """
    filter51 = f"(& ({FILTER_TESTPERSON}) (testUserAccountControl:1.2.840.113556.1.4.803:=8388608))"
    increasesizelimit(topo, 52000)
    users = UserAccounts(topo.standalone, SUFFIX, rdn=None)
    for i in range(5):
        CreateUsers(users, i).create_users_other()
    assert len(Accounts(topo.standalone, SUFFIX).filter(filter51)) == 6
    increasesizelimit(topo, 2000)


def test_5_entries1(topo, _create_schema):
    """Bitwise filter for 5 entries
    By default the size limit is 2000
    Inorder to perform stress tests, we need to icrease the nsslapd-sizelimit.
    IncrSizeLimit 52000

    :id: ef8b050c-9ebd-11e8-979d-8c16451d917b
    :setup: Standalone
    :steps:
            1. Create Filter rules.
            2. Try to pass filter rules as per the condition .
    :expectedresults:
            1. It should pass
            2. It should pass
    """
    filter52 = f"(& ({FILTER_TESTPERSON})(testUserAccountControl:1.2.840.113556.1.4.804:=16777216))"
    increasesizelimit(topo, 52000)
    users = UserAccounts(topo.standalone, SUFFIX, rdn=None)
    for i in range(5):
        CreateUsers(users, i).user_create_52()
    assert len(Accounts(topo.standalone, SUFFIX).filter(filter52)) == 6
    increasesizelimit(topo, 2000)


def test_5_entries3(topo, _create_schema):
    """Bitwise filter test for entries
    By default the size limit is 2000
    Inorder to perform stress tests, we need to icrease the nsslapd-sizelimit.
    IncrSizeLimit 52000

    :id: f5b06648-9ebd-11e8-b08f-8c16451d917b
    :setup: Standalone
    :steps:
            1. Create Filter rules.
            2. Try to pass filter rules as per the condition .
    :expectedresults:
            1. It should pass
            2. It should pass
    """
    increasesizelimit(topo, 52000)
    assert len(Accounts(topo.standalone, SUFFIX).filter(
        "(testUserAccountControl:1.2.840.113556.1.4.803:=8388608, "
        "['attrlist=cn:sn:uid:testUserAccountControl'])")) == 6
    increasesizelimit(topo, 2000)


def test_5_entries4(topo, _create_schema):
    """Bitwise filter for  entries
    By default the size limit is 2000
    Inorder to perform stress tests, we need to icrease the nsslapd-sizelimit.
    IncrSizeLimit 52000

    :id: fa5f7a4e-9ebd-11e8-ad54-8c16451d917b
    :setup: Standalone
    :steps:
            1. Create Filter rules.
            2. Try to pass filter rules as per the condition .
    :expectedresults:
            1. It should pass
            2. It should pass
    """
    increasesizelimit(topo, 52000)
    assert len(Accounts(topo.standalone, SUFFIX).
               filter("(testUserAccountControl:1.2.840.113556.1.4.804:=16777216,"
                      "['attrlist=cn:sn:uid:testUserAccountControl'])")) == 6
    increasesizelimit(topo, 2000)


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
