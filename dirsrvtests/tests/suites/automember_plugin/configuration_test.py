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
