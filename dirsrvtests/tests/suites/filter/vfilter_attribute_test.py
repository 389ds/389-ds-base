# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
This script will test different type of Filters.
"""

import os
import pytest

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.schema import Schema
from lib389.idm.account import Accounts

pytestmark = pytest.mark.tier1

FILTER_COMBINE = f"(& (| (nsRoleDN=cn=new managed role) (sn=Hall)) (l=sunnyvale))"
FILTER_RJ = "(uid=rjense2)"
FILTER_CN = "(nsRoleDN=cn=new managed *)"
FILTER_CN_MT = f"(& {FILTER_CN} (uid=mtyler))"

VALUES_POSITIVE = [
    (FILTER_COMBINE, ['*', 'cn'], 'cn'),
    (FILTER_COMBINE, ['cn', 'cn', 'cn'], 'cn'),
    (FILTER_COMBINE, ['cn', 'Cn', 'CN'], 'cn'),
    (FILTER_COMBINE, ['cn', '*'], 'cn'),
    (FILTER_COMBINE, ['modifiersName', 'modifyTimestamp'], 'modifiersName'),
    (FILTER_COMBINE, ['modifiersName', 'modifyTimestamp'], 'modifyTimestamp'),
    (FILTER_COMBINE, ['*', 'modifiersName', 'modifyTimestamp'], 'modifiersName'),
    (FILTER_COMBINE, ['*', 'modifiersName', 'modifyTimestamp'], 'modifyTimestamp'),
    (FILTER_COMBINE, ['cn', 'modifiersName', 'modifyTimestamp'], 'modifiersName'),
    (FILTER_COMBINE, ['cn', 'modifiersName', 'modifyTimestamp'], 'modifyTimestamp'),
    (FILTER_COMBINE, ['cn', 'modifiersName', 'modifyTimestamp'], 'cn'),
    (FILTER_COMBINE, ['cn', 'modifiersName', 'nsRoleDN'], 'cn'),
    (FILTER_COMBINE, ['cn', 'modifiersName', 'nsRoleDN'], 'modifiersName'),
    (FILTER_COMBINE, ['cn', 'modifiersName', 'nsRoleDN'], 'nsRoleDN'),
    (FILTER_COMBINE, ['cn', '*', 'modifiersName', 'nsRoleDN'], 'cn'),
    (FILTER_COMBINE, ['cn', '*', 'modifiersName', 'nsRoleDN'], 'modifiersName'),
    (FILTER_COMBINE, ['cn', '*', 'modifiersName', 'nsRoleDN'], 'nsRoleDN'),
    (FILTER_RJ, ['*', 'mailquota'], 'mailquota'),
    (FILTER_RJ, ['mailquota', '*'], 'mailquota'),
    (FILTER_RJ, ['mailquota'], 'mailquota'),
    (FILTER_RJ, ['mailquota', 'nsRoleDN'], 'mailquota'),
    (FILTER_RJ, ['mailquota', 'nsRoleDN'], 'nsRoleDN'),
    (FILTER_CN, ['cn', 'nsRoleDN'], 'cn'),
    (FILTER_CN, ['cn', 'nsRoleDN'], 'nsRoleDN'),
    (FILTER_CN_MT, ['mailquota', 'nsRoleDN'], 'mailquota'),
    (FILTER_CN_MT, ['mailquota', 'nsRoleDN'], 'nsRoleDN'),
    (FILTER_CN_MT, ['mailquota', 'modifiersName', 'nsRoleDN'], 'mailquota'),
    (FILTER_CN_MT, ['mailquota', 'modifiersName', 'nsRoleDN'], 'modifiersName'),
    (FILTER_CN_MT, ['mailquota', 'modifiersName', 'nsRoleDN'], 'nsRoleDN'),
    (FILTER_CN_MT, ['*', 'modifiersName', 'nsRoleDN'], 'nsRoleDN'),
    (FILTER_CN_MT, ['*', 'modifiersName', 'nsRoleDN'], 'modifiersName')]


LIST_OF_USER = ['scarter', 'tmorris', 'kvaughan', 'abergin', 'dmiller',
                'gfarmer', 'kwinters', 'trigden', 'cschmith', 'jwallace',
                'jwalker', 'tclow', 'rdaugherty', 'jreuter', 'tmason',
                'btalbot', 'mward', 'bjablons', 'jmcFarla', 'llabonte',
                'jcampaig', 'bhal2', 'alutz', 'achassin', 'hmiller',
                'jcampai2', 'lulrich', 'mlangdon', 'striplet',
                'gtriplet', 'jfalena', 'speterso', 'ejohnson',
                'prigden', 'bwalker', 'kjensen', 'mlott',
                'cwallace', 'tpierce', 'rbannist', 'bplante',
                'rmills', 'bschneid', 'skellehe', 'brentz',
                'dsmith', 'scarte2', 'dthorud', 'ekohler',
                'lcampbel', 'tlabonte', 'slee', 'bfree',
                'tschneid', 'prose', 'jhunter', 'ashelton',
                'mmcinnis', 'falbers', 'mschneid', 'pcruse',
                'tkelly', 'gtyler']


@pytest.fixture(scope="module")
def _create_test_entries(topo):
    """
    :param topo:
    :return: Will create users used for this test script .
    """
    users_people = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for demo1 in LIST_OF_USER:
        users_people.create(properties={
            'uid': demo1,
            'cn': demo1,
            'sn': demo1,
            'uidNumber': str(1000),
            'gidNumber': '2000',
            'homeDirectory': '/home/' + demo1,
            'givenname': demo1,
            'userpassword': PW_DM
        })

    users_people.create(properties={
        'uid': 'bhall',
        'cn': 'Benjamin Hall',
        'sn': 'Hall',
        'uidNumber': str(1000),
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'bhall',
        'mail': 'bhall@anuj.com',
        'givenname': 'Benjamin',
        'ou': ['Product Development', 'People'],
        'l': 'sunnyvale',
        'telephonenumber': '+1 408 555 6067',
        'roomnumber': '2511',
        'manager': 'uid=trigden, ou=People, dc=example, dc=com',
        'nsRoleDN': 'cn=new managed role, ou=People, dc=example, dc=com',
        'userpassword': PW_DM,
    })

    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ou_ou = ous.create(properties={'ou': 'COS'})

    ous = OrganizationalUnits(topo.standalone, ou_ou.dn)
    ous.create(properties={'ou': 'MailSchemeClasses'})

    Schema(topo.standalone).\
        add('attributetypes', "( 9.9.8.4 NAME 'emailclass' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 "
                              "X-ORIGIN 'RFC 2256' )")
    Schema(topo.standalone).\
        add('objectclasses', "( 9.9.8.2 NAME 'mailSchemeUser' DESC "
                             "'User Defined ObjectClass' SUP 'top' MUST "
                             "( objectclass )  MAY (aci $ emailclass) X-ORIGIN 'RFC 2256' )")

    users_people.create(properties={
        'cn': 'Randy Jensen',
        'sn': 'Jensen',
        'givenname': 'Randy',
        'objectclass': 'top account person organizationalPerson inetOrgPerson mailSchemeUser '
                       'mailRecipient posixaccount'.split(),
        'l': 'sunnyvale',
        'uid': 'rjense2',
        'uidNumber': str(1000),
        'gidNumber': str(1000),
        'homeDirectory': '/home/' + 'rjense2',
        'mail': 'rjense2@example.com',
        'telephonenumber': '+1 408 555 9045',
        'roomnumber': '1984',
        'manager': 'uid=jwalker, ou=People, dc=example,dc=com',
        'nsRoleDN': 'cn=new managed role, ou=People, dc=example, dc=com',
        'emailclass': 'vpemail',
        'mailquota': '600',
        'userpassword': PW_DM,
    })

    users_people.create(properties={
        'cn': 'Bjorn Talbot',
        'sn': 'Talbot',
        'givenname': 'Bjorn',
        'objectclass': 'top account person organizationalPerson inetOrgPerson posixaccount'.split(),
        'ou': ['Product Development', 'People'],
        'l': 'Santa Clara',
        'uid': 'btalbo2',
        'mail': 'btalbo2@example.com',
        'telephonenumber': '+1 408 555 4234',
        'roomnumber': '1205',
        'uidNumber': str(1000),
        'gidNumber': str(1000),
        'homeDirectory': '/home/' + 'btalbo2',
        'manager': 'uid=trigden, ou=People, dc=example,dc=com',
        'nsRoleDN': 'cn=new managed role, ou=People, dc=example, dc=com',
        'userpassword': PW_DM
    })

    users_people.create(properties={
        'objectclass': 'top '
                       'account '
                       'person '
                       'organizationalPerson '
                       'inetOrgPerson '
                       'mailRecipient '
                       'mailSchemeUser '
                       'posixaccount'.split(),
        'cn': 'Matthew Tyler',
        'sn': 'Tyler',
        'givenname': 'Matthew',
        'ou': ['Human Resources', 'People'],
        'l': 'Cupertino',
        'uid': 'mtyler',
        'mail': 'mtyler@example.com',
        'telephonenumber': '+1 408 555 7907',
        'roomnumber': '2701',
        'uidNumber': str(1000),
        'gidNumber': str(1000),
        'homeDirectory': '/home/' + 'mtyler',
        'manager': 'uid=jwalker, ou=People, dc=example,dc=com',
        'nsRoleDN': 'cn=new managed role, ou=People, dc=example, dc=com',
        'mailquota': '600',
        'userpassword': PW_DM})


@pytest.mark.parametrize("filter_test, condition, filter_out", VALUES_POSITIVE)
def test_all_together_positive(topo, _create_test_entries, filter_test, condition, filter_out):
    """Test filter with positive results.

        :id: 51924a38-9baa-11e8-b22a-8c16451d917b
        :parametrized: yes
        :setup: Standalone Server
        :steps:
            1. Create Filter rules.
            2. Try to pass filter rules as per the condition .
        :expectedresults:
            1. It should pass
            2. It should pass
        """
    account = Accounts(topo.standalone, DEFAULT_SUFFIX)
    assert account.filter(filter_test)[0].get_attrs_vals_utf8(condition)[filter_out]


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
