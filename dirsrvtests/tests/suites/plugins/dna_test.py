# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
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
from lib389.plugins import DNAPlugin, DNAPluginSharedConfigs, DNAPluginConfigs
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_st
import ldap

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


def test_dnatype_only_valid(topology_st):
    """Test that DNA plugin only accepts valid attributes for "dnaType"

    :id: 0878ecff-5fdc-47d7-8c8f-edf4556f9746
    :setup: Standalone Instance
    :steps:
        1. Create a use entry
        2. Create DNA shared config entry container
        3. Create DNA shared config entry
        4. Add DNA plugin config entry
        5. Enable DNA plugin
        6. Restart the instance
        7. Replace dnaType with invalid value
    :expectedresults:
        1. Successful
        2. Successful
        3. Successful
        4. Successful
        5. Successful
        6. Successful
        7. Unwilling to perform exception should be raised
    """

    inst = topology_st.standalone
    plugin = DNAPlugin(inst)

    log.info("Creating an entry...")
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    users.create_test_user(uid=1)

    log.info("Creating \"ou=ranges\"...")
    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    ou_ranges = ous.create(properties={'ou': 'ranges'})
    ou_people = ous.get("People")

    log.info("Creating DNA shared config entry...")
    shared_configs = DNAPluginSharedConfigs(inst, ou_ranges.dn)
    shared_configs.create(properties={'dnaHostname': str(inst.host),
                                      'dnaPortNum': str(inst.port),
                                      'dnaRemainingValues': '9501'})

    log.info("Add dna plugin config entry...")
    configs = DNAPluginConfigs(inst, plugin.dn)
    config = configs.create(properties={'cn': 'dna config',
                                        'dnaType': 'description',
                                        'dnaMaxValue': '10000',
                                        'dnaMagicRegen': '0',
                                        'dnaFilter': '(objectclass=top)',
                                        'dnaScope': ou_people.dn,
                                        'dnaNextValue': '500',
                                        'dnaSharedCfgDN': ou_ranges.dn})

    log.info("Enable the DNA plugin...")
    plugin.enable()

    log.info("Restarting the server...")
    inst.restart()

    log.info("Apply an invalid attribute to the DNA config(dnaType: foo)...")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        config.replace('dnaType', 'foo')
