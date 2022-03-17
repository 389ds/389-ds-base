# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import time
import pytest
import os
from lib389._constants import PW_DM, DEFAULT_SUFFIX
from lib389.idm.organization import Organization
from lib389.topologies import topology_m1c1 as topo
from lib389.idm.role import FilteredRoles, ManagedRoles
from lib389.cos import  CosClassicDefinition, CosClassicDefinitions, CosTemplate
from lib389.replica import ReplicationManager

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1

DNBASE = "o=acivattr,{}".format(DEFAULT_SUFFIX)


@pytest.fixture(scope="function")
def reset_ignore_vattr(topo, request):
    s = topo.ms['supplier1']
    c = topo.cs['consumer1']
    default_ignore_vattr_value = s.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs')
    default_ignore_vattr_value = c.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs')

    def fin():
        s.config.set('nsslapd-ignore-virtual-attrs', default_ignore_vattr_value)
        c.config.set('nsslapd-ignore-virtual-attrs', default_ignore_vattr_value)

    request.addfinalizer(fin)

def test_vattr_on_cos_definition_with_replication(topo, reset_ignore_vattr):
    """Test nsslapd-ignore-virtual-attrs configuration attribute
       The attribute is ON by default. If a cos definition is
       added it is moved to OFF in replication scenario

    :id: c1fd8fa1-bd13-478b-9b33-e33b49c587bd
    :customerscenario: True
    :setup: Supplier Consumer
    :steps:
         1. Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config over consumer
         2. Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON over consumer
         3. Create a cos definition for employeeType in supplier
         4. Check the value of nsslapd-ignore-virtual-attrs should be OFF (with a delay for postop processing) over consumer
         5. Check a message "slapi_vattrspi_regattr - Because employeeType,.." in error logs of consumer
         6. Check after deleting cos definition value of attribute nsslapd-ignore-virtual-attrs is set back to ON over consumer
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
         6. This should be successful
    """
    s = topo.ms['supplier1']
    c = topo.cs['consumer1']
    log.info("Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config over consumer")
    assert c.config.present('nsslapd-ignore-virtual-attrs')

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON over consumer")
    assert c.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    # creating CosClassicDefinition in supplier
    log.info("Create a cos definition")
    properties = {'cosTemplateDn': 'cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,{}'.format(DEFAULT_SUFFIX),
                  'cosAttribute': 'employeeType',
                  'cosSpecifier': 'nsrole',
                  'cn': 'cosClassicGenerateEmployeeTypeUsingnsrole'}
    cosdef = CosClassicDefinition(s,'cn=cosClassicGenerateEmployeeTypeUsingnsrole,{}'.format(DEFAULT_SUFFIX))\
        .create(properties=properties)

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs is OFF over consumer")
    time.sleep(2)
    assert c.config.present('nsslapd-ignore-virtual-attrs', 'off')

    #stop both supplier and consumer
    c.stop()
    assert c.searchErrorsLog("slapi_vattrspi_regattr - Because employeeType is a new registered virtual attribute , nsslapd-ignore-virtual-attrs was set to \'off\'")
    c.start()
    log.info("Delete a cos definition")
    cosdef.delete()
    repl = ReplicationManager(DEFAULT_SUFFIX)
    log.info("Check Delete was propagated")
    repl.wait_for_replication(s, c)

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs is back to ON over consumer")
    s.restart()
    c.restart()
    assert c.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

def test_vattr_on_filtered_role_with_replication(topo, request):
    """Test nsslapd-ignore-virtual-attrs configuration attribute
       The attribute is ON by default. If a filtered role is
       added it is moved to OFF in replication scenario
    :id: 7b29be88-c8ca-409b-bbb7-ce3962f73f91
    :customerscenario: True
    :setup: Supplier Consumer
    :steps:
         1. Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config over consumer
         2. Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON over consumer
         3. Create a filtered role in supplier
         4. Check the value of nsslapd-ignore-virtual-attrs should be OFF over consumer
         5. Check a message "roles_cache_trigger_update_role - Because of virtual attribute.." in error logs of consumer
         6. Check after deleting role definition value of attribute nsslapd-ignore-virtual-attrs is set back to ON over consumer
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
         6. This should be successful
    """
    s = topo.ms['supplier1']
    c = topo.cs['consumer1']

    log.info("Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config over consumer")
    assert c.config.present('nsslapd-ignore-virtual-attrs')

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON over consumer")
    assert c.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    log.info("Create a filtered role")
    try:
        Organization(s).create(properties={"o": "acivattr"}, basedn=DEFAULT_SUFFIX)
    except:
        pass
    roles = FilteredRoles(s, DNBASE)
    roles.create(properties={'cn': 'FILTERROLEENGROLE', 'nsRoleFilter': 'cn=eng*'})

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be OFF over consumer")
    time.sleep(5)
    assert c.config.present('nsslapd-ignore-virtual-attrs', 'off')

    c.stop()
    assert c.searchErrorsLog("roles_cache_trigger_update_role - Because of virtual attribute definition \(role\), nsslapd-ignore-virtual-attrs was set to \'off\'")

    def fin():
        s.restart()
        c.restart()
        try:
            filtered_roles = FilteredRoles(s, DEFAULT_SUFFIX)
            for i in filtered_roles.list():
                i.delete()
        except:
            pass
        log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs is back to ON over consumer")
        s.restart()
        c.restart()
        assert c.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    request.addfinalizer(fin)

def test_vattr_on_managed_role_replication(topo, request):
    """Test nsslapd-ignore-virtual-attrs configuration attribute
       The attribute is ON by default. If a managed role is
       added it is moved to OFF in replcation scenario

    :id: 446f2fc3-bbb2-4835-b14a-cb855db78c6f
    :customerscenario: True
    :setup: Supplier Consumer
    :steps:
         1. Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config over consumer
         2. Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON over consumer
         3. Create a managed role in supplier
         4. Check the value of nsslapd-ignore-virtual-attrs should be OFF over consumer
         5. Check a message "roles_cache_trigger_update_role - Because of virtual attribute.." in error logs of consumer
         6. Check after deleting role definition value of attribute nsslapd-ignore-virtual-attrs is set back to ON over consumer
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
         6. This should be successful
    """
    s = topo.ms['supplier1']
    c = topo.cs['consumer1']
    log.info("Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config")
    assert c.config.present('nsslapd-ignore-virtual-attrs')

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON")
    assert c.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    log.info("Create a managed role")
    roles = ManagedRoles(s, DEFAULT_SUFFIX)
    role = roles.create(properties={"cn": 'ROLE1'})

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be OFF")
    time.sleep(5)
    assert c.config.present('nsslapd-ignore-virtual-attrs', 'off')

    c.stop()
    assert c.searchErrorsLog("roles_cache_trigger_update_role - Because of virtual attribute definition \(role\), nsslapd-ignore-virtual-attrs was set to \'off\'")

    def fin():
        s.restart()
        c.restart()
        try:
            filtered_roles = ManagedRoles(s, DEFAULT_SUFFIX)
            for i in filtered_roles.list():
                i.delete()
        except:
            pass
        log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs is back to ON")
        s.restart()
        c.restart()
        assert c.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    request.addfinalizer(fin)

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
