# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX
USER3_DN = 'uid=user3,' + DEFAULT_SUFFIX
BUSER1_DN = 'uid=user1,ou=branch1,' + DEFAULT_SUFFIX
BUSER2_DN = 'uid=user2,ou=branch2,' + DEFAULT_SUFFIX
BUSER3_DN = 'uid=user3,ou=branch2,' + DEFAULT_SUFFIX
BRANCH1_DN = 'ou=branch1,' + DEFAULT_SUFFIX
BRANCH2_DN = 'ou=branch2,' + DEFAULT_SUFFIX
GROUP_OU = 'ou=groups,' + DEFAULT_SUFFIX
PEOPLE_OU = 'ou=people,' + DEFAULT_SUFFIX
GROUP_DN = 'cn=group,' + DEFAULT_SUFFIX
CONFIG_AREA = 'nsslapd-pluginConfigArea'


def test_basic(topology_st):
    """Test basic functionality"""

    # Stop the plugin, and start it
    topology_st.standalone.plugins.disable(name=PLUGIN_DNA)
    topology_st.standalone.plugins.enable(name=PLUGIN_DNA)

    CONFIG_DN = 'cn=config,cn=' + PLUGIN_DNA + ',cn=plugins,cn=config'

    log.info('Testing ' + PLUGIN_DNA + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    try:
        topology_st.standalone.add_s(Entry((CONFIG_DN, {
            'objectclass': 'top dnaPluginConfig'.split(),
            'cn': 'config',
            'dnatype': 'uidNumber',
            'dnafilter': '(objectclass=top)',
            'dnascope': DEFAULT_SUFFIX,
            'dnaMagicRegen': '-1',
            'dnaMaxValue': '50000',
            'dnaNextValue': '1'
        })))
    except ldap.ALREADY_EXISTS:
        try:
            topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'dnaNextValue', '1'),
                                                        (ldap.MOD_REPLACE, 'dnaMagicRegen', '-1')])
        except ldap.LDAPError as e:
            log.fatal('test_dna: Failed to set the DNA plugin: error ' + e.message['desc'])
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to add config entry: error ' + e.message['desc'])
        assert False

    # Do we need to restart for the plugin?

    topology_st.standalone.restart()

    ############################################################################
    # Test plugin
    ############################################################################

    try:
        topology_st.standalone.add_s(Entry((USER1_DN, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'user1'
        })))
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to user1: error ' + e.message['desc'])
        assert False

    # See if the entry now has the new uidNumber assignment - uidNumber=1
    try:
        entries = topology_st.standalone.search_s(USER1_DN, ldap.SCOPE_BASE, '(uidNumber=1)')
        if not entries:
            log.fatal('test_dna: user1 was not updated - (looking for uidNumber: 1)')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_dna: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Test the magic regen value
    try:
        topology_st.standalone.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'uidNumber', '-1')])
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to set the magic reg value: error ' + e.message['desc'])
        assert False

    # See if the entry now has the new uidNumber assignment - uidNumber=2
    try:
        entries = topology_st.standalone.search_s(USER1_DN, ldap.SCOPE_BASE, '(uidNumber=2)')
        if not entries:
            log.fatal('test_dna: user1 was not updated (looking for uidNumber: 2)')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_dna: Search for user1 failed: ' + e.message['desc'])
        assert False

    ################################################################################
    # Change the config
    ################################################################################

    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'dnaMagicRegen', '-2')])
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to set the magic reg value to -2: error ' + e.message['desc'])
        assert False

    ################################################################################
    # Test plugin
    ################################################################################

    # Test the magic regen value
    try:
        topology_st.standalone.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'uidNumber', '-2')])
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to set the magic reg value: error ' + e.message['desc'])
        assert False

    # See if the entry now has the new uidNumber assignment - uidNumber=3
    try:
        entries = topology_st.standalone.search_s(USER1_DN, ldap.SCOPE_BASE, '(uidNumber=3)')
        if not entries:
            log.fatal('test_dna: user1 was not updated (looking for uidNumber: 3)')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_dna: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    # test_dependency(inst, PLUGIN_AUTOMEMBER)

    ############################################################################
    # Cleanup
    ############################################################################

    try:
        topology_st.standalone.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to delete test entry1: ' + e.message['desc'])
        assert False

    topology_st.standalone.plugins.disable(name=PLUGIN_DNA)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
