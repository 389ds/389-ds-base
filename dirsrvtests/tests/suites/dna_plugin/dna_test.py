# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

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

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Delete each instance in the end
    def fin():
        # This is useful for analysing the test env.
        standalone.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], excludeSuffixes=[], encrypt=False, \
            repl_data=True, outputfile='%s/ldif/%s.ldif' % (standalone.dbdir,SERVERID_STANDALONE ))
        standalone.clearBackupFS()
        standalone.backupFS()
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_dna_init(topology):
    '''
    Write any test suite initialization here(if needed)
    '''

    return


def test_dna_(topology):
    '''
    Write a single test here...
    '''

    # stop the plugin, and start it
    topology.standalone.plugins.disable(name=PLUGIN_DNA)
    topology.standalone.plugins.enable(name=PLUGIN_DNA)

    CONFIG_DN = 'cn=config,cn=' + PLUGIN_DNA + ',cn=plugins,cn=config'

    log.info('Testing ' + PLUGIN_DNA + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    try:
        topology.standalone.add_s(Entry((CONFIG_DN, {
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
            topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'dnaNextValue', '1'),
                                      (ldap.MOD_REPLACE, 'dnaMagicRegen', '-1')])
        except ldap.LDAPError as e:
            log.fatal('test_dna: Failed to set the DNA plugin: error ' + e.message['desc'])
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to add config entry: error ' + e.message['desc'])
        assert False

    # Do we need to restart for the plugin?

    topology.standalone.restart()

    ############################################################################
    # Test plugin
    ############################################################################

    try:
        topology.standalone.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to user1: error ' + e.message['desc'])
        assert False

    # See if the entry now has the new uidNumber assignment - uidNumber=1
    try:
        entries = topology.standalone.search_s(USER1_DN, ldap.SCOPE_BASE, '(uidNumber=1)')
        if not entries:
            log.fatal('test_dna: user1 was not updated - (looking for uidNumber: 1)')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_dna: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Test the magic regen value
    try:
        topology.standalone.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'uidNumber', '-1')])
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to set the magic reg value: error ' + e.message['desc'])
        assert False

    # See if the entry now has the new uidNumber assignment - uidNumber=2
    try:
        entries = topology.standalone.search_s(USER1_DN, ldap.SCOPE_BASE, '(uidNumber=2)')
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
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'dnaMagicRegen', '-2')])
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to set the magic reg value to -2: error ' + e.message['desc'])
        assert False

    ################################################################################
    # Test plugin
    ################################################################################

    # Test the magic regen value
    try:
        topology.standalone.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'uidNumber', '-2')])
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to set the magic reg value: error ' + e.message['desc'])
        assert False

    # See if the entry now has the new uidNumber assignment - uidNumber=3
    try:
        entries = topology.standalone.search_s(USER1_DN, ldap.SCOPE_BASE, '(uidNumber=3)')
        if not entries:
            log.fatal('test_dna: user1 was not updated (looking for uidNumber: 3)')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_dna: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    #test_dependency(inst, PLUGIN_AUTOMEMBER)

    ############################################################################
    # Cleanup
    ############################################################################

    try:
        topology.standalone.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to delete test entry1: ' + e.message['desc'])
        assert False

    topology.standalone.plugins.disable(name=PLUGIN_DNA)

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_dna: PASS\n')

    return


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
