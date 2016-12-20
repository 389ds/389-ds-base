# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
'''
Created on Dec 09, 2014

@author: mreynolds
'''
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

'''
   Functional tests for each plugin

   Test:
         plugin restarts (test when on and off)
         plugin config validation
         plugin dependencies
         plugin functionality (including plugin tasks)
'''


################################################################################
#
# Test Plugin Dependency
#
################################################################################
def test_dependency(inst, plugin):
    """
    Set the "account usabilty" plugin to depend on this plugin.  This plugin
    is generic, always enabled, and perfect for our testing
    """

    try:
        inst.modify_s('cn=' + PLUGIN_ACCT_USABILITY + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'nsslapd-plugin-depends-on-named', plugin)])

    except ldap.LDAPError as e:
        log.fatal('test_dependency: Failed to modify ' + PLUGIN_ACCT_USABILITY + ': error ' + e.message['desc'])
        assert False

    try:
        inst.modify_s('cn=' + plugin + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'nsslapd-pluginenabled', 'off')])

    except ldap.UNWILLING_TO_PERFORM:
        # failed as expected
        pass
    else:
        # Incorrectly succeeded
        log.fatal('test_dependency: Plugin dependency check failed (%s)' % plugin)
        assert False

    # Now undo the change
    try:
        inst.modify_s('cn=' + PLUGIN_ACCT_USABILITY + ',cn=plugins,cn=config',
                      [(ldap.MOD_DELETE, 'nsslapd-plugin-depends-on-named', None)])
    except ldap.LDAPError as e:
        log.fatal('test_dependency: Failed to reset ' + plugin + ': error ' + e.message['desc'])
        assert False


################################################################################
#
# Wait for task to complete
#
################################################################################
def wait_for_task(conn, task_dn):
    finished = False
    exitcode = 0
    count = 0
    while count < 60:
        try:
            task_entry = conn.search_s(task_dn, ldap.SCOPE_BASE, 'objectclass=*')
            if not task_entry:
                log.fatal('wait_for_task: Search failed to find task: ' + task_dn)
                assert False
            if task_entry[0].hasAttr('nstaskexitcode'):
                # task is done
                exitcode = task_entry[0].nsTaskExitCode
                finished = True
                break
        except ldap.LDAPError as e:
            log.fatal('wait_for_task: Search failed: ' + e.message['desc'])
            assert False

        time.sleep(1)
        count += 1
    if not finished:
        log.fatal('wait_for_task: Task (%s) did not complete!' % task_dn)
        assert False

    return exitcode


################################################################################
#
# Test Account Policy Plugin (0)
#
################################################################################
def test_acctpolicy(inst, args=None):
    # stop the plugin, and start it
    inst.plugins.disable(name=PLUGIN_ACCT_POLICY)
    inst.plugins.enable(name=PLUGIN_ACCT_POLICY)

    if args == "restart":
        return True

    CONFIG_DN = 'cn=config,cn=Account Policy Plugin,cn=plugins,cn=config'

    log.info('Testing ' + PLUGIN_ACCT_POLICY + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    # Add the config entry
    try:
        inst.add_s(Entry((CONFIG_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'cn': 'config',
                          'alwaysrecordlogin': 'yes',
                          'stateattrname': 'lastLoginTime'
                          })))
    except ldap.ALREADY_EXISTS:
        try:
            inst.modify_s(CONFIG_DN,
                      [(ldap.MOD_REPLACE, 'alwaysrecordlogin', 'yes'),
                       (ldap.MOD_REPLACE, 'stateattrname', 'lastLoginTime')])
        except ldap.LDAPError as e:
            log.fatal('test_acctpolicy: Failed to modify config entry: error ' + e.message['desc'])
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_acctpolicy: Failed to add config entry: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Add an entry
    time.sleep(1)
    try:
        inst.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                 'sn': '1',
                                 'cn': 'user 1',
                                 'uid': 'user1',
                                 'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.fatal('test_acctpolicy: Failed to add test user' + USER1_DN + ': error ' + e.message['desc'])
        assert False

    # bind as user
    try:
        inst.simple_bind_s(USER1_DN, "password")
    except ldap.LDAPError as e:
        log.fatal('test_acctpolicy: Failed to bind as user1: ' + e.message['desc'])
        assert False

    # Bind as Root DN
    time.sleep(1)
    try:
        inst.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_acctpolicy: Failed to bind as rootDN: ' + e.message['desc'])
        assert False

    # Check lastLoginTime of USER1
    try:
        entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'lastLoginTime=*')
        if not entries:
            log.fatal('test_acctpolicy: Search failed to find an entry with lastLoginTime.')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_acctpolicy: Search failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Change config - change the stateAttrName to a new attribute
    ############################################################################

    try:
        inst.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'stateattrname', 'testLastLoginTime')])

    except ldap.LDAPError as e:
        log.fatal('test_acctpolicy: Failed to modify config entry: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    time.sleep(1)
    # login as user
    try:
        inst.simple_bind_s(USER1_DN, "password")
    except ldap.LDAPError as e:
        log.fatal('test_acctpolicy: Failed to bind(2nd) as user1: ' + e.message['desc'])
        assert False

    time.sleep(1)
    # Bind as Root DN
    try:
        inst.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_acctpolicy: Failed to bind as rootDN: ' + e.message['desc'])
        assert False

    # Check testLastLoginTime was added to USER1
    try:
        entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(testLastLoginTime=*)')
        if not entries:
            log.fatal('test_acctpolicy: Search failed to find an entry with testLastLoginTime.')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_acctpolicy: Search failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    test_dependency(inst, PLUGIN_ACCT_POLICY)

    ############################################################################
    # Cleanup
    ############################################################################

    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_acctpolicy: Failed to delete test entry: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_acctpolicy: PASS\n')

    return


################################################################################
#
# Test Attribute Uniqueness Plugin (1)
#
################################################################################
def test_attruniq(inst, args=None):
    # stop the plugin, and start it
    inst.plugins.disable(name=PLUGIN_ATTR_UNIQUENESS)
    inst.plugins.enable(name=PLUGIN_ATTR_UNIQUENESS)

    if args == "restart":
        return

    log.info('Testing ' + PLUGIN_ATTR_UNIQUENESS + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    try:
        inst.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', 'uid')])

    except ldap.LDAPError as e:
        log.fatal('test_attruniq: Failed to configure plugin for "uid": error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Add an entry
    try:
        inst.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                     'sn': '1',
                                     'cn': 'user 1',
                                     'uid': 'user1',
                                     'mail': 'user1@example.com',
                                     'mailAlternateAddress' : 'user1@alt.example.com',
                                     'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.fatal('test_attruniq: Failed to add test user' + USER1_DN + ': error ' + e.message['desc'])
        assert False

    # Add an entry with a duplicate "uid"
    try:
        inst.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                     'sn': '2',
                                     'cn': 'user 2',
                                     'uid': 'user2',
                                     'uid': 'user1',
                                     'userpassword': 'password'})))

    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.fatal('test_attruniq: Adding of 2nd entry(uid) incorrectly succeeded')
        assert False

    ############################################################################
    # Change config to use "mail" instead of "uid"
    ############################################################################

    try:
        inst.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', 'mail')])

    except ldap.LDAPError as e:
        log.fatal('test_attruniq: Failed to configure plugin for "mail": error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin - Add an entry, that has a duplicate "mail" value
    ############################################################################

    try:
        inst.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                 'sn': '2',
                                 'cn': 'user 2',
                                 'uid': 'user2',
                                 'mail': 'user1@example.com',
                                 'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.fatal('test_attruniq: Adding of 2nd entry(mail) incorrectly succeeded')
        assert False

    ############################################################################
    # Reconfigure plugin for mail and mailAlternateAddress
    ############################################################################

    try:
        inst.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', 'mail'),
                       (ldap.MOD_ADD, 'uniqueness-attribute-name',
                        'mailAlternateAddress')])

    except ldap.LDAPError as e:
        log.error('test_attruniq: Failed to reconfigure plugin for "mail mailAlternateAddress": error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin - Add an entry, that has a duplicate "mail" value
    ############################################################################

    try:
        inst.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                 'sn': '2',
                                 'cn': 'user 2',
                                 'uid': 'user2',
                                 'mail': 'user1@example.com',
                                 'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_attruniq: Adding of 3rd entry(mail) incorrectly succeeded')
        assert False

    ############################################################################
    # Test plugin - Add an entry, that has a duplicate "mailAlternateAddress" value
    ############################################################################

    try:
        inst.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                 'sn': '2',
                                 'cn': 'user 2',
                                 'uid': 'user2',
                                 'mailAlternateAddress': 'user1@alt.example.com',
                                 'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_attruniq: Adding of 4th entry(mailAlternateAddress) incorrectly succeeded')
        assert False

    ############################################################################
    # Test plugin - Add an entry, that has a duplicate "mail" value conflicting mailAlternateAddress
    ############################################################################

    try:
        inst.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                 'sn': '2',
                                 'cn': 'user 2',
                                 'uid': 'user2',
                                 'mail': 'user1@alt.example.com',
                                 'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_attruniq: Adding of 5th entry(mailAlternateAddress) incorrectly succeeded')
        assert False

    ############################################################################
    # Test plugin - Add an entry, that has a duplicate "mailAlternateAddress" conflicting mail
    ############################################################################

    try:
        inst.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                 'sn': '2',
                                 'cn': 'user 2',
                                 'uid': 'user2',
                                 'mailAlternateAddress': 'user1@example.com',
                                 'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_attruniq: Adding of 6th entry(mail) incorrectly succeeded')
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    test_dependency(inst, PLUGIN_ATTR_UNIQUENESS)

    ############################################################################
    # Cleanup
    ############################################################################

    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_attruniq: Failed to delete test entry: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_attruniq: PASS\n')
    return


