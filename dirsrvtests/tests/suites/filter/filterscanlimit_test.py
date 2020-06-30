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

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.index import Index
from lib389.idm.account import Accounts
from lib389.idm.group import UniqueGroups
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1

GIVEN_NAME = 'cn=givenname,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
CN_NAME = 'cn=sn,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
UNIQMEMBER = 'cn=uniquemember,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
OBJECTCLASS = 'cn=objectclass,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
MAIL = 'cn=mail,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
ACLG_OU = f'ou=ACLGroup,{DEFAULT_SUFFIX}'
NESG_OU = f'ou=nestedgroup, {DEFAULT_SUFFIX}'

LIST_OF_USER_ACCOUNTING = [
    "Ted Morris",
    "David Miller",
    "Gern Farmer",
    "Judy Wallace",
    "Marcus Ward",
    "Judy McFarland",
    "Gern Triplett",
    "Emanuel Johnson",
    "Brad Walker",
    "Tobias Pierce",
    "Randy Mills",
    "David Thorud",
    "Elba Kohler",
    "Laurel Campbell",
    "Torrey Schneider",
    "Paula Rose",
    "Frank Albers",
    "Martin Schneider",
    "Andrew Hel",
    "Pete Tyler",
    "Randy Ulrich",
    "Richard Francis",
    "Morgan White",
    "Jody Jensen",
    "Mike Carter",
    "Gern Tyler",
    "Bjorn Jensen",
    "Andy Hall",
    "Ted Jensen",
    "Wendy Lutz",
    "Kelly Mcinnis",
    "Trent Couzens",
    "Dan Lanoway",
    "Richard Jensen"]

LIST_OF_USER_HUMAN = [
    "Kirsten Vaughan",
    "Chris Schmith",
    "Torrey Clow",
    "Robert Daugherty",
    "Torrey Mason",
    "Brad Talbot",
    "Jeffrey Campaigne",
    "Stephen Triplett",
    "John Falena",
    "Peter Rigden",
    "Mike Lott",
    "Richard Bannister",
    "Brian Plante",
    "Daniel Smith",
    "Tim Labonte",
    "Scott Lee",
    "Bjorn Free",
    "Alexander Shelton",
    "James Burrell",
    "Karen Carter",
    "Randy Fish",
    "Philip Hunt",
    "Rachel Schneider",
    "Gern Jensen",
    "David Akers",
    "Tobias Ward",
    "Jody Rentz",
    "Peter Lorig",
    "Kelly Schmith",
    "Pete Worrell",
    "Matthew Reuter",
    "Tobias Schmith",
    "Jon Goldstein",
    "Janet Lutz",
    "Karl Cope"]

LIST_OF_USER_TESTING = [
    "Andy Bergin",
    "John Walker",
    "Jayne Reuter",
    "Lee Ulrich",
    "Benjamin Schneider",
    "Bertram Rentz",
    "Patricia Cruse",
    "Jim Lange",
    "Alan White",
    "Daniel Ward",
    "Lee Stockton",
    "Matthew Vaughan"]

LIST_OF_USER_DEVELOPMENT = [
    "Kelly Winters",
    "Torrey Rigden",
    "Benjamin Hall",
    "Lee Labonte",
    "Jody Campaigne",
    "Alexander Lutz",
    "Bjorn Talbot",
    "Marcus Langdon",
    "Sue Peterson",
    "Kurt Jensen",
    "Cecil Wallace",
    "Stephen Carter",
    "Janet Hunter",
    "Marcus Mcinnis",
    "Timothy Kelly",
    "Sue Mason",
    "Chris Alexander",
    "Martin Talbot",
    "Scott Farmer",
    "Allison Jensen",
    "Jeff Muffly",
    "Alan Worrell",
    "Dan Langdon",
    "Ashley Knutson",
    "Jon Bourke",
    "Pete Hunt"]

LIST_OF_USER_PAYROLL = [
    "Ashley Chassin",
    "Sue Kelleher",
    "Jim Cruse",
    "Judy Brown",
    "Patricia Shelton",
    "Dietrich Swain",
    "Allison Hunter",
    "Anne-Louise Barnes"]

LIST_OF_USER_PEOPLE = [
    'Sam Carter',
    'Tom Morris',
    'Kevin Vaughan',
    'Rich Daugherty',
    'Harry Miller',
    'Sam Schmith']

@pytest.mark.xfail(ds_is_older('1.4.4'), reason="https://pagure.io/389-ds-base/issue/50201")
def test_invalid_configuration(topo):
    """"Error handling for invalid configuration
    Starting...test cases for bug1011539
    Index config error handling does not exist - you can add any old thing

    :id: 377950f6-9f06-11e8-831b-8c16451d917b
    :setup: Standalone instance
    :steps:
        1. Try change nsIndexIDListScanLimit
    :expected results:
        1. This should pass
    """
    for i in ['4000',
              'limit=0 flags=bogus',
              'limit=0 limit=1',
              'limit=0 type=eq type=eq',
              'limit=0 type=sub type=eq',
              'limit=0 flags=AND flags=AND',
              'limit=0 type=eq values=foo values=foo',
              'limit=0 type=eq values=foo,foo',
              'limit',
              'limit=0 type=pres values=bogus',
              'limit=0 type=eq,sub values=bogus',
              'limit=',
              'limit=1 type=',
              'limit=1 flags=',
              'limit=1 type=eq values=',
              'limit=-2',
              'type=eq',
              'limit=0 type=bogus']:
        with pytest.raises(ldap.UNWILLING_TO_PERFORM):
            Index(topo.standalone, GIVEN_NAME).replace('nsIndexIDListScanLimit', i)


