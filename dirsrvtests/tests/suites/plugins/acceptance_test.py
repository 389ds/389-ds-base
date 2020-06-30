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
import logging
import subprocess
import pytest
from lib389.utils import *
from lib389.plugins import *
from lib389._constants import *
from lib389.dseldif import DSEldif
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.domain import Domain
from lib389.topologies import create_topology, topology_i2 as topo

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

USER_DN = 'uid=test_user_1001,ou=people,dc=example,dc=com'
USER_PW = 'password'
GROUP_DN = 'cn=group,' + DEFAULT_SUFFIX
CONFIG_AREA = 'nsslapd-pluginConfigArea'

if ds_is_older('1.3.7'):
    MEMBER_ATTR = 'member'
else:
    MEMBER_ATTR = 'memberOf'

'''
   Functional tests for each plugin

   Test:
         plugin restarts (test when on and off)
         plugin config validation
         plugin dependencies
         plugin functionality (including plugin tasks)
'''


def check_dependency(inst, plugin, online=True):
    """Set the "account usability" plugin to depend on this plugin.
    This plugin is generic, always enabled, and perfect for our testing
    """

    acct_usability = AccountUsabilityPlugin(inst)
    acct_usability.replace('nsslapd-plugin-depends-on-named', plugin.rdn)

    if online:
        with pytest.raises(ldap.UNWILLING_TO_PERFORM):
            plugin.disable()
        # Now undo the change
        acct_usability.remove('nsslapd-plugin-depends-on-named', plugin.rdn)
    else:
        plugin.disable()
        with pytest.raises((subprocess.CalledProcessError, ValueError)):
            inst.restart()
        dse_ldif = DSEldif(inst)
        dse_ldif.delete(acct_usability.dn, 'nsslapd-plugin-depends-on-named')
        dse_ldif.replace(plugin.dn, 'nsslapd-pluginEnabled', 'on')
        inst.start()


def test_acctpolicy(topo, args=None):
    """Test Account policy basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d829
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Add a config entry for 'lastLoginTime'
        4. Add a user
        5. Bind as the user
        6. Check testLastLoginTime was added to the user
        7. Replace 'stateattrname': 'testLastLoginTime'
        8. Bind as the user
        9. Check testLastLoginTime was added to the user
        10. Check nsslapd-plugin-depends-on-named for the plugin
        11. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = AccountPolicyPlugin(inst)
    plugin.disable()
    plugin.enable()

    if args == "restart":
        return True

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    log.info('Testing {}'.format(PLUGIN_ACCT_POLICY))

    ############################################################################
    # Configure plugin
    ############################################################################
    # Add the config entry
    ap_configs = AccountPolicyConfigs(inst)
    try:
        ap_config = ap_configs.create(properties={'cn': 'config',
                                                  'alwaysrecordlogin': 'yes',
                                                  'stateattrname': 'lastLoginTime'})
    except ldap.ALREADY_EXISTS:
        ap_config = ap_configs.get('config')
        ap_config.replace_many(('alwaysrecordlogin', 'yes'),
                               ('stateattrname', 'lastLoginTime'))

    ############################################################################
    # Test plugin
    ############################################################################
    # Add an entry
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create_test_user(1000, 2000)
    user.add('objectclass', 'extensibleObject')
    user.replace('userPassword', USER_PW)

    # Bind as user
    user.bind(USER_PW)
    time.sleep(1)

    # Check lastLoginTime of USER1
    entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'lastLoginTime=*')
    assert entries

    ############################################################################
    # Change config - change the stateAttrName to a new attribute
    ############################################################################
    test_attribute = "( 2.16.840.1.113719.1.1.4.1.35999 \
    NAME 'testLastLoginTime' DESC 'Test Last login time' \
    SYNTAX 1.3.6.1.4.1.1466.115.121.1.24 SINGLE-VALUE USAGE \
    directoryOperation X-ORIGIN 'dirsrvtests' )"
    Schema(inst).add('attributetypes', test_attribute)
    ap_config.replace('stateattrname', 'testLastLoginTime')

    ############################################################################
    # Test plugin
    ############################################################################
    # login as user
    user.bind(USER_PW)
    time.sleep(1)

    # Check testLastLoginTime was added to USER1
    entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(testLastLoginTime=*)')
    assert entries

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst, plugin, online=isinstance(args, str))

    ############################################################################
    # Cleanup
    ############################################################################
    user.delete()

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_acctpolicy: PASS\n')

    return


def test_attruniq(topo, args=None):
    """Test Attribute uniqueness basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d801
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Add a user: with 'mail' and 'mailAlternateAddress' attributes
        4. Replace 'uniqueness-attribute-name': 'cn'
        5. Try to add a user with the same 'cn'
        6. Replace 'uniqueness-attribute-name': 'mail'
        7. Try to add a user with the same 'mail'
        8. Add 'uniqueness-attribute-name': 'mailAlternateAddress'
        9. Try to add a user with the same 'mailAlternateAddress'
        10. Check nsslapd-plugin-depends-on-named for the plugin
        11. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Should fail
        6. Success
        7. Should fail
        8. Success
        9. Should fail
        10. Success
        11. Success
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = AttributeUniquenessPlugin(inst, dn="cn=attribute uniqueness,cn=plugins,cn=config")
    plugin.disable()
    plugin.enable()

    if args == "restart":
        return

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    log.info('Testing {}'.format(PLUGIN_ATTR_UNIQUENESS))
    user1_dict = {'objectclass': 'extensibleObject',
                  'uid': 'testuser1',
                  'cn': 'testuser1',
                  'sn': 'user1',
                  'uidNumber': '1001',
                  'gidNumber': '2001',
                  'mail': 'user1@example.com',
                  'mailAlternateAddress': 'user1@alt.example.com',
                  'homeDirectory': '/home/testuser1',
                  'userpassword': 'password'}
    user2_dict = {'objectclass': 'extensibleObject',
                  'uid': 'testuser2',
                  'cn': 'testuser2',
                  'sn': 'user2',
                  'uidNumber': '1000',
                  'gidNumber': '2000',
                  'homeDirectory': '/home/testuser2',
                  'userpassword': 'password'}

    ############################################################################
    # Configure plugin
    ############################################################################
    plugin.replace('uniqueness-attribute-name', 'cn')
    if args is None:
        inst.restart()

    ############################################################################
    # Test plugin
    ############################################################################
    # Add an entry
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user1 = users.create(properties=user1_dict)

    # Add an entry with a duplicate "cn"
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user2_dict['cn'] = 'testuser1'
        users.create(properties=user2_dict)

    ############################################################################
    # Change config to use "mail" instead of "uid"
    ############################################################################

    plugin.replace('uniqueness-attribute-name', 'mail')

    ############################################################################
    # Test plugin - Add an entry, that has a duplicate "mail" value
    ############################################################################
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user2_dict['mail'] = 'user1@example.com'
        users.create(properties=user2_dict)

    ############################################################################
    # Reconfigure plugin for mail and mailAlternateAddress
    ############################################################################
    plugin.add('uniqueness-attribute-name', 'mailAlternateAddress')

    ############################################################################
    # Test plugin - Add an entry, that has a duplicate "mail" value
    ############################################################################
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user2_dict['mail'] = 'user1@example.com'
        users.create(properties=user2_dict)

    ############################################################################
    # Test plugin - Add an entry, that has a duplicate "mailAlternateAddress" value
    ############################################################################
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user2_dict['mailAlternateAddress'] = 'user1@alt.example.com'
        users.create(properties=user2_dict)

    ############################################################################
    # Test plugin - Add an entry, that has a duplicate "mail" value conflicting mailAlternateAddress
    ############################################################################
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user2_dict['mail'] = 'user1@alt.example.com'
        users.create(properties=user2_dict)

    ############################################################################
    # Test plugin - Add an entry, that has a duplicate "mailAlternateAddress" conflicting mail
    ############################################################################
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user2_dict['mailAlternateAddress'] = 'user1@example.com'
        users.create(properties=user2_dict)

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst, plugin, online=isinstance(args, str))

    ############################################################################
    # Cleanup
    ############################################################################
    user1.delete()

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_attruniq: PASS\n')
    return


