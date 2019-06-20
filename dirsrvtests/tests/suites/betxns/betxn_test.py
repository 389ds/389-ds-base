# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import six
import ldap
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, PLUGIN_7_BIT_CHECK, PLUGIN_ATTR_UNIQUENESS, PLUGIN_MEMBER_OF

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope='module')
def dynamic_plugins(topology_st):
    """Enable dynamic plugins - makes plugin testing much easier"""
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError as e:
        ldap.error('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False


def test_betxt_7bit(topology_st, dynamic_plugins):
    """Test that the 7-bit plugin correctly rejects an invalid update

    :id: 9e2ab27b-eda9-4cd9-9968-a1a8513210fd

    :setup: Standalone instance and enabled dynamic plugins

    :steps: 1. Enable PLUGIN_7_BIT_CHECK to "ON"
            2. Add test user
            3. Try to Modify test user's RDN to have 8 bit RDN
            4. Execute search operation for new 8 bit RDN
            5. Remove the test user for cleanup

    :expectedresults:
            1. PLUGIN_7_BIT_CHECK should be ON
            2. Test users should be added
            3. Modify RDN for test user should FAIL
            4. Search operation should FAIL
            5. Test user should be removed
    """

    log.info('Running test_betxt_7bit...')

    USER_DN = 'uid=test_entry,' + DEFAULT_SUFFIX
    eight_bit_rdn = six.u('uid=Fu\u00c4\u00e8')
    BAD_RDN = eight_bit_rdn.encode('utf-8')

    # This plugin should on by default, but just in case...
    topology_st.standalone.plugins.enable(name=PLUGIN_7_BIT_CHECK)

    # Add our test user
    try:
        topology_st.standalone.add_s(Entry((USER_DN, {'objectclass': "top extensibleObject".split(),
                                                      'sn': '1',
                                                      'cn': 'test 1',
                                                      'uid': 'test_entry',
                                                      'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.error('Failed to add test user' + USER_DN + ': error ' + e.message['desc'])
        assert False

    # Attempt a modrdn, this should fail
    try:
        topology_st.standalone.rename_s(USER_DN, BAD_RDN, delold=0)
        log.fatal('test_betxt_7bit: Modrdn operation incorrectly succeeded')
        assert False
    except ldap.LDAPError as e:
        log.info('Modrdn failed as expected: error ' + e.message['desc'])

    # Make sure the operation did not succeed, attempt to search for the new RDN
    try:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, BAD_RDN)
        if entries:
            log.fatal('test_betxt_7bit: Incorrectly found the entry using the invalid RDN')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Error while searching for test entry: ' + e.message['desc'])
        assert False

    #
    # Cleanup - remove the user
    #
    try:
        topology_st.standalone.delete_s(USER_DN)
    except ldap.LDAPError as e:
        log.fatal('Failed to delete test entry: ' + e.message['desc'])
        assert False

    log.info('test_betxt_7bit: PASSED')


def test_betxn_attr_uniqueness(topology_st, dynamic_plugins):
    """Test that we can not add two entries that have the same attr value that is
    defined by the plugin

    :id: 42aeb41c-fbb5-4bc6-a97b-56274034d29f

    :setup: Standalone instance and enabled dynamic plugins

    :steps: 1. Enable PLUGIN_ATTR_UNIQUENESS plugin as "ON"
            2. Add a test user
            3. Add another test user having duplicate uid as previous one
            4. Cleanup - disable PLUGIN_ATTR_UNIQUENESS plugin as "OFF"
            5. Cleanup - remove test user entry

    :expectedresults:
            1. PLUGIN_ATTR_UNIQUENESS plugin should be ON
            2. Test user should be added
            3. Add operation should FAIL
            4. PLUGIN_ATTR_UNIQUENESS plugin should be "OFF"
            5. Test user entry should be removed
    """

    USER1_DN = 'uid=test_entry1,' + DEFAULT_SUFFIX
    USER2_DN = 'uid=test_entry2,' + DEFAULT_SUFFIX

    topology_st.standalone.plugins.enable(name=PLUGIN_ATTR_UNIQUENESS)

    # Add the first entry
    try:
        topology_st.standalone.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '1',
                                                       'cn': 'test 1',
                                                       'uid': 'test_entry1',
                                                       'userpassword': 'password1'})))
    except ldap.LDAPError as e:
        log.fatal('test_betxn_attr_uniqueness: Failed to add test user: ' +
                  USER1_DN + ', error ' + e.message['desc'])
        assert False

    # Add the second entry with a duplicate uid
    try:
        topology_st.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '2',
                                                       'cn': 'test 2',
                                                       'uid': 'test_entry2',
                                                       'uid': 'test_entry1',  # Duplicate value
                                                       'userpassword': 'password2'})))
        log.fatal('test_betxn_attr_uniqueness: The second entry was incorrectly added.')
        assert False
    except ldap.LDAPError as e:
        log.error('test_betxn_attr_uniqueness: Failed to add test user as expected: ' +
                  USER1_DN + ', error ' + e.message['desc'])

    #
    # Cleanup - disable plugin, remove test entry
    #
    topology_st.standalone.plugins.disable(name=PLUGIN_ATTR_UNIQUENESS)

    try:
        topology_st.standalone.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_betxn_attr_uniqueness: Failed to delete test entry1: ' +
                  e.message['desc'])
        assert False

    log.info('test_betxn_attr_uniqueness: PASSED')


