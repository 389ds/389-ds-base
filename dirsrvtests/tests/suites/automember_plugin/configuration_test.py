# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import os
import pytest
from lib389.topologies import topology_st as topo
from lib389.plugins import AutoMembershipPlugin, AutoMembershipDefinitions, MemberOfPlugin
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

@pytest.mark.bz834056
def test_configuration(topo):
    """
    Automembership plugin and mixed in the plugin configuration
    :id: 45a5a8f8-e800-11e8-ab16-8c16451d917b
    :setup: Single Instance
    :steps:
        1. Automembership plugin fails in a MMR setup, if data and config
        area mixed in the plugin configuration
        2. Plugin configuration should throw proper error messages if not configured properly
    :expected results:
        1. Should success
        2. Should success
    """
    # Configure pluginConfigArea for PLUGIN_AUTO
    AutoMembershipPlugin(topo.standalone).set("nsslapd-pluginConfigArea", 'cn=config')
    # Enable MemberOf plugin
    MemberOfPlugin(topo.standalone).enable()
    topo.standalone.restart()
    # Add invalid configuration, which mixes data and config area: All will fail
    automembers = AutoMembershipDefinitions(topo.standalone)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        automembers.create(properties={
            'cn': 'autouserGroups',
            'autoMemberScope': f'ou=Employees,cn=config',
            'autoMemberFilter': "objectclass=posixAccount",
            'autoMemberDefaultGroup': [f'cn=SuffDef1,ou=autouserGroups,cn=config',
                                       f'cn=SuffDef2,ou=autouserGroups,cn=config'],
            'autoMemberGroupingAttr': 'member:dn'
        })
    # Search in error logs
    assert topo.standalone.ds_error_log.match('.*ERR - auto-membership-plugin - '
                                              'automember_parse_config_entry - The default group '
                                              '"cn=SuffDef1,ou=autouserGroups,cn=config" '
                                              'can not be a child of the plugin config area "cn=config"')

def test_invalid_regex(topo):
    """Test invalid regex is properly reportedin the error log

    :id: a6d89f84-ec76-4871-be96-411d051800b1
    :setup: Standalone Instance
    :steps:
        1. Setup automember
        2. Add invalid regex
        3. Error log reports useful message
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    REGEX_DN = "cn=regex1,cn=testregex,cn=auto membership plugin,cn=plugins,cn=config"
    REGEX_VALUE = "cn=*invalid*"
    REGEX_ESC_VALUE = "cn=\\*invalid\\*"
    GROUP_DN = "cn=demo_group,ou=groups,"  + DEFAULT_SUFFIX

    AutoMembershipPlugin(topo.standalone).remove_all("nsslapd-pluginConfigArea")
    automemberplugin = AutoMembershipPlugin(topo.standalone)

    automember_prop = {
        'cn': 'testRegex',
        'autoMemberScope': 'ou=People,' + DEFAULT_SUFFIX,
        'autoMemberFilter': 'objectclass=*',
        'autoMemberDefaultGroup': GROUP_DN,
        'autoMemberGroupingAttr': 'member:dn',
    }
    automember_defs = AutoMembershipDefinitions(topo.standalone, "cn=Auto Membership Plugin,cn=plugins,cn=config")
    automember_def = automember_defs.create(properties=automember_prop)
    automember_def.add_regex_rule("regex1", GROUP_DN, include_regex=[REGEX_VALUE])

    automemberplugin.enable()
    topo.standalone.restart()

    # Check errors log for invalid message
    ERR_STR1 = "automember_parse_regex_rule - Unable to parse regex rule"
    ERR_STR2 = f"Skipping invalid inclusive regex rule in rule entry \"{REGEX_DN}\" \\(rule = \"{REGEX_ESC_VALUE}\"\\)"
    assert topo.standalone.searchErrorsLog(ERR_STR1)
    assert topo.standalone.searchErrorsLog(ERR_STR2)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
