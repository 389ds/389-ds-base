# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest, os, ldap
from lib389.cos import  CosClassicDefinition, CosClassicDefinitions, CosTemplate
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.idm.role import FilteredRoles
from lib389.idm.nscontainer import nsContainer
from lib389.idm.user import UserAccount

pytestmark = pytest.mark.tier1

def test_positive(topo):
    """CoS positive tests

    :id: a5a74235-597f-4fe8-8c38-826860927472
    :setup: server
    :steps:
        1. Add filter role entry
        2. Add ns container
        3. Add cos template
        4. Add CosClassic Definition
        5. Cos entries should be added and searchable
        6. employeeType attribute should be there in user entry as per the cos plugin property
    :expectedresults:
        1. Operation should success
        2. Operation should success
        3. Operation should success
        4. Operation should success
        5. Operation should success
        6. Operation should success
    """
    # Adding ns filter role
    roles = FilteredRoles(topo.standalone, DEFAULT_SUFFIX)
    roles.create(properties={'cn': 'FILTERROLEENGROLE',
                             'nsRoleFilter': 'cn=eng*'})
    # adding ns container
    nsContainer(topo.standalone,'cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,{}'.format(DEFAULT_SUFFIX))\
        .create(properties={'cn': 'cosTemplates'})

    # creating cos template
    properties = {'employeeType': 'EngType',
                  'cn': '"cn=filterRoleEngRole,dc=example,dc=com",cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,dc=example,dc=com'
                  }
    CosTemplate(topo.standalone, 'cn="cn=filterRoleEngRole,dc=example,dc=com",cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,{}'.format(DEFAULT_SUFFIX))\
        .create(properties=properties)

    # creating CosClassicDefinition
    properties = {'cosTemplateDn': 'cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,{}'.format(DEFAULT_SUFFIX),
                  'cosAttribute': 'employeeType',
                  'cosSpecifier': 'nsrole',
                  'cn': 'cosClassicGenerateEmployeeTypeUsingnsrole'}
    CosClassicDefinition(topo.standalone,'cn=cosClassicGenerateEmployeeTypeUsingnsrole,{}'.format(DEFAULT_SUFFIX))\
        .create(properties=properties)

    # Adding User entry
    properties = {
        'uid': 'enguser1',
        'cn': 'enguser1',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'enguser1'
    }
    user = UserAccount(topo.standalone, 'cn=enguser1,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    # Asserting Cos should be added and searchable
    cosdef = CosClassicDefinitions(topo.standalone, DEFAULT_SUFFIX).get('cosClassicGenerateEmployeeTypeUsingnsrole')
    assert cosdef.dn == 'cn=cosClassicGenerateEmployeeTypeUsingnsrole,dc=example,dc=com'
    assert cosdef.get_attr_val_utf8('cn') == 'cosClassicGenerateEmployeeTypeUsingnsrole'

    #  CoS definition entry's cosSpecifier attribute specifies the employeeType attribute
    assert user.present('employeeType')


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
