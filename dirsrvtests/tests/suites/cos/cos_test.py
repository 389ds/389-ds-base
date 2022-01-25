# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import time
import pytest, os, ldap
from lib389.cos import  CosClassicDefinition, CosClassicDefinitions, CosTemplate
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.idm.role import FilteredRoles
from lib389.idm.nscontainer import nsContainer
from lib389.idm.user import UserAccount

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1
@pytest.fixture(scope="function")
def reset_ignore_vattr(topo, request):
    default_ignore_vattr_value = topo.standalone.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs')
    def fin():
        topo.standalone.config.set('nsslapd-ignore-virtual-attrs', default_ignore_vattr_value)

    request.addfinalizer(fin)

def test_positive(topo, reset_ignore_vattr):
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
    cosdef.delete()

def test_vattr_on_cos_definition(topo, reset_ignore_vattr):
    """Test nsslapd-ignore-virtual-attrs configuration attribute
       The attribute is ON by default. If a cos definition is
       added it is moved to OFF

    :id: e7ef5254-386f-4362-bbb4-9409f3f51b08
    :customerscenario: True
    :setup: Standalone instance
    :steps:
         1. Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config
         2. Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON
         3. Create a cos definition for employeeType
         4. Check the value of nsslapd-ignore-virtual-attrs should be OFF (with a delay for postop processing)
         5. Check a message "slapi_vattrspi_regattr - Because employeeType,.." in error logs
         6. Check after deleting cos definition value of attribute nsslapd-ignore-virtual-attrs is set back to ON
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
         6. This should be successful
    """

    log.info("Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs')

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON")
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    # creating CosClassicDefinition
    log.info("Create a cos definition")
    properties = {'cosTemplateDn': 'cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,{}'.format(DEFAULT_SUFFIX),
                  'cosAttribute': 'employeeType',
                  'cosSpecifier': 'nsrole',
                  'cn': 'cosClassicGenerateEmployeeTypeUsingnsrole'}
    cosdef = CosClassicDefinition(topo.standalone,'cn=cosClassicGenerateEmployeeTypeUsingnsrole,{}'.format(DEFAULT_SUFFIX))\
        .create(properties=properties)

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be OFF")
    time.sleep(2)
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs', 'off')

    topo.standalone.stop()
    assert topo.standalone.searchErrorsLog("slapi_vattrspi_regattr - Because employeeType is a new registered virtual attribute , nsslapd-ignore-virtual-attrs was set to \'off\'")
    topo.standalone.start()
    log.info("Delete a cos definition")
    cosdef.delete()
    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs is back to ON")
    topo.standalone.restart()
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
