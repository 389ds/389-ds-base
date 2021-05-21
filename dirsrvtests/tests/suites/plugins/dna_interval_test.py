# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Test DNA plugin functionality"""

import logging
import pytest
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import DNAPlugin, DNAPluginConfigs
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

def test_dna_interval(topology_st):
    """Test the dna interval works

    :id: 3982d698-e16b-4945-9eb4-eecaa4bac5f7
    :setup: Standalone Instance
    :steps:
        1. Set DNAZZ interval to 10
        2. Create user that trigger DNA to assign a value
        3. Verify DNA is working
        4. Make update to entry that triggers DNA again
        5. Verify interval is applied as expected
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    inst = topology_st.standalone
    plugin = DNAPlugin(inst)
    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    ou_people = ous.get("People")

    log.info("Add dna plugin config entry...")
    configs = DNAPluginConfigs(inst, plugin.dn)
    configs.create(properties={'cn': 'dna config',
                               'dnaType': 'uidNumber',
                               'dnaMaxValue': '1000',
                               'dnaMagicRegen': '-1',
                               'dnaFilter': '(objectclass=top)',
                               'dnaScope': ou_people.dn,
                               'dnaNextValue': '10',
                               'dnaInterval': '10'})

    log.info("Enable the DNA plugin and restart...")
    plugin.enable()
    inst.restart()

    # Create user and check interval
    log.info("Test DNA is working...")
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    log.info('Adding user1')
    user = users.create(properties={
        'sn': 'interval',
        'cn': 'interval',
        'uid': 'interval',
        'uidNumber': '-1',  # Magic regen value
        'gidNumber': '111',
        'givenname': 'interval',
        'homePhone': '0861234567',
        'carLicense': '131D16674',
        'mail': 'interval@whereever.com',
        'homeDirectory': '/home/interval'})

    # Verify DNA works
    assert user.get_attr_val_utf8_l('uidNumber') == '10'

    # Make update and verify interval was applied
    log.info("Test DNA interval assignment is working...")
    user.replace('uidNumber', '-1')
    assert user.get_attr_val_utf8_l('uidNumber') == '20'