################################################################################
#
# Test Auto Membership Plugin (2)
#
################################################################################
def test_automember(inst, args=None):
    # stop the plugin, and start it
    inst.plugins.disable(name=PLUGIN_AUTOMEMBER)
    inst.plugins.enable(name=PLUGIN_AUTOMEMBER)

    if args == "restart":
        return

    CONFIG_DN = 'cn=config,cn=' + PLUGIN_AUTOMEMBER + ',cn=plugins,cn=config'

    log.info('Testing ' + PLUGIN_AUTOMEMBER + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    # Add the automember group
    try:
        inst.add_s(Entry((GROUP_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'cn': 'group'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to add group: error ' + e.message['desc'])
        assert False

    # Add ou=branch1
    try:
        inst.add_s(Entry((BRANCH1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'ou': 'branch1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to add branch1: error ' + e.message['desc'])
        assert False

    # Add ou=branch2
    try:
        inst.add_s(Entry((BRANCH2_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'ou': 'branch2'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to add branch2: error ' + e.message['desc'])
        assert False

    # Add the automember config entry
    try:
        inst.add_s(Entry((CONFIG_DN, {
                          'objectclass': 'top autoMemberDefinition'.split(),
                          'cn': 'config',
                          'autoMemberScope': 'ou=branch1,' + DEFAULT_SUFFIX,
                          'autoMemberFilter': 'objectclass=top',
                          'autoMemberDefaultGroup': 'cn=group,' + DEFAULT_SUFFIX,
                          'autoMemberGroupingAttr': 'member:dn'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to add config entry: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test the plugin
    ############################################################################

    # Add a user that should get added to the group
    try:
        inst.add_s(Entry((BUSER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to add user: error ' + e.message['desc'])
        assert False

    # Check the group
    try:
        entries = inst.search_s(GROUP_DN, ldap.SCOPE_BASE,
                                '(member=' + BUSER1_DN + ')')
        if not entries:
            log.fatal('test_automember: Search failed to find member user1')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_automember: Search failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Change config
    ############################################################################

    try:
        inst.modify_s(CONFIG_DN,
                      [(ldap.MOD_REPLACE, 'autoMemberGroupingAttr', 'uniquemember:dn'),
                       (ldap.MOD_REPLACE, 'autoMemberScope', 'ou=branch2,' + DEFAULT_SUFFIX)])

    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to modify config entry: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Add a user that should get added to the group
    try:
        inst.add_s(Entry((BUSER2_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user2'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to user to branch2: error ' + e.message['desc'])
        assert False

    # Check the group
    try:
        entries = inst.search_s(GROUP_DN, ldap.SCOPE_BASE,
                                '(uniquemember=' + BUSER2_DN + ')')
        if not entries:
            log.fatal('test_automember: Search failed to find uniquemember user2')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_automember: Search failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test Task
    ############################################################################

    # Disable plugin
    inst.plugins.disable(name=PLUGIN_AUTOMEMBER)

    # Add an entry that should be picked up by automember - verify it is not(yet)
    try:
        inst.add_s(Entry((BUSER3_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user3'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to user3 to branch2: error ' + e.message['desc'])
        assert False

    # Check the group - uniquemember should not exist
    try:
        entries = inst.search_s(GROUP_DN, ldap.SCOPE_BASE,
                                '(uniquemember=' + BUSER3_DN + ')')
        if entries:
            log.fatal('test_automember: user3 was incorrectly added to the group')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_automember: Search failed: ' + e.message['desc'])
        assert False

    # Enable plugin
    inst.plugins.enable(name=PLUGIN_AUTOMEMBER)

    TASK_DN = 'cn=task-' + str(int(time.time())) + ',cn=automember rebuild membership,cn=tasks,cn=config'
    # Add the task
    try:
        inst.add_s(Entry((TASK_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'basedn': 'ou=branch2,' + DEFAULT_SUFFIX,
                          'filter': 'objectclass=top'})))
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to add task: error ' + e.message['desc'])
        assert False

    wait_for_task(inst, TASK_DN)

    # Verify the fixup task worked
    try:
        entries = inst.search_s(GROUP_DN, ldap.SCOPE_BASE,
                                '(uniquemember=' + BUSER3_DN + ')')
        if not entries:
            log.fatal('test_automember: user3 was not added to the group')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_automember: Search failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    test_dependency(inst, PLUGIN_AUTOMEMBER)

    ############################################################################
    # Cleanup
    ############################################################################

    try:
        inst.delete_s(BUSER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to delete test entry1: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(BUSER2_DN)
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to delete test entry2: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(BUSER3_DN)
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to delete test entry3: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(BRANCH1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to delete branch1: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(BRANCH2_DN)
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to delete test branch2: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(GROUP_DN)
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to delete test group: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(CONFIG_DN)
    except ldap.LDAPError as e:
        log.fatal('test_automember: Failed to delete plugin config entry: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_automember: PASS\n')
    return


################################################################################
#
# Test DNA Plugin (3)
#
################################################################################
def test_dna(inst, args=None):
    # stop the plugin, and start it
    inst.plugins.disable(name=PLUGIN_DNA)
    inst.plugins.enable(name=PLUGIN_DNA)

    if args == "restart":
        return

    CONFIG_DN = 'cn=config,cn=' + PLUGIN_DNA + ',cn=plugins,cn=config'

    log.info('Testing ' + PLUGIN_DNA + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    try:
        inst.add_s(Entry((CONFIG_DN, {
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
            inst.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'dnaNextValue', '1'),
                                      (ldap.MOD_REPLACE, 'dnaMagicRegen', '-1')])
        except ldap.LDAPError as e:
            log.fatal('test_dna: Failed to set the DNA plugin: error ' + e.message['desc'])
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to add config entry: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    try:
        inst.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to user1: error ' + e.message['desc'])
        assert False

    # See if the entry now has the new uidNumber assignment - uidNumber=1
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(uidNumber=1)')
        if not entries:
            log.fatal('test_dna: user1 was not updated - (looking for uidNumber: 1)')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_dna: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Test the magic regen value
    try:
        inst.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'uidNumber', '-1')])
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to set the magic reg value: error ' + e.message['desc'])
        assert False

    # See if the entry now has the new uidNumber assignment - uidNumber=2
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(uidNumber=2)')
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
        inst.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'dnaMagicRegen', '-2')])
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to set the magic reg value to -2: error ' + e.message['desc'])
        assert False

    ################################################################################
    # Test plugin
    ################################################################################

    # Test the magic regen value
    try:
        inst.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'uidNumber', '-2')])
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to set the magic reg value: error ' + e.message['desc'])
        assert False

    # See if the entry now has the new uidNumber assignment - uidNumber=3
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(uidNumber=3)')
        if not entries:
            log.fatal('test_dna: user1 was not updated (looking for uidNumber: 3)')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_dna: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    test_dependency(inst, PLUGIN_AUTOMEMBER)

    ############################################################################
    # Cleanup
    ############################################################################

    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_dna: Failed to delete test entry1: ' + e.message['desc'])
        assert False

    inst.plugins.disable(name=PLUGIN_DNA)

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_dna: PASS\n')

    return


################################################################################
#
# Test Linked Attrs Plugin (4)
#
################################################################################
def test_linkedattrs(inst, args=None):
    # stop the plugin, and start it
    inst.plugins.disable(name=PLUGIN_LINKED_ATTRS)
    inst.plugins.enable(name=PLUGIN_LINKED_ATTRS)

    if args == "restart":
        return

    CONFIG_DN = 'cn=config,cn=' + PLUGIN_LINKED_ATTRS + ',cn=plugins,cn=config'

    log.info('Testing ' + PLUGIN_LINKED_ATTRS + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    # Add test entries
    try:
        inst.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to user1: error ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((USER2_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user2'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to user1: error ' + e.message['desc'])
        assert False

    # Add the linked attrs config entry
    try:
        inst.add_s(Entry((CONFIG_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'cn': 'config',
                          'linkType': 'directReport',
                          'managedType': 'manager'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to add config entry: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Set "directReport" should add "manager" to the other entry
    try:
        inst.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'directReport', USER2_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to add "directReport" to user1: error ' + e.message['desc'])
        assert False

    # See if manager was added to the other entry
    try:
        entries = inst.search_s(USER2_DN, ldap.SCOPE_BASE, '(manager=*)')
        if not entries:
            log.fatal('test_linkedattrs: user2 missing "manager" attribute')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Remove "directReport" should remove "manager" to the other entry
    try:
        inst.modify_s(USER1_DN, [(ldap.MOD_DELETE, 'directReport', None)])
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to delete directReport: error ' + e.message['desc'])
        assert False

    # See if manager was removed
    try:
        entries = inst.search_s(USER2_DN, ldap.SCOPE_BASE, '(manager=*)')
        if entries:
            log.fatal('test_linkedattrs: user2 "manager" attribute not removed')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Change the config - using linkType "indirectReport" now
    ############################################################################

    try:
        inst.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'linkType', 'indirectReport')])
    except ldap.LDAPError as e:
        log.error('test_linkedattrs: Failed to set linkTypee: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Make sure the old linkType(directManager) is not working
    try:
        inst.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'directReport', USER2_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to add "directReport" to user1: error ' + e.message['desc'])
        assert False

    # See if manager was added to the other entry, better not be...
    try:
        entries = inst.search_s(USER2_DN, ldap.SCOPE_BASE, '(manager=*)')
        if entries:
            log.fatal('test_linkedattrs: user2 had "manager" added unexpectedly')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Search for user2 failed: ' + e.message['desc'])
        assert False

    # Now, set the new linkType "indirectReport", which should add "manager" to the other entry
    try:
        inst.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'indirectReport', USER2_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to add "indirectReport" to user1: error ' + e.message['desc'])
        assert False

    # See if manager was added to the other entry, better not be
    try:
        entries = inst.search_s(USER2_DN, ldap.SCOPE_BASE, '(manager=*)')
        if not entries:
            log.fatal('test_linkedattrs: user2 missing "manager"')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Search for user2 failed: ' + e.message['desc'])
        assert False

    # Remove "indirectReport" should remove "manager" to the other entry
    try:
        inst.modify_s(USER1_DN, [(ldap.MOD_DELETE, 'indirectReport', None)])
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to delete directReport: error ' + e.message['desc'])
        assert False

    # See if manager was removed
    try:
        entries = inst.search_s(USER2_DN, ldap.SCOPE_BASE, '(manager=*)')
        if entries:
            log.fatal('test_linkedattrs: user2 "manager" attribute not removed')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test Fixup Task
    ############################################################################

    # Disable plugin and make some updates that would of triggered the plugin
    inst.plugins.disable(name=PLUGIN_LINKED_ATTRS)

    try:
        inst.modify_s(USER1_DN, [(ldap.MOD_REPLACE, 'indirectReport', USER2_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to add "indirectReport" to user1: error ' + e.message['desc'])
        assert False

    # The entry should not have a manager attribute
    try:
        entries = inst.search_s(USER2_DN, ldap.SCOPE_BASE, '(manager=*)')
        if entries:
            log.fatal('test_linkedattrs: user2 incorrectly has a "manager" attr')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Enable the plugin and rerun the task entry
    inst.plugins.enable(name=PLUGIN_LINKED_ATTRS)

    # Add the task again
    TASK_DN = 'cn=task-' + str(int(time.time())) + ',cn=fixup linked attributes,cn=tasks,cn=config'
    try:
        inst.add_s(Entry(('cn=task-' + str(int(time.time())) + ',cn=fixup linked attributes,cn=tasks,cn=config', {
                          'objectclass': 'top extensibleObject'.split(),
                          'basedn': DEFAULT_SUFFIX,
                          'filter': 'objectclass=top'})))
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to add task: error ' + e.message['desc'])
        assert False

    wait_for_task(inst, TASK_DN)

    # Check if user2 now has a manager attribute now
    try:
        entries = inst.search_s(USER2_DN, ldap.SCOPE_BASE, '(manager=*)')
        if not entries:
            log.fatal('test_linkedattrs: task failed: user2 missing "manager" attr')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    test_dependency(inst, PLUGIN_LINKED_ATTRS)

    ############################################################################
    # Cleanup
    ############################################################################

    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to delete test entry1: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(USER2_DN)
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to delete test entry2: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(CONFIG_DN)
    except ldap.LDAPError as e:
        log.fatal('test_linkedattrs: Failed to delete plugin config entry: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_linkedattrs: PASS\n')
    return


################################################################################
#
# Test MemberOf Plugin (5)
#
################################################################################
def test_memberof(inst, args=None):
    # stop the plugin, and start it
    inst.plugins.disable(name=PLUGIN_MEMBER_OF)
    inst.plugins.enable(name=PLUGIN_MEMBER_OF)

    if args == "restart":
        return

    PLUGIN_DN = 'cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config'
    SHARED_CONFIG_DN = 'cn=memberOf Config,' + DEFAULT_SUFFIX

    log.info('Testing ' + PLUGIN_MEMBER_OF + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'memberofgroupattr', 'member')])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to update config(member): error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Add our test entries
    try:
        inst.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add user1: error ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((GROUP_DN, {
                          'objectclass': 'top groupOfNames groupOfUniqueNames extensibleObject'.split(),
                          'cn': 'group',
                          'member': USER1_DN
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add group: error ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((SHARED_CONFIG_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'memberofgroupattr': 'member',
                          'memberofattr': 'memberof'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to shared config entry: error ' + e.message['desc'])
        assert False

    # Check if the user now has a "memberOf" attribute
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if not entries:
            log.fatal('test_memberof: user1 missing memberOf')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Remove "member" should remove "memberOf" from the entry
    try:
        inst.modify_s(GROUP_DN, [(ldap.MOD_DELETE, 'member', None)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to delete member: error ' + e.message['desc'])
        assert False

    # Check that "memberOf" was removed
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if entries:
            log.fatal('test_memberof: user1 incorrectly has memberOf attr')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Change the config
    ############################################################################

    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'memberofgroupattr', 'uniquemember')])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to update config(uniquemember): error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    try:
        inst.modify_s(GROUP_DN, [(ldap.MOD_REPLACE, 'uniquemember', USER1_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add uniquemember: error ' + e.message['desc'])
        assert False

    # Check if the user now has a "memberOf" attribute
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if not entries:
            log.fatal('test_memberof: user1 missing memberOf')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Remove "uniquemember" should remove "memberOf" from the entry
    try:
        inst.modify_s(GROUP_DN, [(ldap.MOD_DELETE, 'uniquemember', None)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to delete member: error ' + e.message['desc'])
        assert False

    # Check that "memberOf" was removed
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if entries:
            log.fatal('test_memberof: user1 incorrectly has memberOf attr')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Set the shared config entry and test the plugin
    ############################################################################

    # The shared config entry uses "member" - the above test uses "uniquemember"
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, CONFIG_AREA, SHARED_CONFIG_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to set plugin area: error ' + e.message['desc'])
        assert False

    # Delete the test entries then readd them to start with a clean slate
    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to delete test entry1: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(GROUP_DN)
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to delete test group: ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add user1: error ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((GROUP_DN, {
                          'objectclass': 'top groupOfNames groupOfUniqueNames extensibleObject'.split(),
                          'cn': 'group',
                          'member': USER1_DN
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add group: error ' + e.message['desc'])
        assert False

    # Test the shared config
    # Check if the user now has a "memberOf" attribute
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if not entries:
            log.fatal('test_memberof: user1 missing memberOf')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Remove "member" should remove "memberOf" from the entry
    try:
        inst.modify_s(GROUP_DN, [(ldap.MOD_DELETE, 'member', None)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to delete member: error ' + e.message['desc'])
        assert False

    # Check that "memberOf" was removed
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if entries:
            log.fatal('test_memberof: user1 incorrectly has memberOf attr')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Change the shared config entry to use 'uniquemember' and test the plugin
    ############################################################################

    try:
        inst.modify_s(SHARED_CONFIG_DN, [(ldap.MOD_REPLACE, 'memberofgroupattr', 'uniquemember')])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to set shared plugin entry(uniquemember): error '
            + e.message['desc'])
        assert False

    try:
        inst.modify_s(GROUP_DN, [(ldap.MOD_REPLACE, 'uniquemember', USER1_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add uniquemember: error ' + e.message['desc'])
        assert False

    # Check if the user now has a "memberOf" attribute
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if not entries:
            log.fatal('test_memberof: user1 missing memberOf')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Remove "uniquemember" should remove "memberOf" from the entry
    try:
        inst.modify_s(GROUP_DN, [(ldap.MOD_DELETE, 'uniquemember', None)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to delete member: error ' + e.message['desc'])
        assert False

    # Check that "memberOf" was removed
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if entries:
            log.fatal('test_memberof: user1 incorrectly has memberOf attr')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Remove shared config from plugin, and retest
    ############################################################################

    # First change the plugin to use member before we move the shared config that uses uniquemember
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'memberofgroupattr', 'member')])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to update config(uniquemember): error ' + e.message['desc'])
        assert False

    # Remove shared config from plugin
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, CONFIG_AREA, None)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add uniquemember: error ' + e.message['desc'])
        assert False

    try:
        inst.modify_s(GROUP_DN, [(ldap.MOD_REPLACE, 'member', USER1_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add uniquemember: error ' + e.message['desc'])
        assert False

    # Check if the user now has a "memberOf" attribute
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if not entries:
            log.fatal('test_memberof: user1 missing memberOf')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Remove "uniquemember" should remove "memberOf" from the entry
    try:
        inst.modify_s(GROUP_DN, [(ldap.MOD_DELETE, 'member', None)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to delete member: error ' + e.message['desc'])
        assert False

    # Check that "memberOf" was removed
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if entries:
            log.fatal('test_memberof: user1 incorrectly has memberOf attr')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test Fixup Task
    ############################################################################

    inst.plugins.disable(name=PLUGIN_MEMBER_OF)

    # First change the plugin to use uniquemember
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'memberofgroupattr', 'uniquemember')])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to update config(uniquemember): error ' + e.message['desc'])
        assert False

    # Add uniquemember, should not update USER1
    try:
        inst.modify_s(GROUP_DN, [(ldap.MOD_REPLACE, 'uniquemember', USER1_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add uniquemember: error ' + e.message['desc'])
        assert False

    # Check for "memberOf"
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if entries:
            log.fatal('test_memberof: user1 incorrect has memberOf attr')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    # Enable memberof plugin
    inst.plugins.enable(name=PLUGIN_MEMBER_OF)

    #############################################################
    # Test memberOf fixup arg validation:  Test the DN and filter
    #############################################################

    #
    # Test bad/nonexistant DN
    #
    TASK_DN = 'cn=task-' + str(int(time.time())) + ',' + DN_MBO_TASK
    try:
        inst.add_s(Entry((TASK_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'basedn': DEFAULT_SUFFIX + "bad",
                          'filter': 'objectclass=top'})))
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add task(bad dn): error ' +
                  e.message['desc'])
        assert False

    exitcode = wait_for_task(inst, TASK_DN)
    if exitcode == "0":
        # We should an error
        log.fatal('test_memberof: Task with invalid DN still reported success')
        assert False

    #
    # Test invalid DN syntax
    #
    TASK_DN = 'cn=task-' + str(int(time.time())) + ',' + DN_MBO_TASK
    try:
        inst.add_s(Entry((TASK_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'basedn': "bad",
                          'filter': 'objectclass=top'})))
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add task(invalid dn syntax): ' +
                  e.message['desc'])
        assert False

    exitcode = wait_for_task(inst, TASK_DN)
    if exitcode == "0":
        # We should an error
        log.fatal('test_memberof: Task with invalid DN syntax still reported' +
                  ' success')
        assert False

    #
    # Test bad filter (missing closing parenthesis)
    #
    TASK_DN = 'cn=task-' + str(int(time.time())) + ',' + DN_MBO_TASK
    try:
        inst.add_s(Entry((TASK_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'basedn': DEFAULT_SUFFIX,
                          'filter': '(objectclass=top'})))
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add task(bad filter: error ' +
                  e.message['desc'])
        assert False

    exitcode = wait_for_task(inst, TASK_DN)
    if exitcode == "0":
        # We should an error
        log.fatal('test_memberof: Task with invalid filter still reported ' +
                  'success')
        assert False

    ####################################################
    # Test fixup works
    ####################################################

    #
    # Run the task and validate that it worked
    #
    TASK_DN = 'cn=task-' + str(int(time.time())) + ',' + DN_MBO_TASK
    try:
        inst.add_s(Entry((TASK_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'basedn': DEFAULT_SUFFIX,
                          'filter': 'objectclass=top'})))
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to add task: error ' + e.message['desc'])
        assert False

    wait_for_task(inst, TASK_DN)

    # Check for "memberOf"
    try:
        entries = inst.search_s(USER1_DN, ldap.SCOPE_BASE, '(memberOf=*)')
        if not entries:
            log.fatal('test_memberof: user1 missing memberOf attr')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Search for user1 failed: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    test_dependency(inst, PLUGIN_MEMBER_OF)

    ############################################################################
    # Cleanup
    ############################################################################

    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to delete test entry1: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(GROUP_DN)
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to delete test group: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(SHARED_CONFIG_DN)
    except ldap.LDAPError as e:
        log.fatal('test_memberof: Failed to delete shared config entry: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_memberof: PASS\n')

    return


################################################################################
#
# Test Managed Entry Plugin (6)
#
################################################################################
def test_mep(inst, args=None):
    # stop the plugin, and start it
    inst.plugins.disable(name=PLUGIN_MANAGED_ENTRY)
    inst.plugins.enable(name=PLUGIN_MANAGED_ENTRY)

    if args == "restart":
        return

    USER_DN = 'uid=user1,ou=people,' + DEFAULT_SUFFIX
    MEP_USER_DN = 'cn=user1,ou=groups,' + DEFAULT_SUFFIX
    USER_DN2 = 'uid=user 1,ou=people,' + DEFAULT_SUFFIX
    MEP_USER_DN2 = 'uid=user 1,ou=groups,' + DEFAULT_SUFFIX
    CONFIG_DN = 'cn=config,cn=' + PLUGIN_MANAGED_ENTRY + ',cn=plugins,cn=config'
    TEMPLATE_DN = 'cn=MEP Template,' + DEFAULT_SUFFIX
    TEMPLATE_DN2 = 'cn=MEP Template2,' + DEFAULT_SUFFIX

    log.info('Testing ' + PLUGIN_MANAGED_ENTRY + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    # Add our org units
    try:
        inst.add_s(Entry((PEOPLE_OU, {
                   'objectclass': 'top extensibleObject'.split(),
                   'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add people org unit: error ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((GROUP_OU, {
                   'objectclass': 'top extensibleObject'.split(),
                   'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add people org unit: error ' + e.message['desc'])
        assert False

    # Add the template entry
    try:
        inst.add_s(Entry((TEMPLATE_DN, {
                   'objectclass': 'top mepTemplateEntry extensibleObject'.split(),
                   'cn': 'MEP Template',
                   'mepRDNAttr': 'cn',
                   'mepStaticAttr': 'objectclass: posixGroup|objectclass: extensibleObject'.split('|'),
                   'mepMappedAttr': 'cn: $cn|uid: $cn|gidNumber: $uidNumber'.split('|')
                   })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add template entry: error ' + e.message['desc'])
        assert False

    # Add the config entry
    try:
        inst.add_s(Entry((CONFIG_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'cn': 'config',
                          'originScope': PEOPLE_OU,
                          'originFilter': 'objectclass=posixAccount',
                          'managedBase': GROUP_OU,
                          'managedTemplate': TEMPLATE_DN
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add config entry: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Add an entry that meets the MEP scope
    try:
        inst.add_s(Entry((USER_DN, {
                          'objectclass': 'top posixAccount extensibleObject'.split(),
                          'uid': 'user1',
                          'cn': 'user1',
                          'uidNumber': '1',
                          'gidNumber': '1',
                          'homeDirectory': '/home/user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to user1: error ' + e.message['desc'])
        assert False

    # Check if a managed group entry was created
    try:
        inst.search_s(MEP_USER_DN, ldap.SCOPE_BASE, '(objectclass=top)')
    except ldap.LDAPError as e:
        log.fatal('test_mep: Unable to find MEP entry: ' + e.message['desc'])
        assert False

    ############################################################################
    # Change the config
    ############################################################################

    # Add a new template entry
    try:
        inst.add_s(Entry((TEMPLATE_DN2, {
                   'objectclass': 'top mepTemplateEntry extensibleObject'.split(),
                   'cn': 'MEP Template2',
                   'mepRDNAttr': 'uid',
                   'mepStaticAttr': 'objectclass: posixGroup|objectclass: extensibleObject'.split('|'),
                   'mepMappedAttr': 'cn: $uid|uid: $cn|gidNumber: $gidNumber'.split('|')
                   })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add template entry2: error ' + e.message['desc'])
        assert False

    # Set the new template dn
    try:
        inst.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'managedTemplate', TEMPLATE_DN2)])
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to set mep plugin config: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Add an entry that meets the MEP scope
    try:
        inst.add_s(Entry((USER_DN2, {
                          'objectclass': 'top posixAccount extensibleObject'.split(),
                          'uid': 'user 1',
                          'cn': 'user 1',
                          'uidNumber': '1',
                          'gidNumber': '1',
                          'homeDirectory': '/home/user2'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to user2: error ' + e.message['desc'])
        assert False

    # Check if a managed group entry was created
    try:
        inst.search_s(MEP_USER_DN2, ldap.SCOPE_BASE, '(objectclass=top)')
    except ldap.LDAPError as e:
        log.fatal('test_mep: Unable to find MEP entry2: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    test_dependency(inst, PLUGIN_MANAGED_ENTRY)

    ############################################################################
    # Cleanup
    ############################################################################

    try:
        inst.delete_s(USER_DN)
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to delete test user1: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(USER_DN2)
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to delete test user 2: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(TEMPLATE_DN)
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to delete template1: ' + e.message['desc'])
        assert False

    inst.plugins.disable(name=PLUGIN_MANAGED_ENTRY)

    try:
        inst.delete_s(TEMPLATE_DN2)
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to delete template2: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(CONFIG_DN)
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to delete config: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_mep: PASS\n')
    return


################################################################################
#
# Test Passthru Plugin (7)
#
################################################################################
def test_passthru(inst, args=None):
    # Passthru is a bit picky about the state of the entry - we can't just restart it
    if args == "restart":
        return

    # stop the plugin
    inst.plugins.disable(name=PLUGIN_PASSTHRU)

    PLUGIN_DN = 'cn=' + PLUGIN_PASSTHRU + ',cn=plugins,cn=config'
    PASSTHRU_DN = 'uid=admin,dc=pass,dc=thru'
    PASSTHRU_DN2 = 'uid=admin2,dc=pass2,dc=thru'
    PASS_SUFFIX1 = 'dc=pass,dc=thru'
    PASS_SUFFIX2 = 'dc=pass2,dc=thru'
    PASS_BE2 = 'PASS2'

    log.info('Testing ' + PLUGIN_PASSTHRU + '...')

    ############################################################################
    # Add a new "remote" instance, and a user for auth
    ############################################################################

    # Create second instance
    passthru_inst = DirSrv(verbose=False)

    # Args for the instance
    args_instance[SER_HOST] = LOCALHOST
    args_instance[SER_PORT] = 33333
    args_instance[SER_SERVERID_PROP] = 'passthru'
    args_instance[SER_CREATION_SUFFIX] = PASS_SUFFIX1
    args_passthru_inst = args_instance.copy()
    passthru_inst.allocate(args_passthru_inst)
    passthru_inst.create()
    passthru_inst.open()

    # Create a second backend
    passthru_inst.backend.create(PASS_SUFFIX2, {BACKEND_NAME: PASS_BE2})
    passthru_inst.mappingtree.create(PASS_SUFFIX2, bename=PASS_BE2)

    # Create the top of the tree
    try:
        passthru_inst.add_s(Entry((PASS_SUFFIX2, {
                          'objectclass': 'top domain'.split(),
                          'dc': 'pass2'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.fatal('test_passthru: Failed to create suffix entry: error ' + e.message['desc'])
        passthru_inst.delete()
        assert False

    # Add user to suffix1
    try:
        passthru_inst.add_s(Entry((PASSTHRU_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'admin',
                          'userpassword': 'password'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_passthru: Failed to admin1: error ' + e.message['desc'])
        passthru_inst.delete()
        assert False

    # Add user to suffix 2
    try:
        passthru_inst.add_s(Entry((PASSTHRU_DN2, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'admin2',
                          'userpassword': 'password'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_passthru: Failed to admin2 : error ' + e.message['desc'])
        passthru_inst.delete()
        assert False

    ############################################################################
    # Configure and start plugin
    ############################################################################

    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'nsslapd-pluginenabled', 'on'),
                                  (ldap.MOD_REPLACE, 'nsslapd-pluginarg0', 'ldap://127.0.0.1:33333/dc=pass,dc=thru')])
    except ldap.LDAPError as e:
        log.fatal('test_passthru: Failed to set mep plugin config: error ' + e.message['desc'])
        passthru_inst.delete()
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # login as user
    try:
        inst.simple_bind_s(PASSTHRU_DN, "password")
    except ldap.LDAPError as e:
        log.fatal('test_passthru: pass through bind failed: ' + e.message['desc'])
        passthru_inst.delete()
        assert False

    ############################################################################
    # Change the config
    ############################################################################

    # login as root DN
    try:
        inst.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_passthru: pass through bind failed: ' + e.message['desc'])
        passthru_inst.delete()
        assert False

    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'nsslapd-pluginarg0', 'ldap://127.0.0.1:33333/dc=pass2,dc=thru')])
    except ldap.LDAPError as e:
        log.fatal('test_passthru: Failed to set mep plugin config: error ' + e.message['desc'])
        passthru_inst.delete()
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # login as user
    try:
        inst.simple_bind_s(PASSTHRU_DN2, "password")
    except ldap.LDAPError as e:
        log.fatal('test_passthru: pass through bind failed: ' + e.message['desc'])
        passthru_inst.delete()
        assert False

    # login as root DN
    try:
        inst.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_passthru: pass through bind failed: ' + e.message['desc'])
        passthru_inst.delete()
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    test_dependency(inst, PLUGIN_PASSTHRU)

    ############################################################################
    # Cleanup
    ############################################################################

    # remove the passthru instance
    passthru_inst.delete()

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_passthru: PASS\n')

    return


################################################################################
#
# Test Referential Integrity Plugin (8)
#
################################################################################
def test_referint(inst, args=None):
    # stop the plugin, and start it
    inst.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
    inst.plugins.enable(name=PLUGIN_REFER_INTEGRITY)

    if args == "restart":
        return

    log.info('Testing ' + PLUGIN_REFER_INTEGRITY + '...')
    PLUGIN_DN = 'cn=' + PLUGIN_REFER_INTEGRITY + ',cn=plugins,cn=config'
    SHARED_CONFIG_DN = 'cn=RI Config,' + DEFAULT_SUFFIX

    ############################################################################
    # Configure plugin
    ############################################################################

    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'referint-membership-attr', 'member')])
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to configure RI plugin: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Add some users and a group
    try:
        inst.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to add user1: error ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((USER2_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user2'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to add user2: error ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((GROUP_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'cn': 'group',
                          'member': USER1_DN,
                          'uniquemember': USER2_DN
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to add group: error ' + e.message['desc'])
        assert False

    # Grab the referint log file from the plugin

    try:
        entries = inst.search_s(PLUGIN_DN, ldap.SCOPE_BASE, '(objectclass=top)')
        REFERINT_LOGFILE = entries[0].getValue('referint-logfile')
    except ldap.LDAPError as e:
        log.fatal('test_referint: Unable to search plugin entry: ' + e.message['desc'])
        assert False

    # Add shared config entry
    try:
        inst.add_s(Entry((SHARED_CONFIG_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'referint-membership-attr': 'member',
                          'referint-update-delay': '0',
                          'referint-logfile': REFERINT_LOGFILE,
                          'referint-logchanges': '0'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to shared config entry: error ' + e.message['desc'])
        assert False

    # Delete a user
    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to delete user1: ' + e.message['desc'])
        assert False

    # Check for integrity
    try:
        entry = inst.search_s(GROUP_DN, ldap.SCOPE_BASE, '(member=' + USER1_DN + ')')
        if entry:
            log.fatal('test_referint: user1 was not removed from group')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_referint: Unable to search group: ' + e.message['desc'])
        assert False

    ############################################################################
    # Change the config
    ############################################################################

    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'referint-membership-attr', 'uniquemember')])
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to configure RI plugin: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Delete a user
    try:
        inst.delete_s(USER2_DN)
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to delete user1: ' + e.message['desc'])
        assert False

    # Check for integrity
    try:
        entry = inst.search_s(GROUP_DN, ldap.SCOPE_BASE, '(uniquemember=' + USER2_DN + ')')
        if entry:
            log.fatal('test_referint: user2 was not removed from group')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_referint: Unable to search group: ' + e.message['desc'])
        assert False

    ############################################################################
    # Set the shared config entry and test the plugin
    ############################################################################

    # The shared config entry uses "member" - the above test used "uniquemember"
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, CONFIG_AREA, SHARED_CONFIG_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to set plugin area: error ' + e.message['desc'])
        assert False

    # Delete the group, and readd everything
    try:
        inst.delete_s(GROUP_DN)
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to delete group: ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to add user1: error ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((USER2_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user2'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to add user2: error ' + e.message['desc'])
        assert False

    try:
        inst.add_s(Entry((GROUP_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'cn': 'group',
                          'member': USER1_DN,
                          'uniquemember': USER2_DN
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to add group: error ' + e.message['desc'])
        assert False

    # Delete a user
    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to delete user1: ' + e.message['desc'])
        assert False

    # Check for integrity
    try:
        entry = inst.search_s(GROUP_DN, ldap.SCOPE_BASE, '(member=' + USER1_DN + ')')
        if entry:
            log.fatal('test_referint: user1 was not removed from group')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_referint: Unable to search group: ' + e.message['desc'])
        assert False

    ############################################################################
    # Change the shared config entry to use 'uniquemember' and test the plugin
    ############################################################################

    try:
        inst.modify_s(SHARED_CONFIG_DN, [(ldap.MOD_REPLACE, 'referint-membership-attr', 'uniquemember')])
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to set shared plugin entry(uniquemember): error '
            + e.message['desc'])
        assert False

    # Delete a user
    try:
        inst.delete_s(USER2_DN)
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to delete user1: ' + e.message['desc'])
        assert False

    # Check for integrity
    try:
        entry = inst.search_s(GROUP_DN, ldap.SCOPE_BASE, '(uniquemember=' + USER2_DN + ')')
        if entry:
            log.fatal('test_referint: user2 was not removed from group')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_referint: Unable to search group: ' + e.message['desc'])
        assert False

    ############################################################################
    # Remove shared config from plugin, and retest
    ############################################################################

    # First change the plugin to use member before we move the shared config that uses uniquemember
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'referint-membership-attr', 'member')])
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to update config(uniquemember): error ' + e.message['desc'])
        assert False

    # Remove shared config from plugin
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, CONFIG_AREA, None)])
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to add uniquemember: error ' + e.message['desc'])
        assert False

    # Add test user
    try:
        inst.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to add user1: error ' + e.message['desc'])
        assert False

    # Add user to group
    try:
        inst.modify_s(GROUP_DN, [(ldap.MOD_REPLACE, 'member', USER1_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to add uniquemember: error ' + e.message['desc'])
        assert False

    # Delete a user
    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to delete user1: ' + e.message['desc'])
        assert False

    # Check for integrity
    try:
        entry = inst.search_s(GROUP_DN, ldap.SCOPE_BASE, '(member=' + USER1_DN + ')')
        if entry:
            log.fatal('test_referint: user1 was not removed from group')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_referint: Unable to search group: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    test_dependency(inst, PLUGIN_REFER_INTEGRITY)

    ############################################################################
    # Cleanup
    ############################################################################

    try:
        inst.delete_s(GROUP_DN)
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to delete group: ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(SHARED_CONFIG_DN)
    except ldap.LDAPError as e:
        log.fatal('test_referint: Failed to delete shared config entry: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_referint: PASS\n')

    return


################################################################################
#
# Test Retro Changelog Plugin (9)
#
################################################################################
def test_retrocl(inst, args=None):
    # stop the plugin, and start it
    inst.plugins.disable(name=PLUGIN_RETRO_CHANGELOG)
    inst.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)

    if args == "restart":
        return

    log.info('Testing ' + PLUGIN_RETRO_CHANGELOG + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    # Gather the current change count (it's not 1 once we start the stabilty tests)
    try:
        entry = inst.search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE, '(changenumber=*)')
    except ldap.LDAPError as e:
        log.fatal('test_retrocl: Failed to get the count: error ' + e.message['desc'])
        assert False

    entry_count = len(entry)

    ############################################################################
    # Test plugin
    ############################################################################

    # Add a user
    try:
        inst.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_retrocl: Failed to add user1: error ' + e.message['desc'])
        assert False

    # Check we logged this in the retro cl
    try:
        entry = inst.search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE, '(changenumber=*)')
        if not entry or len(entry) == entry_count:
            log.fatal('test_retrocl: changelog not updated')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_retrocl: Unable to search group: ' + e.message['desc'])
        assert False

    entry_count += 1

    ############################################################################
    # Change the config - disable plugin
    ############################################################################

    inst.plugins.disable(name=PLUGIN_RETRO_CHANGELOG)

    ############################################################################
    # Test plugin
    ############################################################################

    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_retrocl: Failed to delete user1: ' + e.message['desc'])
        assert False

    # Check we didn't logged this in the retro cl
    try:
        entry = inst.search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE, '(changenumber=*)')
        if len(entry) != entry_count:
            log.fatal('test_retrocl: changelog incorrectly updated - change count: '
                + str(len(entry)) + ' - expected 1')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_retrocl: Unable to search retro changelog: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    inst.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)
    test_dependency(inst, PLUGIN_RETRO_CHANGELOG)

    ############################################################################
    # Cleanup
    ############################################################################

    # None

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_retrocl: PASS\n')

    return


################################################################################
#
# Test Root DN Access Control Plugin (10)
#
################################################################################
def test_rootdn(inst, args=None):
    # stop the plugin, and start it
    inst.plugins.disable(name=PLUGIN_ROOTDN_ACCESS)
    inst.plugins.enable(name=PLUGIN_ROOTDN_ACCESS)

    if args == "restart":
        return

    PLUGIN_DN = 'cn=' + PLUGIN_ROOTDN_ACCESS + ',cn=plugins,cn=config'

    log.info('Testing ' + PLUGIN_ROOTDN_ACCESS + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    # Add an user and aci to open up cn=config
    try:
        inst.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1',
                          'userpassword': 'password'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_rootdn: Failed to add user1: error ' + e.message['desc'])
        assert False

    # Set an aci so we can modify the plugin after ew deny the root dn
    ACI = ('(target ="ldap:///cn=config")(targetattr = "*")(version 3.0;acl ' +
           '"all access";allow (all)(userdn="ldap:///anyone");)')
    try:
        inst.modify_s(DN_CONFIG, [(ldap.MOD_ADD, 'aci', ACI)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn: Failed to add aci to config: error ' + e.message['desc'])
        assert False

    # Set allowed IP to an unknown host - blocks root dn
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-ip', '10.10.10.10')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn: Failed to set rootDN plugin config: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Bind as Root DN
    failed = False
    try:
        inst.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        failed = True

    if not failed:
        log.fatal('test_rootdn: Root DN was incorrectly able to bind')
        assert False

    ############################################################################
    # Change the config
    ############################################################################

    # Bind as the user who can make updates to the config
    try:
        inst.simple_bind_s(USER1_DN, 'password')
    except ldap.LDAPError as e:
        log.fatal('test_rootdn: failed to bind as user1')
        assert False

    # First, test that invalid plugin changes are rejected
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-deny-ip', '12.12.ZZZ.12')])
        log.fatal('test_rootdn: Incorrectly allowed to add invalid "rootdn-deny-ip: 12.12.ZZZ.12"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-host', 'host._.com')])
        log.fatal('test_rootdn: Incorrectly allowed to add invalid "rootdn-allow-host: host._.com"')
        assert False
    except ldap.LDAPError:
        pass

    # Remove the restriction
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-allow-ip', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn: Failed to set rootDN plugin config: error ' + e.message['desc'])
        assert False

    ############################################################################
    # Test plugin
    ############################################################################

    # Bind as Root DN
    failed = False
    try:
        inst.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        failed = True

    if failed:
        log.fatal('test_rootdn: Root DN was not able to bind')
        assert False

    ############################################################################
    # Test plugin dependency
    ############################################################################

    test_dependency(inst, PLUGIN_ROOTDN_ACCESS)

    ############################################################################
    # Cleanup - remove ACI from cn=config and test user
    ############################################################################

    try:
        inst.modify_s(DN_CONFIG, [(ldap.MOD_DELETE, 'aci', ACI)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn: Failed to add aci to config: error ' + e.message['desc'])
        assert False

    try:
        inst.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn: Failed to delete user1: ' + e.message['desc'])
        assert False

    ############################################################################
    # Test passed
    ############################################################################

    log.info('test_rootdn: PASS\n')

    return


# Array of test functions
func_tests = [test_acctpolicy, test_attruniq, test_automember, test_dna,
              test_linkedattrs, test_memberof, test_mep, test_passthru,
              test_referint, test_retrocl, test_rootdn]


def test_all_plugins(inst, args=None):
    for func in func_tests:
        func(inst, args)

    return

