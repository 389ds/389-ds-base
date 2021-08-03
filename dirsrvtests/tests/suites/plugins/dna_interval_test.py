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
from lib389.utils import *

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def dna_plugin(topology_st, request):
    inst = topology_st.standalone
    plugin = DNAPlugin(inst)
    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    ou_people = ous.get("People")

    log.info("Add dna plugin config entry...")
    configs = DNAPluginConfigs(inst, plugin.dn)
    dna_config = configs.create(properties={'cn': 'dna config',
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

    def fin():
        inst.stop()
        dse_ldif = DSEldif(inst)
        dse_ldif.delete_dn(f'cn=dna config,{plugin.dn}')
        inst.start()
    request.addfinalizer(fin)

    return dna_config


def test_dna_interval(topology_st, dna_plugin):
    """Test the dna interval works

    :id: 3982d698-e16b-4945-9eb4-eecaa4bac5f7
    :customerscenario: True
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
        'mail': 'interval@example.com',
        'homeDirectory': '/home/interval'})

    # Verify DNA works
    assert user.get_attr_val_utf8_l('uidNumber') == '10'

    # Make update and verify interval was applied
    log.info("Test DNA interval assignment is working...")
    user.replace('uidNumber', '-1')
    assert user.get_attr_val_utf8_l('uidNumber') == '20'


def test_dna_max_value(topology_st, dna_plugin):
    """Test the dna max value works with dna interval

    :id: cc979ea8-3cd0-4d52-af35-9cea7cf8cb5f
    :customerscenario: True
    :setup: Standalone Instance
    :steps:
        1. Set dnaMaxValue, dnaNextValue, dnaInterval values to 100, 90, 50 respectively
        2. Create user that trigger DNA to assign a value
        3. Try to make an update to entry that triggers DNA again
    :expectedresults:
        1. Success
        2. Success
        3. Operation should fail with OPERATIONS_ERROR
    """
    log.info("Make the config changes needed to test dnaMaxValue")
    dna_plugin.replace_many(('dnaMaxValue', '100'), ('dnaNextValue', '90'), ('dnaInterval', '50'))
    # Create user2
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    log.info('Adding user2')
    user = users.create(properties={
        'sn': 'interval2',
        'cn': 'interval2',
        'uid': 'interval2',
        'uidNumber': '-1',  # Magic regen value
        'gidNumber': '222',
        'givenname': 'interval2',
        'homePhone': '0861234567',
        'carLicense': '131D16674',
        'mail': 'interval2@example.com',
        'homeDirectory': '/home/interval2'})

    # Verify DNA works
    assert user.get_attr_val_utf8_l('uidNumber') == '90'

    log.info("Make an update and verify it raises error as the new interval value is more than dnaMaxValue")
    with pytest.raises(ldap.OPERATIONS_ERROR):
        user.replace('uidNumber', '-1')


@pytest.mark.parametrize('attr_value', ('0', 'abc', '2000'))
def test_dna_interval_with_different_values(topology_st, dna_plugin, attr_value):
    """Test the dna interval with different values

    :id: 1a3f69fd-1d8d-4046-ba68-b6aa7cafbd37
    :customerscenario: True
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Set dnaInterval value to 0
        2. Set dnaInterval value to 'abc'
        3. Set dnaInterval value to 2000 and dnaMaxValue to 1000
        4. Create user that trigger DNA to assign a value
        5. Try to make an update to entry that triggers DNA again when dnaInerval is greater than dnaMaxValue
    :expectedresults:
        1. Success
        2. Operation should fail with INVALID_SYNTAX
        3. Success
        4. Success
        5. Operation should fail with OPERATIONS_ERROR
    """
    log.info("Make the config changes needed to test dnaInterval")
    if attr_value == '0':
        dna_plugin.replace('dnaInterval', attr_value)
    elif attr_value == 'abc':
        with pytest.raises(ldap.INVALID_SYNTAX):
            dna_plugin.replace('dnaInterval', attr_value)
    else:
        dna_plugin.replace_many(('dnaInterval', attr_value), ('dnaMaxValue', '1000'))
    # Create user3
        users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
        log.info('Adding user3')
        user = users.create(properties={
            'sn': 'interval3',
            'cn': 'interval3',
            'uid': 'interval3',
            'uidNumber': '-1',  # Magic regen value
            'gidNumber': '333',
            'givenname': 'interval3',
            'homePhone': '0861234567',
            'carLicense': '131D16674',
            'mail': 'interval3@example.com',
            'homeDirectory': '/home/interval3'})

        # Verify DNA works
        assert user.get_attr_val_utf8_l('uidNumber') == '10'

        log.info("Make an update and verify it raises error as the new interval value is more than dnaMaxValue")
        with pytest.raises(ldap.OPERATIONS_ERROR):
            user.replace('uidNumber', '-1')