def test_automember(topo, args=None):
    """Test Auto Membership basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d802
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Add a group
        4. Add two Organisation Units entries
        5. Add a config entry for the group and one branch
        6. Add a user that should get added to the group
        7. Check the entry is in group
        8. Set groupattr to 'uniquemember:dn' and scope to branch2
        9. Add a user that should get added to the group
        10. Check the group
        11. Disable plugin and restart
        12. Add an entry that should be picked up by automember
        13. Verify that the entry is not picked up by automember (yet)
        14. Check the group - uniquemember should not exist
        15. Enable plugin and restart
        16. Verify the fixup task worked
        17. Check nsslapd-plugin-depends-on-named for the plugin
        18. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
        17. Success
        18. Success
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = AutoMembershipPlugin(inst)
    plugin.disable()
    plugin.enable()

    if args == "restart":
        return

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    log.info('Testing ' + PLUGIN_AUTOMEMBER + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    # Add the automember group
    groups = Groups(inst, DEFAULT_SUFFIX)
    group = groups.create(properties={'cn': 'group'})

    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    branch1 = ous.create(properties={'ou': 'branch1'})
    branch2 = ous.create(properties={'ou': 'branch2'})

    # Add the automember config entry
    am_configs = AutoMembershipDefinitions(inst)
    am_config = am_configs.create(properties={'cn': 'config',
                                              'autoMemberScope': branch1.dn,
                                              'autoMemberFilter': 'objectclass=top',
                                              'autoMemberDefaultGroup': group.dn,
                                              'autoMemberGroupingAttr': '{}:dn'.format(MEMBER_ATTR)})

    ############################################################################
    # Test the plugin
    ############################################################################

    users = UserAccounts(inst, DEFAULT_SUFFIX, rdn='ou={}'.format(branch1.rdn))
    # Add a user that should get added to the group
    user1 = users.create_test_user(uid=1001)

    # Check the group
    group_members = group.get_attr_vals_utf8(MEMBER_ATTR)
    assert user1.dn in group_members

    ############################################################################
    # Change config
    ############################################################################
    group.add('objectclass', 'groupOfUniqueNames')
    am_config.set_groupattr('uniquemember:dn')
    am_config.set_scope(branch2.dn)

    ############################################################################
    # Test plugin
    ############################################################################
    # Add a user that should get added to the group
    users = UserAccounts(inst, DEFAULT_SUFFIX, rdn='ou={}'.format(branch2.rdn))
    user2 = users.create_test_user(uid=1002)

    # Check the group
    group_members = group.get_attr_vals_utf8('uniquemember')
    assert user2.dn in group_members

    ############################################################################
    # Test Task
    ############################################################################

    # Disable plugin
    plugin.disable()

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    # Add an entry that should be picked up by automember - verify it is not(yet)
    user3 = users.create_test_user(uid=1003)

    # Check the group - uniquemember should not exist
    group_members = group.get_attr_vals_utf8('uniquemember')
    assert user3.dn not in group_members

    # Enable plugin
    plugin.enable()

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()
    task = plugin.fixup(branch2.dn, _filter='objectclass=top')
    task.wait()

    # Verify the fixup task worked
    group_members = group.get_attr_vals_utf8('uniquemember')
    assert user3.dn in group_members

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst, plugin, online=isinstance(args, str))

    ############################################################################
    # Cleanup
    ############################################################################
    user1.delete()
    user2.delete()
    user3.delete()
    branch1.delete()
    branch2.delete()
    group.delete()
    am_config.delete()

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_automember: PASS\n')
    return


def test_dna(topo, args=None):
    """Test DNA basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d803
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Configure plugin for uidNumber
        4. Add a user
        5. See if the entry now has the new uidNumber assignment - uidNumber=1
        6. Test the magic regen value
        7. See if the entry now has the new uidNumber assignment - uidNumber=2
        8. Set 'dnaMagicRegen': '-2'
        9. Test the magic regen value
        10. See if the entry now has the new uidNumber assignment - uidNumber=3
        11. Check nsslapd-plugin-depends-on-named for the plugin
        12. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = DNAPlugin(inst)
    plugin.disable()
    plugin.enable()

    if args == "restart":
        return

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    log.info('Testing ' + PLUGIN_DNA + '...')

    ############################################################################
    # Configure plugin
    ############################################################################
    dna_configs = DNAPluginConfigs(inst, plugin.dn)
    try:
        dna_config = dna_configs.create(properties={'cn': 'config',
                                                    'dnatype': 'uidNumber',
                                                    'dnafilter': '(objectclass=top)',
                                                    'dnascope': DEFAULT_SUFFIX,
                                                    'dnaMagicRegen': '-1',
                                                    'dnaMaxValue': '50000',
                                                    'dnaNextValue': '1'})
    except ldap.ALREADY_EXISTS:
        dna_config = dna_configs.get('config')
        dna_config.replace_many(('dnaNextValue', '1'), ('dnaMagicRegen', '-1'))

    ############################################################################
    # Test plugin
    ############################################################################
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user1 = users.create_test_user(uid=1)

    # See if the entry now has the new uidNumber assignment - uidNumber=1
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '(uidNumber=1)')
    assert entries

    # Test the magic regen value
    user1.replace('uidNumber', '-1')

    # See if the entry now has the new uidNumber assignment - uidNumber=2
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '(uidNumber=2)')
    assert entries

    ################################################################################
    # Change the config
    ################################################################################
    dna_config.replace('dnaMagicRegen', '-2')

    ################################################################################
    # Test plugin
    ################################################################################

    # Test the magic regen value
    user1.replace('uidNumber', '-2')

    # See if the entry now has the new uidNumber assignment - uidNumber=3
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '(uidNumber=3)')
    assert entries

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst, plugin, online=isinstance(args, str))

    ############################################################################
    # Cleanup
    ############################################################################
    user1.delete()
    dna_config.delete()
    plugin.disable()

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_dna: PASS\n')
    return