def test_idlistscanlimit(topo):
    """Test various combinations of filters and idlistscanlimit

    :id: 44f83e2c-9f06-11e8-bffe-8c16451d917b
    :setup: Standalone instance
    :steps:
         1. Create entries
         2. Try change nsslapd-errorlog-level
         3. Test nsIndexIDListScanLimit values
         4. Search created entries
         5. restart instance
         6. Search created entries
         7. indexing works after restart
    :expected results:
         1. This should pass
         2. This should pass
         3. This should pass
         4. This should pass
         5. This should pass
         6. This should pass
         7. This should pass
    """
    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    for demo in ['Product Development',
                 'Accounting',
                 'Human Resources',
                 'Payroll',
                 'Product Testing']:
        ous.create(properties={'ou': demo})

    users_accounts = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=Accounting')
    users_human = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=Human Resources')
    users_testing = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=Product Testing')
    users_development = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=Product Development')
    users_payroll = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=Payroll')
    users_people = UserAccounts(topo.standalone, DEFAULT_SUFFIX)

    for data in [(LIST_OF_USER_ACCOUNTING, users_accounts),
                 (LIST_OF_USER_HUMAN, users_human),
                 (LIST_OF_USER_TESTING, users_testing),
                 (LIST_OF_USER_DEVELOPMENT, users_development),
                 (LIST_OF_USER_PAYROLL, users_payroll),
                 (LIST_OF_USER_PEOPLE, users_people)]:
        for demo1 in data[0]:
            fn = demo1.split()[0]
            sn = demo1.split()[1]
            uid = ''.join([fn[:1], sn]).lower()
            data[1].create(properties={
                'uid': uid,
                'cn': demo1,
                'sn': sn,
                'uidNumber': str(1000),
                'gidNumber': '2000',
                'homeDirectory': f'/home/{uid}',
                'givenname': fn,
                'userpassword': PW_DM,
                'mail': f'{uid}@test.com'
            })

    try:
        # Change log levels
        errorlog_value = topo.standalone.config.get_attr_val_utf8('nsslapd-errorlog-level')
        topo.standalone.config.set('nsslapd-errorlog-level', '524288')

        # Test nsIndexIDListScanLimit values
        for i in ['limit=1 type=eq values=Lutz,Hunter',
                  'limit=2 type=eq flags=AND values=Jensen,Rentz',
                  'limit=3 type=eq flags=AND',
                  'limit=4 type=eq', 'limit=5 flags=AND',
                  'limit=6',
                  'limit=1 type=sub values=*utz,*ter',
                  'limit=4 type=sub',
                  'limit=2 type=sub flags=AND values=*sen,*ntz',
                  'limit=3 type=sub flags=AND',
                  'limit=5 type=sub values=*sch*']:
            Index(topo.standalone, CN_NAME).replace('nsIndexIDListScanLimit', i)

        for i in ['limit=1 type=eq values=Andy,Andrew',
                  'limit=2 type=eq flags=AND values=Bjorn,David',
                  'limit=3 type=eq flags=AND',
                  'limit=4 type=eq',
                  'limit=5 flags=AND',
                  'limit=6']:
            Index(topo.standalone, GIVEN_NAME).replace('nsIndexIDListScanLimit', i)

        Index(topo.standalone, UNIQMEMBER).\
        replace('nsIndexIDListScanLimit',
                'limit=0 type=eq values=uid=kvaughan\2Cou=People\2Cdc=example\2Cdc=com,'
                'uid=rdaugherty\2Cou=People\2Cdc=example\2Cdc=com')

        Index(topo.standalone, OBJECTCLASS).\
        replace('nsIndexIDListScanLimit', 'limit=0 type=eq flags=AND values=inetOrgPerson')

        # Search with filter
        for i in ['(sn=Lutz)',
                  '(sn=*ter)',
                  '(&(sn=*sen)(objectclass=organizationalPerson))',
                  '(&(objectclass=organizationalPerson)(sn=*ntz))',
                  '(&(sn=Car*)(objectclass=organizationalPerson))',
                  '(sn=sc*)',
                  '(sn=*sch*)',
                  '(|(givenname=Andy)(givenname=Andrew))',
                  '(&(givenname=Bjorn)(objectclass=organizationalPerson))',
                  '(&(objectclass=organizationalPerson)(givenname=David))',
                  '(&(sn=*)(cn=*))',
                  '(sn=Hunter)',
                  '(&(givenname=Richard)(objectclass=organizationalPerson))',
                  '(givenname=Morgan)',
                  '(&(givenname=*)(cn=*))',
                  '(givenname=*)']:
            assert Accounts(topo.standalone, DEFAULT_SUFFIX).filter(f'{i}')

        # Creating Groups and adding members
        groups = UniqueGroups(topo.standalone, DEFAULT_SUFFIX)
        accounting_managers = groups.ensure_state(properties={'cn': 'Accounting Managers'})
        hr_managers = groups.ensure_state(properties={'cn': 'HR Managers'})

        accounting_managers.add('uniquemember',
            ['uid=scarter, ou=People, dc=example,dc=com',
             'uid=tmorris, ou=People, dc=example,dc=com',
             'uid=kvaughan, ou=People, dc=example,dc=com',
             'uid=rdaugherty, ou=People, dc=example,dc=com',
             'uid=hmiller, ou=People, dc=example,dc=com'])

        hr_managers.add('uniquemember',
            ['uid=kvaughan, ou=People, dc=example,dc=com',
             'uid=cschmith, ou=People, dc=example,dc=com'])

        # Test Filters
        for value in ['(uniquemember=uid=kvaughan,ou=People,dc=example,dc=com)',
                      '(uniquemember=uid=rdaugherty,ou=People,dc=example,dc=com)',
                      '(uniquemember=uid=hmiller,ou=People,dc=example,dc=com)',
                      '(&(objectclass=inetorgperson)(uid=scarter))',
                      '(&(objectclass=organizationalperson)(uid=scarter))',
                      '(objectclass=inetorgperson)',
                      '(&(objectclass=organizationalPerson)(sn=Jensen))',
                      '(&(mail=*)(objectclass=organizationalPerson))',
                      '(mail=*)',
                      '(&(sn=Rentz)(objectclass=organizationalPerson))',
                      '(&(sn=Ward)(sn=Ward))',
                      '(sn=Jensen)',
                      '(sn=*)',
                      '(sn=*utz)',
                      '(&(sn=Hunter)(sn=Rentz))']:
            if value == '(&(sn=Hunter)(sn=Rentz))':
                assert not Accounts(topo.standalone, DEFAULT_SUFFIX).filter(value)
            elif value.find('uniquemember') == 1:
                assert UniqueGroups(topo.standalone, DEFAULT_SUFFIX).filter(value)
            else:
                assert Accounts(topo.standalone, DEFAULT_SUFFIX).filter(value)

        # restart instance
        topo.standalone.restart()

        # see if indexing works after restart
        for value in ['(uniquemember=uid=kvaughan,ou=People,dc=example,dc=com)',
                      '(uniquemember=uid=rdaugherty,ou=People,dc=example,dc=com)',
                      '(uniquemember=uid=hmiller,ou=People,dc=example,dc=com)',
                      '(&(objectclass=inetorgperson)(uid=scarter))',
                      '(&(objectclass=organizationalperson)(uid=scarter))',
                      '(objectclass=inetorgperson)',
                      '(&(objectclass=organizationalPerson)(sn=Jensen))',
                      '(&(mail=*)(objectclass=organizationalPerson))',
                      '(mail=*)',
                      '(&(sn=Rentz)(objectclass=organizationalPerson))',
                      '(&(sn=Ward)(sn=Ward))',
                      '(sn=Jensen)',
                      '(sn=*)',
                      '(sn=*utz)']:
            if value == '(&(sn=Hunter)(sn=Rentz))':
                assert not Accounts(topo.standalone, DEFAULT_SUFFIX).filter(value)
            elif value.find('uniquemember') == 1:
                assert UniqueGroups(topo.standalone, DEFAULT_SUFFIX).filter(value)
            else:
                assert Accounts(topo.standalone, DEFAULT_SUFFIX).filter(value)

        assert not Accounts(topo.standalone, DEFAULT_SUFFIX).filter('(&(sn=Hunter)(sn=Rentz))')

        for value in ['(sn=Lutz)',
                      '(sn=*ter)',
                      '(&(sn=*sen)(objectclass=organizationalPerson))',
                      '(&(objectclass=organizationalPerson)(sn=*ntz))',
                      '(&(sn=Car*)(objectclass=organizationalPerson))',
                      '(sn=sc*)',
                      '(sn=*sch*)',
                      '(|(givenname=Andy)(givenname=Andrew))',
                      '(&(givenname=Bjorn)(objectclass=organizationalPerson))',
                      '(&(objectclass=organizationalPerson)(givenname=David))',
                      '(&(sn=*)(cn=*))',
                      '(sn=Hunter)',
                      '(&(givenname=Richard)(objectclass=organizationalPerson))',
                      '(givenname=Morgan)',
                      '(&(givenname=*)(cn=*))',
                      '(givenname=*)']:
            assert Accounts(topo.standalone, DEFAULT_SUFFIX).filter(value)

    finally:
        topo.standalone.config.set('nsslapd-errorlog-level', errorlog_value)


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
