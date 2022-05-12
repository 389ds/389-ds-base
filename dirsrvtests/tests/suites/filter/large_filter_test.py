# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


"""
verify and testing  Filter from a search
"""

import os
import pytest

from lib389._constants import PW_DM
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.account import Accounts
from lib389.backend import Backends
from lib389.idm.domain import Domain

SUFFIX = 'dc=anuj,dc=com'

pytestmark = pytest.mark.tier1


@pytest.fixture(scope="module")
def _create_entries(request, topo):
    """
    Will create necessary users for this script.
    """
    # Creating Backend
    backends = Backends(topo.standalone)
    backend = backends.create(properties={'nsslapd-suffix': SUFFIX, 'cn': 'AnujRoot'})
    # Creating suffix
    suffix = Domain(topo.standalone, SUFFIX).create(properties={'dc': 'anuj'})
    # Add ACI
    domain = Domain(topo.standalone, SUFFIX)
    domain.add("aci", f'(targetattr!="userPassword")(version 3.0; acl '
                      f'"Enable anonymous access";allow (read, search, compare) userdn="ldap:///anyone";)')
    # Creating Users
    users = UserAccounts(topo.standalone, suffix.dn, rdn=None)

    for user in ['scarter',
                 'dmiller',
                 'jwallace',
                 'rdaugherty',
                 'tmason',
                 'bjablons',
                 'bhal2',
                 'lulrich',
                 'fmcdonnagh',
                 'prigden',
                 'mlott',
                 'tpierce',
                 'brentz',
                 'ekohler',
                 'tschneid',
                 'falbers',
                 'jburrell',
                 'calexand',
                 'phunt',
                 'rulrich',
                 'awhite',
                 'jjensen',
                 'dward',
                 'plorig',
                 'mreuter',
                 'dswain',
                 'jvedder',
                 'jlutz',
                 'tcouzens',
                 'kcope',
                 'mvaughan',
                 'dcope',
                 'ttully',
                 'drose',
                 'jvaughan',
                 'brigden',
                 'rjense2',
                 'bparker',
                 'cnewport']:
        users.create(properties={
            'mail': f'{user}@redhat.com',
            'uid': user,
            'givenName': user.title(),
            'cn': f'bit {user}',
            'sn': user.title(),
            'manager': f'uid={user},{SUFFIX}',
            'userpassword': PW_DM,
            'homeDirectory': '/home/' + user,
            'uidNumber': '1000',
            'gidNumber': '2000',
        })

    def fin():
        """
        Deletes entries after the test.
        """
        for user in users.list():
            user.delete()

        suffix.delete()
        backend.delete()

    request.addfinalizer(fin)


FILTERS = ['(&(objectClass=person)(|(manager=uid=fmcdonnagh,dc=anuj,dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_0,dc=anuj,dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_1,dc=anuj,dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_2,dc=anuj,dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_3,dc=anuj,dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_4,dc=anuj,dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_5,dc=anuj,dc=com)'
           '(manager=uid=jvedder,  dc=anuj, dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_6,dc=anuj,dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_7,dc=anuj,dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_8,dc=anuj,dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_9,dc=anuj,dc=com)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_10,dc=anuj,dc=com)'
           '(manager=uid=cnewport,  dc=anuj, dc=com)))',
           '(&(objectClass=person)(|(manager=uid=fmcdonnagh *)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_0,*)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_1,*)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_2,*)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_3,*)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_4,*)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_5,*)'
           '(manager=uid=jvedder,*)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_6,*)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_7,*)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_8,*)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_9,*)'
           '(manager=cn=no_such_entry_with_a_really_long_dn_component_to_stress_the_filter_handling_code_10,*)'
           '(manager=uid=cnewport,*)))']


@pytest.mark.bz772777
@pytest.mark.parametrize("real_value", FILTERS)
def test_large_filter(topo, _create_entries, real_value, ids=FILTERS):
    """Exercise large eq filter with dn syntax attributes

        :id: abe3e6de-9ecc-11e8-adf0-8c16451d917b
        :parametrized: yes
        :setup: Standalone
        :steps:
            1. Try to pass filter rules as per the condition.
            2. Bind with any user.
            3. Try to pass filter rules with new binding.
        :expected results:
            1. Pass
            2. Pass
            3. Pass
    """
    assert len(Accounts(topo.standalone, SUFFIX).filter(real_value)) == 3
    conn = UserAccount(topo.standalone, f'uid=drose,{SUFFIX}').bind(PW_DM)
    assert len(Accounts(conn, SUFFIX).filter(real_value)) == 3


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