def test_linkedattrs(topo, args=None):
    """Test Linked Attributes basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d804
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Add a config entry for directReport
        4. Add test entries
        5. Add the linked attrs config entry
        6. User1 - Set "directReport" to user2
        7. See if manager was added to the other entry
        8. User1 - Remove "directReport"
        9. See if manager was removed
        10. Change the config - using linkType "indirectReport" now
        11. Make sure the old linkType(directManager) is not working
        12. See if manager was added to the other entry, better not be...
        13. Now, set the new linkType "indirectReport", which should add "manager" to the other entry
        14. See if manager was added to the other entry, better not be
        15. Remove "indirectReport" should remove "manager" to the other entry
        16. See if manager was removed
        17. Disable plugin and make some updates that would of triggered the plugin
        18. The entry should not have a manager attribute
        19. Enable the plugin and rerun the task entry
        20. Add the task again
        21. Check if user2 now has a manager attribute now
        22. Check nsslapd-plugin-depends-on-named for the plugin
        23. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
        17. Success
        18. Success
        19. Success
        20. Success
        21. Success
        22. Success
        23. Success
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = LinkedAttributesPlugin(inst)
    plugin.disable()
    plugin.enable()

    if args == "restart":
        return

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    log.info('Testing ' + PLUGIN_LINKED_ATTRS + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    # Add test entries
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user1 = users.create_test_user(uid=1001)
    user1.add('objectclass', 'extensibleObject')
    user2 = users.create_test_user(uid=1002)
    user2.add('objectclass', 'extensibleObject')

    # Add the linked attrs config entry
    la_configs = LinkedAttributesConfigs(inst)
    la_config = la_configs.create(properties={'cn': 'config',
                                              'linkType': 'directReport',
                                              'managedType': 'manager'})

    ############################################################################
    # Test plugin
    ############################################################################
    # Set "directReport" should add "manager" to the other entry
    user1.replace('directReport', user2.dn)

    # See if manager was added to the other entry
    entries = inst.search_s(user2.dn, ldap.SCOPE_BASE, '(manager=*)')
    assert entries

    # Remove "directReport" should remove "manager" to the other entry
    user1.remove_all('directReport')

    # See if manager was removed
    entries = inst.search_s(user2.dn, ldap.SCOPE_BASE, '(manager=*)')
    assert not entries

    ############################################################################
    # Change the config - using linkType "indirectReport" now
    ############################################################################
    la_config.replace('linkType', 'indirectReport')

    ############################################################################
    # Test plugin
    ############################################################################
    # Make sure the old linkType(directManager) is not working
    user1.replace('directReport', user2.dn)

    # See if manager was added to the other entry, better not be...
    entries = inst.search_s(user2.dn, ldap.SCOPE_BASE, '(manager=*)')
    assert not entries

    # Now, set the new linkType "indirectReport", which should add "manager" to the other entry
    user1.replace('indirectReport', user2.dn)

    # See if manager was added to the other entry, better not be
    entries = inst.search_s(user2.dn, ldap.SCOPE_BASE, '(manager=*)')
    assert entries

    # Remove "indirectReport" should remove "manager" to the other entry
    user1.remove_all('indirectReport')

    # See if manager was removed
    entries = inst.search_s(user2.dn, ldap.SCOPE_BASE, '(manager=*)')
    assert not entries

    ############################################################################
    # Test Fixup Task
    ############################################################################
    # Disable plugin and make some updates that would of triggered the plugin
    plugin.disable()

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    user1.replace('indirectReport', user2.dn)

    # The entry should not have a manager attribute
    entries = inst.search_s(user2.dn, ldap.SCOPE_BASE, '(manager=*)')
    assert not entries

    # Enable the plugin and rerun the task entry
    plugin.enable()

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    # Add the task again
    task = plugin.fixup(la_config.dn)
    task.wait()

    # Check if user2 now has a manager attribute now
    entries = inst.search_s(user2.dn, ldap.SCOPE_BASE, '(manager=*)')
    assert entries

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst, plugin, online=isinstance(args, str))

    ############################################################################
    # Cleanup
    ############################################################################
    user1.delete()
    user2.delete()
    la_config.delete()

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_linkedattrs: PASS\n')
    return


def test_memberof(topo, args=None):
    """Test MemberOf basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d805
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Replace groupattr with 'member'
        4. Add our test entries
        5. Check if the user now has a "memberOf" attribute
        6. Remove "member" should remove "memberOf" from the entry
        7. Check that "memberOf" was removed
        8. Replace 'memberofgroupattr': 'uniquemember'
        9. Replace 'uniquemember': user1
        10. Check if the user now has a "memberOf" attribute
        11. Remove "uniquemember" should remove "memberOf" from the entry
        12. Check that "memberOf" was removed
        13. The shared config entry uses "member" - the above test uses "uniquemember"
        14. Delete the test entries then read them to start with a clean slate
        15. Check if the user now has a "memberOf" attribute
        16. Check that "memberOf" was removed
        17. Replace 'memberofgroupattr': 'uniquemember'
        18. Check if the user now has a "memberOf" attribute
        19. Remove "uniquemember" should remove "memberOf" from the entry
        20. Check that "memberOf" was removed
        21. Replace 'memberofgroupattr': 'member'
        22. Remove shared config from plugin
        23. Check if the user now has a "memberOf" attribute
        24. Remove "uniquemember" should remove "memberOf" from the entry
        25. Check that "memberOf" was removed
        26. First change the plugin to use uniquemember
        27. Add uniquemember, should not update user1
        28. Check for "memberOf"
        29. Enable memberof plugin
        30. Run the task and validate that it worked
        31. Check for "memberOf"
        32. Check nsslapd-plugin-depends-on-named for the plugin
        33. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
        17. Success
        18. Success
        19. Success
        20. Success
        21. Success
        22. Success
        23. Success
        24. Success
        25. Success
        26. Success
        27. Success
        28. Success
        29. Success
        30. Success
        31. Success
        32. Success
        33. Success
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = MemberOfPlugin(inst)
    plugin.disable()
    plugin.enable()

    if args == "restart":
        return

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    log.info('Testing ' + PLUGIN_MEMBER_OF + '...')

    ############################################################################
    # Configure plugin
    ############################################################################
    plugin.replace_groupattr('member')

    ############################################################################
    # Test plugin
    ############################################################################
    # Add our test entries
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user1 = users.create_test_user(uid=1001)

    groups = Groups(inst, DEFAULT_SUFFIX)
    group = groups.create(properties={'cn': 'group',
                                      'member': user1.dn})
    group.add('objectclass', 'groupOfUniqueNames')

    memberof_config = MemberOfSharedConfig(inst, 'cn=memberOf config,{}'.format(DEFAULT_SUFFIX))
    memberof_config.create(properties={'cn': 'memberOf config',
                                       'memberOfGroupAttr': 'member',
                                       'memberOfAttr': MEMBER_ATTR})

    # Check if the user now has a "memberOf" attribute
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert entries

    # Remove "member" should remove "memberOf" from the entry
    group.remove_all('member')

    # Check that "memberOf" was removed
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert not entries

    ############################################################################
    # Change the config
    ############################################################################
    plugin.replace('memberofgroupattr', 'uniquemember')

    ############################################################################
    # Test plugin
    ############################################################################
    group.replace('uniquemember', user1.dn)

    # Check if the user now has a "memberOf" attribute
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert entries

    # Remove "uniquemember" should remove "memberOf" from the entry
    group.remove_all('uniquemember')

    # Check that "memberOf" was removed
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert not entries

    ############################################################################
    # Set the shared config entry and test the plugin
    ############################################################################
    # The shared config entry uses "member" - the above test uses "uniquemember"
    plugin.set_configarea(memberof_config.dn)
    if args is None:
        inst.restart()

    # Delete the test entries then readd them to start with a clean slate
    user1.delete()
    group.delete()

    user1 = users.create_test_user(uid=1001)
    group = groups.create(properties={'cn': 'group',
                                      'member': user1.dn})
    group.add('objectclass', 'groupOfUniqueNames')

    # Test the shared config
    # Check if the user now has a "memberOf" attribute
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert entries

    group.remove_all('member')

    # Check that "memberOf" was removed
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert not entries

    ############################################################################
    # Change the shared config entry to use 'uniquemember' and test the plugin
    ############################################################################
    memberof_config.replace('memberofgroupattr', 'uniquemember')

    group.replace('uniquemember', user1.dn)

    # Check if the user now has a "memberOf" attribute
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert entries

    # Remove "uniquemember" should remove "memberOf" from the entry
    group.remove_all('uniquemember')

    # Check that "memberOf" was removed
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert not entries

    ############################################################################
    # Remove shared config from plugin, and retest
    ############################################################################
    # First change the plugin to use member before we move the shared config that uses uniquemember
    plugin.replace('memberofgroupattr', 'member')

    # Remove shared config from plugin
    plugin.remove_configarea()

    group.replace('member', user1.dn)

    # Check if the user now has a "memberOf" attribute
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert entries

    # Remove "uniquemember" should remove "memberOf" from the entry
    group.remove_all('member')

    # Check that "memberOf" was removed
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert not entries

    ############################################################################
    # Test Fixup Task
    ############################################################################
    plugin.disable()

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    # First change the plugin to use uniquemember
    plugin.replace('memberofgroupattr', 'uniquemember')

    # Add uniquemember, should not update USER1
    group.replace('uniquemember', user1.dn)

    # Check for "memberOf"
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert not entries

    # Enable memberof plugin
    plugin.enable()

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    #############################################################
    # Test memberOf fixup arg validation:  Test the DN and filter
    #############################################################
    for basedn, filter in (('{}bad'.format(DEFAULT_SUFFIX), 'objectclass=top'),
                           ("bad", 'objectclass=top'),
                           (DEFAULT_SUFFIX, '(objectclass=top')):
        task = plugin.fixup(basedn, filter)
        task.wait()
        exitcode = task.get_exit_code()
        assert exitcode != "0", 'test_memberof: Task with invalid DN still reported success'

    ####################################################
    # Test fixup works
    ####################################################
    # Run the task and validate that it worked
    task = plugin.fixup(DEFAULT_SUFFIX, 'objectclass=top')
    task.wait()

    # Check for "memberOf"
    entries = inst.search_s(user1.dn, ldap.SCOPE_BASE, '({}=*)'.format(MEMBER_ATTR))
    assert entries

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst, plugin, online=isinstance(args, str))

    ############################################################################
    # Cleanup
    ############################################################################
    user1.delete()
    group.delete()
    memberof_config.delete()

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_memberof: PASS\n')
    return


