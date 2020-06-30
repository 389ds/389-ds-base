# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


"""
verify and testing  indexing Filter from a search
"""

import os
import pytest

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts
from lib389.idm.account import Accounts
from lib389.cos import CosTemplates
from lib389.schema import Schema

pytestmark = pytest.mark.tier1


FILTERS = ["(|(|(ou=nothing1)(ou=people))(|(ou=nothing2)(ou=nothing3)))",
           "(|(|(ou=people)(ou=nothing1))(|(ou=nothing2)(ou=nothing3)))",
           "(|(|(ou=nothing1)(ou=nothing2))(|(ou=people)(ou=nothing3)))",
           "(|(|(ou=nothing1)(ou=nothing2))(|(ou=nothing3)(ou=people)))",
           "(&(sn<=0000000000000000)(givenname>=FFFFFFFFFFFFFFFF))",
           "(&(sn>=0000000000000000)(sn<=1111111111111111))",
           "(&(sn>=0000000000000000)(givenname<=FFFFFFFFFFFFFFFF))"]

INDEXES = ["(uidNumber=18446744073709551617)",
           "(gidNumber=18446744073709551617)",
           "(MYINTATTR=18446744073709551617)",
           "(&(uidNumber=*)(!(uidNumber=18446744073709551617)))",
           "(&(gidNumber=*)(!(gidNumber=18446744073709551617)))",
           "(&(uidNumber=*)(!(gidNumber=18446744073709551617)))",
           "(&(myintattr=*)(!(myintattr=18446744073709551617)))",
           "(uidNumber>=-18446744073709551617)",
           "(gidNumber>=-18446744073709551617)",
           "(uidNumber<=18446744073709551617)",
           "(gidNumber<=18446744073709551617)",
           "(myintattr<=18446744073709551617)"]


INDEXES_FALSE = ["(gidNumber=54321)",
                 "(uidNumber=54321)",
                 "(myintattr=54321)",
                 "(gidNumber<=-999999999999999999999999999999)",
                 "(uidNumber<=-999999999999999999999999999999)",
                 "(myintattr<=-999999999999999999999999999999)",
                 "(gidNumber>=999999999999999999999999999999)",
                 "(uidNumber>=999999999999999999999999999999)",
                 "(myintattr>=999999999999999999999999999999)"]


@pytest.fixture(scope="module")
def _create_entries(topo):
    """
    Will create necessary users for this script.
    """
    # Creating Users
    users_people = UserAccounts(topo.standalone, DEFAULT_SUFFIX)

    for count in range(3):
        users_people.create(properties={
            'ou': ['Accounting', 'People'],
            'cn': f'User {count}F',
            'sn': f'{count}' * 16,
            'givenname': 'FFFFFFFFFFFFFFFF',
            'uid': f'user{count}F',
            'mail': f'user{count}F@test.com',
            'manager': f'uid=user{count}F,ou=People,{DEFAULT_SUFFIX}',
            'userpassword': PW_DM,
            'homeDirectory': '/home/' + f'user{count}F',
            'uidNumber': '1000',
            'gidNumber': '2000',
        })

    cos = CosTemplates(topo.standalone, DEFAULT_SUFFIX, rdn='ou=People')
    for user, number, des in [('a', '18446744073709551617', '2^64+1'),
                              ('b', '18446744073709551618', '2^64+1'),
                              ('c', '-18446744073709551617', '-2^64+1'),
                              ('d', '-18446744073709551618', '-2^64+1'),
                              ('e', '0', '0'),
                              ('f', '2', '2'),
                              ('g', '-2', '-2')]:
        cos.create(properties={
            'cn': user,
            'uidnumber': number,
            'gidnumber': number,
            'myintattr': number,
            'description': f'uidnumber value {des} - gidnumber is same but not indexed'
        })


@pytest.mark.parametrize("real_value", FILTERS)
def test_positive(topo, _create_entries, real_value):
    """Test positive filters

    :id: 57243326-91ae-11e9-aca3-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Try to pass filter rules as per the condition .
    :expected results:
        1. Pass
    """
    assert Accounts(topo.standalone, DEFAULT_SUFFIX).filter(real_value)


def test_indexing_schema(topo, _create_entries):
    """Test with schema

    :id: 67a2179a-91ae-11e9-9a33-8c16451d917b
    :setup: Standalone
    :steps:
        1. Add attribute types to Schema.
        2. Try to pass filter rules as per the condition .
    :expected results:
        1. Pass
        2. Pass
    """
    cos = CosTemplates(topo.standalone, DEFAULT_SUFFIX, rdn='ou=People')
    Schema(topo.standalone).add('attributetypes',
                                "( 8.9.10.11.12.13.14.15 NAME 'myintattr' DESC 'for integer "
                                "syntax index ordering testing' EQUALITY integerMatch ORDERING "
                                "integerOrderingMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.27 )")
    topo.standalone.restart()
    assert cos.filter("(myintattr>=-18446744073709551617)")


@pytest.mark.parametrize("real_value", INDEXES)
def test_indexing(topo, _create_entries, real_value):
    """Test positive index filters

    :id: 7337589a-91ae-11e9-ad44-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Try to pass filter rules as per the condition .
    :expected results:
        1. Pass
    """
    cos = CosTemplates(topo.standalone, DEFAULT_SUFFIX, rdn='ou=People')
    assert cos.filter(real_value)


@pytest.mark.parametrize("real_value", INDEXES_FALSE)
def test_indexing_negative(topo, _create_entries, real_value):
    """Test negative index filters

    :id: 7e19deae-91ae-11e9-900c-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Try to pass negative filter rules as per the condition .
    :expected results:
        1. Fail
    """
    cos = CosTemplates(topo.standalone, DEFAULT_SUFFIX, rdn='ou=People')
    assert not cos.filter(real_value)


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
