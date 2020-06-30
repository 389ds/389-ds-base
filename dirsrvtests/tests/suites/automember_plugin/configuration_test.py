# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import pytest

from lib389.topologies import topology_st as topo
from lib389.plugins import AutoMembershipPlugin, AutoMembershipDefinitions, MemberOfPlugin
import ldap

pytestmark = pytest.mark.tier1


@pytest.mark.bz834056
def test_configuration(topo):
    """Automembership plugin and mixed in the plugin configuration

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


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