def test_mep(topo, args=None):
    """Test Managed Entries basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d806
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Add our org units
        4. Set up config entry and template entry for the org units
        5. Add an entry that meets the MEP scope
        6. Check if a managed group entry was created
        7. Add a new template entry
        8. Add an entry that meets the MEP scope
        9. Check if a managed group entry was created
        10. Check nsslapd-plugin-depends-on-named for the plugin
        11. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = ManagedEntriesPlugin(inst)
    plugin.disable()
    plugin.enable()

    if args == "restart":
        return

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    log.info('Testing ' + PLUGIN_MANAGED_ENTRY + '...')

    ############################################################################
    # Configure plugin
    ############################################################################
    # Add our org units
    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    ou_people = ous.create(properties={'ou': 'managed_people'})
    ou_groups = ous.create(properties={'ou': 'managed_groups'})

    mep_templates = MEPTemplates(inst, DEFAULT_SUFFIX)
    mep_template1 = mep_templates.create(properties={
        'cn': 'MEP template',
        'mepRDNAttr': 'cn',
        'mepStaticAttr': 'objectclass: posixGroup|objectclass: extensibleObject'.split('|'),
        'mepMappedAttr': 'cn: $cn|uid: $cn|gidNumber: $uidNumber'.split('|')
    })
    mep_configs = MEPConfigs(inst)
    mep_config = mep_configs.create(properties={'cn': 'config',
                                                'originScope': ou_people.dn,
                                                'originFilter': 'objectclass=posixAccount',
                                                'managedBase': ou_groups.dn,
                                                'managedTemplate': mep_template1.dn})
    if args is None:
        inst.restart()

    ############################################################################
    # Test plugin
    ############################################################################
    # Add an entry that meets the MEP scope
    test_users_m1 = UserAccounts(inst, DEFAULT_SUFFIX, rdn='ou={}'.format(ou_people.rdn))
    test_user1 = test_users_m1.create_test_user(1001)

    # Check if a managed group entry was created
    entries = inst.search_s('cn={},{}'.format(test_user1.rdn, ou_groups.dn), ldap.SCOPE_BASE, '(objectclass=top)')
    assert len(entries) == 1

    ############################################################################
    # Change the config
    ############################################################################
    # Add a new template entry
    mep_template2 = mep_templates.create(properties={
        'cn': 'MEP template2',
        'mepRDNAttr': 'uid',
        'mepStaticAttr': 'objectclass: posixGroup|objectclass: extensibleObject'.split('|'),
        'mepMappedAttr': 'cn: $cn|uid: $cn|gidNumber: $uidNumber'.split('|')
    })
    mep_config.replace('managedTemplate', mep_template2.dn)

    ############################################################################
    # Test plugin
    ############################################################################
    # Add an entry that meets the MEP scope
    test_user2 = test_users_m1.create_test_user(1002)

    # Check if a managed group entry was created
    entries = inst.search_s('uid={},{}'.format(test_user2.rdn, ou_groups.dn), ldap.SCOPE_BASE, '(objectclass=top)')
    assert len(entries) == 1

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst, plugin, online=isinstance(args, str))

    ############################################################################
    # Cleanup
    ############################################################################
    test_user1.delete()
    test_user2.delete()
    ou_people.delete()
    ou_groups.delete()
    mep_config.delete()
    mep_template1.delete()
    mep_template2.delete()

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_mep: PASS\n')
    return


def test_passthru(topo, args=None):
    """Test Passthrough Authentication basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d807
    :setup: Standalone Instance
    :steps:
        1. Stop the plugin
        2. Restart the instance
        3. Create a second backend
        4. Create the top of the tree
        5. Add user to suffix1
        6. Configure and start plugin
        7. Login as user
        8. Login as root DN
        9. Replace 'nsslapd-pluginarg0': ldap uri for second instance
        10. Login as user
        11. Login as root DN
        12. Check nsslapd-plugin-depends-on-named for the plugin
        13. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
    """

    inst1 = topo[0]
    inst2 = topo[1]

    # Passthru is a bit picky about the state of the entry - we can't just restart it
    if args == "restart":
        return

    # stop the plugin
    plugin = PassThroughAuthenticationPlugin(inst1)
    plugin.disable()

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst1.restart()

    PASS_SUFFIX1 = 'dc=pass1,dc=thru'
    PASS_SUFFIX2 = 'dc=pass2,dc=thru'
    PASS_BE1 = 'PASS1'
    PASS_BE2 = 'PASS2'

    log.info('Testing ' + PLUGIN_PASSTHRU + '...')

    ############################################################################
    # Use a new "remote" instance, and a user for auth
    ############################################################################
    # Create a second backend
    backend1 = inst2.backends.create(properties={'cn': PASS_BE1,
                                     'nsslapd-suffix': PASS_SUFFIX1})
    backend2 = inst2.backends.create(properties={'cn': PASS_BE2,
                                     'nsslapd-suffix': PASS_SUFFIX2})

    # Create the top of the tree
    suffix = Domain(inst2, PASS_SUFFIX1)
    pass1 = suffix.create(properties={'dc': 'pass1'})
    suffix = Domain(inst2, PASS_SUFFIX2)
    pass2 = suffix.create(properties={'dc': 'pass2'})

    # Add user to suffix1
    users = UserAccounts(inst2, pass1.dn, None)
    test_user1 = users.create_test_user(1001)
    test_user1.replace('userpassword', 'password')

    users = UserAccounts(inst2, pass2.dn, None)
    test_user2 = users.create_test_user(1002)
    test_user2.replace('userpassword', 'password')

    ############################################################################
    # Configure and start plugin
    ############################################################################
    plugin.replace('nsslapd-pluginarg0',
                   'ldap://{}:{}/{}'.format(inst2.host, inst2.port, pass1.dn))
    plugin.enable()

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst1.restart()

    ############################################################################
    # Test plugin
    ############################################################################
    # login as user
    inst1.simple_bind_s(test_user1.dn, "password")

    ############################################################################
    # Change the config
    ############################################################################
    # login as root DN
    inst1.simple_bind_s(DN_DM, PASSWORD)

    plugin.replace('nsslapd-pluginarg0',
                   'ldap://{}:{}/{}'.format(inst2.host, inst2.port, pass2.dn))
    if args is None:
        inst1.restart()

    ############################################################################
    # Test plugin
    ############################################################################

    # login as user
    inst1.simple_bind_s(test_user2.dn, "password")

    # login as root DN
    inst1.simple_bind_s(DN_DM, PASSWORD)

    # Clean up
    backend1.delete()
    backend2.delete()

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst1, plugin, online=isinstance(args, str))

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_passthru: PASS\n')
    return


def test_referint(topo, args=None):
    """Test Referential Integrity basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d808
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Replace 'referint-membership-attr': 'member'
        4. Add some users and a group
        5. Grab the referint log file from the plugin
        6. Add shared config entry
        7. Delete one user
        8. Check for integrity
        9. Replace 'referint-membership-attr': 'uniquemember'
        10. Delete second user
        11. Check for integrity
        12. The shared config entry uses "member" - the above test used "uniquemember"
        13. Recreate users and a group
        14. Delete one user
        15. Check for integrity
        16. Change the shared config entry to use 'uniquemember' and test the plugin
        17. Delete second user
        18. Check for integrity
        19. First change the plugin to use member before we move the shared config that uses uniquemember
        20. Remove shared config from plugin
        21. Add test user
        22. Add user to group
        23. Delete a user
        24. Check for integrity
        25. Check nsslapd-plugin-depends-on-named for the plugin
        26. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
        17. Success
        18. Success
        19. Success
        20. Success
        21. Success
        22. Success
        23. Success
        24. Success
        25. Success
        26. Success
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = ReferentialIntegrityPlugin(inst)
    plugin.disable()
    plugin.enable()

    if args == "restart":
        return

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    log.info('Testing ' + PLUGIN_REFER_INTEGRITY + '...')

    ############################################################################
    # Configure plugin
    ############################################################################
    plugin.replace('referint-membership-attr', 'member')

    ############################################################################
    # Test plugin
    ############################################################################
    # Add some users and a group
    users = UserAccounts(inst, DEFAULT_SUFFIX, None)
    user1 = users.create_test_user(uid=1001)
    user2 = users.create_test_user(uid=1002)

    groups = Groups(inst, DEFAULT_SUFFIX, None)
    group = groups.create(properties={'cn': 'group',
                                      MEMBER_ATTR: user1.dn})
    group.add('objectclass', 'groupOfUniqueNames')
    group.add('uniquemember', user2.dn)

    # Grab the referint log file from the plugin
    referin_logfile = plugin.get_attr_val_utf8('referint-logfile')

    # Add shared config entry
    referin_config = ReferentialIntegrityConfig(inst, 'cn=RI config,{}'.format(DEFAULT_SUFFIX))
    referin_config.create(properties={'cn': 'RI config',
                                      'referint-membership-attr': 'member',
                                      'referint-update-delay': '0',
                                      'referint-logfile': referin_logfile})

    user1.delete()

    # Check for integrity
    entry = inst.search_s(group.dn, ldap.SCOPE_BASE, '(member={})'.format(user1.dn))
    assert not entry

    ############################################################################
    # Change the config
    ############################################################################
    plugin.replace('referint-membership-attr', 'uniquemember')

    ############################################################################
    # Test plugin
    ############################################################################

    user2.delete()

    # Check for integrity
    entry = inst.search_s(group.dn, ldap.SCOPE_BASE, '(uniquemember={})'.format(user2.dn))
    assert not entry

    ############################################################################
    # Set the shared config entry and test the plugin
    ############################################################################
    # The shared config entry uses "member" - the above test used "uniquemember"
    plugin.set_configarea(referin_config.dn)
    group.delete()

    user1 = users.create_test_user(uid=1001)
    user2 = users.create_test_user(uid=1002)
    group = groups.create(properties={'cn': 'group',
                                      MEMBER_ATTR: user1.dn})
    group.add('objectclass', 'groupOfUniqueNames')
    group.add('uniquemember', user2.dn)

    # Delete a user
    user1.delete()

    # Check for integrity
    entry = inst.search_s(group.dn, ldap.SCOPE_BASE, '(member={})'.format(user1.dn))
    assert not entry

    ############################################################################
    # Change the shared config entry to use 'uniquemember' and test the plugin
    ############################################################################

    referin_config.replace('referint-membership-attr', 'uniquemember')

    # Delete a user
    user2.delete()

    # Check for integrity
    entry = inst.search_s(group.dn, ldap.SCOPE_BASE, '(uniquemember={})'.format(user2.dn))
    assert not entry

    ############################################################################
    # Remove shared config from plugin, and retest
    ############################################################################
    # First change the plugin to use member before we move the shared config that uses uniquemember
    plugin.replace('referint-membership-attr', 'member')

    # Remove shared config from plugin
    plugin.remove_configarea()

    # Add test user
    user1 = users.create_test_user(uid=1001)

    # Add user to group
    group.replace('member', user1.dn)

    # Delete a user
    user1.delete()

    # Check for integrity
    entry = inst.search_s(group.dn, ldap.SCOPE_BASE, '(member={})'.format(user1.dn))
    assert not entry

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst, plugin, online=isinstance(args, str))

    ############################################################################
    # Cleanup
    ############################################################################
    group.delete()
    referin_config.delete()

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_referint: PASS\n')
    return


