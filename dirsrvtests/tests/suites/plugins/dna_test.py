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
import os
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import DNAPlugin, DNAPluginSharedConfigs, DNAPluginConfigs
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts, UserAccount
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


def test_dna_exclude_scope_functionality(topology_st):
    """Test DNA plugin dnaExcludeScope functionality

    :id: a0603f09-7484-42ef-8468-41f16a51c321
    :setup: Standalone instance with DNA plugin configured
    :steps:
        1. Test basic DNA functionality (magic regen -1 becomes assigned number)
        2. Test DNA preserves explicitly set values
        3. Test DNA in staging OU without exclude scope
        4. Add staging OU to dnaExcludeScope
        5. Verify DNA works in main suffix but not in excluded staging OU
        6. Test invalid DN handling in dnaExcludeScope
    :expectedresults:
        1. DNA replaces magic regen value -1 with assigned numbers
        2. DNA preserves explicitly set non-magic values
        3. DNA works in all areas when no exclude scope is set
        4. dnaExcludeScope is configured successfully
        5. DNA skips excluded staging OU but works elsewhere
        6. Invalid DNs are rejected with INVALID_SYNTAX error
    """
    inst = topology_st.standalone
    plugin = DNAPlugin(inst)

    plugin.enable()

    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    staging_ou = ous.create(properties={'ou': 'staging'})

    configs = DNAPluginConfigs(inst, plugin.dn)
    dna_config = configs.create(properties={
        'cn': 'exclude scope config',
        'dnaType': 'employeeNumber',
        'dnaNextValue': '1000',
        'dnaMaxValue': '2000',
        'dnaMagicRegen': '-1',
        'dnaFilter': '(&(objectClass=person)(objectClass=organizationalPerson)(objectClass=inetOrgPerson))',
        'dnaScope': DEFAULT_SUFFIX
    })

    inst.restart()

    users = UserAccounts(inst, DEFAULT_SUFFIX)

    log.info("Test basic DNA functionality (magic regen -1 becomes assigned number)")
    user1 = users.create_test_user()
    user1.replace('employeeNumber', '-1')
    assigned_num = user1.get_attr_val_utf8('employeeNumber')
    assert assigned_num != '-1', f"DNA should replace magic value -1 with real number, got {assigned_num}"
    assert int(assigned_num) >= 1000, f"Assigned number {assigned_num} should be in configured range"
    user1.delete()

    log.info("Test DNA preserves explicitly set values")
    user2 = users.create_test_user()
    user2.replace('employeeNumber', '500')
    preserved_num = user2.get_attr_val_utf8('employeeNumber')
    assert preserved_num == '500', f"DNA should preserve explicit value 500, got {preserved_num}"
    user2.delete()

    log.info("Test DNA in staging OU without exclude scope")
    user3 = UserAccount(inst, dn=f'cn=staging_user3,{staging_ou.dn}')
    user3.create(properties={
        'cn': 'staging_user3',
        'sn': 'user3',
        'uid': 'staging_user3',
        'uidNumber': '3000',
        'gidNumber': '3000',
        'homeDirectory': '/home/staging_user3',
        'employeeNumber': '-1'
    })
    assigned_num3 = user3.get_attr_val_utf8('employeeNumber')
    assert assigned_num3 != '-1', f"DNA should work in staging OU when not excluded, got {assigned_num3}"
    user3.delete()

    log.info("Add staging OU to dnaExcludeScope")
    dna_config.add('dnaExcludeScope', staging_ou.dn)

    log.info("Verify DNA works in main suffix but not in excluded staging OU")
    user4 = users.create_test_user()
    user4.replace('employeeNumber', '-1')
    assigned_num4 = user4.get_attr_val_utf8('employeeNumber')
    assert assigned_num4 != '-1', f"DNA should still work in main suffix, got {assigned_num4}"

    user5 = UserAccount(inst, dn=f'cn=staging_user5,{staging_ou.dn}')
    user5.create(properties={
        'cn': 'staging_user5',
        'sn': 'user5',
        'uid': 'staging_user5',
        'uidNumber': '3001',
        'gidNumber': '3001',
        'homeDirectory': '/home/staging_user5',
        'employeeNumber': '-1'
    })
    excluded_num5 = user5.get_attr_val_utf8('employeeNumber')
    assert excluded_num5 == '-1', f"DNA should skip excluded staging OU, magic value should remain -1, got {excluded_num5}"

    user4.delete()
    user5.delete()

    log.info("Test invalid DN handling in dnaExcludeScope")
    invalid_dn = f"invalidDN,{DEFAULT_SUFFIX}"
    with pytest.raises(ldap.INVALID_SYNTAX):
        dna_config.add('dnaExcludeScope', invalid_dn)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