def test_betxn_memberof(topology_st, dynamic_plugins):
    """Test PLUGIN_MEMBER_OF plugin

    :id: 70d0b96e-b693-4bf7-bbf5-102a66ac5993

    :setup: Standalone instance and enabled dynamic plugins

    :steps: 1. Enable and configure memberOf plugin
            2. Set memberofgroupattr="member" and memberofAutoAddOC="referral"
            3. Add two test groups - group1 and group2
            4. Add group2 to group1
            5. Add group1 to group2

    :expectedresults:
            1. memberOf plugin plugin should be ON
            2. Set memberofgroupattr="member" and memberofAutoAddOC="referral" should PASS
            3. Add operation should PASS
            4. Add operation should FAIL
            5. Add operation should FAIL
    """

    ENTRY1_DN = 'cn=group1,' + DEFAULT_SUFFIX
    ENTRY2_DN = 'cn=group2,' + DEFAULT_SUFFIX
    PLUGIN_DN = 'cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config'

    # Enable and configure memberOf plugin
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'memberofgroupattr', 'member'),
                                                    (ldap.MOD_REPLACE, 'memberofAutoAddOC', 'referral')])
    except ldap.LDAPError as e:
        log.fatal('test_betxn_memberof: Failed to update config(member): error ' + e.message['desc'])
        assert False

    # Add our test entries
    try:
        topology_st.standalone.add_s(Entry((ENTRY1_DN, {'objectclass': "top groupofnames".split(),
                                                        'cn': 'group1'})))
    except ldap.LDAPError as e:
        log.error('test_betxn_memberof: Failed to add group1:' +
                  ENTRY1_DN + ', error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((ENTRY2_DN, {'objectclass': "top groupofnames".split(),
                                                        'cn': 'group1'})))
    except ldap.LDAPError as e:
        log.error('test_betxn_memberof: Failed to add group2:' +
                  ENTRY2_DN + ', error ' + e.message['desc'])
        assert False

    #
    # Test mod replace
    #

    # Add group2 to group1 - it should fail with objectclass violation
    try:
        topology_st.standalone.modify_s(ENTRY1_DN, [(ldap.MOD_REPLACE, 'member', ENTRY2_DN)])
        log.fatal('test_betxn_memberof: Group2 was incorrectly allowed to be added to group1')
        assert False
    except ldap.LDAPError as e:
        log.info('test_betxn_memberof: Group2 was correctly rejected (mod replace): error ' + e.message['desc'])

    #
    # Test mod add
    #

    # Add group2 to group1 - it should fail with objectclass violation
    try:
        topology_st.standalone.modify_s(ENTRY1_DN, [(ldap.MOD_ADD, 'member', ENTRY2_DN)])
        log.fatal('test_betxn_memberof: Group2 was incorrectly allowed to be added to group1')
        assert False
    except ldap.LDAPError as e:
        log.info('test_betxn_memberof: Group2 was correctly rejected (mod add): error ' + e.message['desc'])

    #
    # Done
    #
    log.info('test_betxn_memberof: PASSED')


def test_betxn_modrdn_memberof(topology_st):
    """Test modrdn operartions and memberOf

    :id: 70d0b96e-b693-4bf7-bbf5-102a66ac5994

    :setup: Standalone instance

    :steps: 1. Enable and configure memberOf plugin
            2. Set memberofgroupattr="member" and memberofAutoAddOC="nsContainer"
            3. Create group and user outside of memberOf plugin scope
            4. Do modrdn to move group into scope
            5. Do modrdn to move group into scope (again)

    :expectedresults:
            1. memberOf plugin plugin should be ON
            2. Set memberofgroupattr="member" and memberofAutoAddOC="nsContainer" should PASS
            3. Creating group and user should PASS
            4. Modrdn should fail with objectclass violation
            5. Second modrdn should also fail with objectclass violation
    """

    peoplebase = 'ou=people,%s' % DEFAULT_SUFFIX
    memberof = MemberOfPlugin(topology_st.standalone)
    memberof.enable()
    memberof.set_autoaddoc('nsContainer')  # Bad OC
    memberof.set('memberOfEntryScope', peoplebase)
    memberof.set('memberOfAllBackends', 'on')
    topology_st.standalone.restart()

    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={
        'cn': 'group',
    })

    # Create user and add it to group
    users = UserAccounts(topology_st.standalone, basedn=DEFAULT_SUFFIX)
    user = users.create(properties=TEST_USER_PROPERTIES)
    if not ds_is_older('1.3.7'):
        user.remove('objectClass', 'nsMemberOf')

    group.add_member(user.dn)

    # Attempt modrdn that should fail, but the original entry should stay in the cache
    with pytest.raises(ldap.OBJECTCLASS_VIOLATION):
        group.rename('cn=group_to_people', newsuperior=peoplebase)

    # Should fail, but not with NO_SUCH_OBJECT as the original entry should still be in the cache
    with pytest.raises(ldap.OBJECTCLASS_VIOLATION):
        group.rename('cn=group_to_people', newsuperior=peoplebase)

    #
    # Done
    #
    log.info('test_betxn_modrdn_memberof: PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