def test_retrocl(topo, args=None):
    """Test Retro Changelog basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d810
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Gather the current change count (it's not 1 once we start the stability tests)
        4. Add a user
        5. Check we logged this in the retro cl
        6. Change the config - disable plugin
        7. Delete the user
        8. Check we didn't log this in the retro cl
        9. Check nsslapd-plugin-depends-on-named for the plugin
        10. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = RetroChangelogPlugin(inst)
    plugin.disable()
    plugin.enable()

    if args == "restart":
        return

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    log.info('Testing ' + PLUGIN_RETRO_CHANGELOG + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    # Gather the current change count (it's not 1 once we start the stabilty tests)
    entry = inst.search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE, '(changenumber=*)')
    entry_count = len(entry)

    ############################################################################
    # Test plugin
    ############################################################################

    # Add a user
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user1 = users.create_test_user(uid=1001)

    # Check we logged this in the retro cl
    entry = inst.search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE, '(changenumber=*)')
    assert entry
    assert len(entry) != entry_count

    entry_count += 1

    ############################################################################
    # Change the config - disable plugin
    ############################################################################
    plugin.disable()

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    ############################################################################
    # Test plugin
    ############################################################################
    user1.delete()

    # Check we didn't logged this in the retro cl
    entry = inst.search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE, '(changenumber=*)')
    assert len(entry) == entry_count

    plugin.enable()
    if args is None:
        inst.restart()

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst, plugin, online=isinstance(args, str))

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_retrocl: PASS\n')
    return


def _rootdn_restart(inst):
    """Special restart wrapper function for rootDN plugin"""

    with pytest.raises(ldap.LDAPError):
        inst.restart()
    # Bind as the user who can make updates to the config
    inst.simple_bind_s(USER_DN, USER_PW)
    # We need it online for other operations to work
    inst.state = DIRSRV_STATE_ONLINE


def test_rootdn(topo, args=None):
    """Test Root DNA Access control basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d811
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Add an user and aci to open up cn=config
        4. Set an aci so we can modify the plugin after we deny the root dn
        5. Set allowed IP to an unknown host - blocks root dn
        6. Bind as Root DN
        7. Bind as the user who can make updates to the config
        8. Test that invalid plugin changes are rejected
        9. Remove the restriction
        10. Bind as Root DN
        11. Check nsslapd-plugin-depends-on-named for the plugin
        12. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = RootDNAccessControlPlugin(inst)
    plugin.disable()
    plugin.enable()

    if args == "restart":
        return

    # If args is None then we run the test suite as pytest standalone and it's not dynamic
    if args is None:
        inst.restart()

    log.info('Testing ' + PLUGIN_ROOTDN_ACCESS + '...')

    ############################################################################
    # Configure plugin
    ############################################################################

    # Add an user and aci to open up cn=config
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user1 = users.create_test_user(uid=1001)
    user1.replace('userpassword', USER_PW)

    # Set an aci so we can modify the plugin after ew deny the root dn
    ACI = ('(target ="ldap:///cn=config")(targetattr = "*")(version 3.0;acl ' +
           '"all access";allow (all)(userdn="ldap:///anyone");)')
    inst.config.add('aci', ACI)

    # Set allowed IP to an unknown host - blocks root dn
    plugin.replace('rootdn-allow-ip', '10.10.10.10')

    ############################################################################
    # Test plugin
    ############################################################################
    # Bind as Root DN
    if args is None:
        _rootdn_restart(inst)
    else:
        with pytest.raises(ldap.LDAPError):
            inst.simple_bind_s(DN_DM, PASSWORD)
        # Bind as the user who can make updates to the config
        inst.simple_bind_s(USER_DN, USER_PW)

    ############################################################################
    # Change the config
    ############################################################################
    # First, test that invalid plugin changes are rejected
    if args is None:
        plugin.replace('rootdn-deny-ip', '12.12.ZZZ.12')
        with pytest.raises((subprocess.CalledProcessError, ValueError)):
            inst.restart()
        dse_ldif = DSEldif(inst)
        dse_ldif.delete(plugin.dn, 'rootdn-deny-ip')
        _rootdn_restart(inst)

        plugin.replace('rootdn-allow-host', 'host._.com')
        with pytest.raises((subprocess.CalledProcessError, ValueError)):
            inst.restart()
        dse_ldif = DSEldif(inst)
        dse_ldif.delete(plugin.dn, 'rootdn-allow-host')
        _rootdn_restart(inst)
    else:
        with pytest.raises(ldap.LDAPError):
            plugin.replace('rootdn-deny-ip', '12.12.ZZZ.12')

        with pytest.raises(ldap.LDAPError):
            plugin.replace('rootdn-allow-host', 'host._.com')

    # Remove the restriction
    plugin.remove_all('rootdn-allow-ip')
    if args is None:
        inst.restart()

    ############################################################################
    # Test plugin
    ############################################################################
    # Bind as Root DN
    inst.simple_bind_s(DN_DM, PASSWORD)

    ############################################################################
    # Test plugin dependency
    ############################################################################
    check_dependency(inst, plugin, online=isinstance(args, str))

    ############################################################################
    # Cleanup - remove ACI from cn=config and test user
    ############################################################################
    inst.config.remove('aci', ACI)
    user1.delete()

    ############################################################################
    # Test passed
    ############################################################################
    log.info('test_rootdn: PASS\n')
    return


# Array of test functions
func_tests = [test_acctpolicy, test_attruniq, test_automember, test_dna,
              test_linkedattrs, test_memberof, test_mep, test_passthru,
              test_referint, test_retrocl, test_rootdn]


def check_all_plugins(topo, args="online"):
    for func in func_tests:
        func(topo, args)

    return
