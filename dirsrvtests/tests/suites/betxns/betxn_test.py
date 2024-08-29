# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import ldap
import os
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.plugins import (SevenBitCheckPlugin, AttributeUniquenessPlugin,
                            MemberOfPlugin, ManagedEntriesPlugin,
                            ReferentialIntegrityPlugin, MEPTemplates,
                            MEPConfigs)
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.group import Groups, Group
from lib389.idm.domain import Domain
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)
USER_PASSWORD = 'password'


def test_betxt_7bit(topology_st):
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

    BAD_RDN = u'uid=Fu\u00c4\u00e8'

    sevenbc = SevenBitCheckPlugin(topology_st.standalone)
    sevenbc.enable()
    topology_st.standalone.restart()

    users = UserAccounts(topology_st.standalone, basedn=DEFAULT_SUFFIX)
    user = users.create(properties=TEST_USER_PROPERTIES)

    # Attempt a modrdn, this should fail
    with pytest.raises(ldap.LDAPError):
        user.rename(BAD_RDN)

    # Make sure the operation did not succeed, attempt to search for the new RDN
    with pytest.raises(ldap.LDAPError):
        users.get(u'Fu\u00c4\u00e8')

    # Make sure original entry is present
    user_check = users.get("testuser")
    assert user_check.dn.lower() == user.dn.lower()

    # Cleanup - remove the user
    user.delete()

    log.info('test_betxt_7bit: PASSED')


def test_betxn_attr_uniqueness(topology_st):
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

    attruniq = AttributeUniquenessPlugin(topology_st.standalone, dn="cn=attruniq,cn=plugins,cn=config")
    attruniq.create(properties={'cn': 'attruniq'})
    attruniq.add_unique_attribute('uid')
    attruniq.add_unique_subtree(DEFAULT_SUFFIX)
    attruniq.enable_all_subtrees()
    attruniq.enable()
    topology_st.standalone.restart()

    users = UserAccounts(topology_st.standalone, basedn=DEFAULT_SUFFIX)
    user1 = users.create(properties={
        'uid': 'testuser1',
        'cn': 'testuser1',
        'sn': 'user1',
        'uidNumber': '1001',
        'gidNumber': '2001',
        'homeDirectory': '/home/testuser1'
    })

    with pytest.raises(ldap.LDAPError):
        users.create(properties={
            'uid': ['testuser2', 'testuser1'],
            'cn': 'testuser2',
            'sn': 'user2',
            'uidNumber': '1002',
            'gidNumber': '2002',
            'homeDirectory': '/home/testuser2'
        })

    user1.delete()

    log.info('test_betxn_attr_uniqueness: PASSED')


def test_betxn_memberof(topology_st):
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

    memberof = MemberOfPlugin(topology_st.standalone)
    memberof.enable()
    memberof.set_autoaddoc('referral')
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        singleTXN = False
    else:
        singleTXN = True
    topology_st.standalone.restart()

    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    group1 = groups.create(properties={'cn': 'group1'})
    group2 = groups.create(properties={'cn': 'group2'})

    # We may need to mod groups to not have nsMemberOf ... ?
    if not ds_is_older('1.3.7'):
        group1.remove('objectClass', 'nsMemberOf')
        group2.remove('objectClass', 'nsMemberOf')

    # Add group2 to group1 - it should fail with objectclass violation
    if singleTXN:
        with pytest.raises(ldap.OBJECT_CLASS_VIOLATION):
            group1.add_member(group2.dn)

        # verify entry cache reflects the current/correct state of group1
        assert not group1.is_member(group2.dn)
    else:
        group1.add_member(group2.dn)

        # verify entry cache reflects the current/correct state of group1
        assert group1.is_member(group2.dn)

    # Done
    log.info('test_betxn_memberof: PASSED')


def test_betxn_modrdn_memberof_cache_corruption(topology_st):
    """Test modrdn operations and memberOf be txn post op failures

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
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        singleTXN = False
    else:
        singleTXN = True
    topology_st.standalone.restart()

    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={
        'cn': 'group',
    })

    # Create user and add it to group
    users = UserAccounts(topology_st.standalone, basedn=DEFAULT_SUFFIX)
    user = users.ensure_state(properties=TEST_USER_PROPERTIES)
    if not ds_is_older('1.3.7'):
        user.remove('objectClass', 'nsMemberOf')

    group.add_member(user.dn)

    if singleTXN:
        # Attempt modrdn that should fail, but the original entry should stay in the cache
        with pytest.raises(ldap.OBJECT_CLASS_VIOLATION):
            group.rename('cn=group_to_people', newsuperior=peoplebase)

        # Should fail, but not with NO_SUCH_OBJECT as the original entry should still be in the cache
        with pytest.raises(ldap.OBJECT_CLASS_VIOLATION):
            group.rename('cn=group_to_people', newsuperior=peoplebase)
    else:
        group.rename('cn=group_to_people', newsuperior=peoplebase)
        group.rename('cn=group_to_people', newsuperior=peoplebase)

    # Done
    log.info('test_betxn_modrdn_memberof: PASSED')


def test_ri_and_mep_cache_corruption(topology_st):
    """Test RI plugin aborts change after MEP plugin fails.
    This is really testing the entry cache for corruption

    :id: 70d0b96e-b693-4bf7-bbf5-102a66ac5995

    :setup: Standalone instance

    :steps: 1. Enable and configure mep and ri plugins
            2. Add user and add it to a group
            3. Disable MEP plugin and remove MEP group
            4. Delete user
            5. Check that user is still a member of the group

    :expectedresults:
            1. Success
            2. Success
            3. Success
            4. It fails with NO_SUCH_OBJECT
            5. Success

    """
    # Add ACI so we can test that non-DM user can't delete managed entry
    domain = Domain(topology_st.standalone, DEFAULT_SUFFIX)
    ACI_TARGET = f"(target = \"ldap:///{DEFAULT_SUFFIX}\")"
    ACI_TARGETATTR = "(targetattr = *)"
    ACI_ALLOW = "(version 3.0; acl \"Admin Access\"; allow (all) "
    ACI_SUBJECT = "(userdn = \"ldap:///anyone\");)"
    ACI_BODY = ACI_TARGET + ACI_TARGETATTR + ACI_ALLOW + ACI_SUBJECT
    domain.add('aci', ACI_BODY)

    # Start plugins
    topology_st.standalone.config.set('nsslapd-dynamic-plugins', 'on')
    mep_plugin = ManagedEntriesPlugin(topology_st.standalone)
    mep_plugin.enable()
    ri_plugin = ReferentialIntegrityPlugin(topology_st.standalone)
    ri_plugin.enable()

    # Add our org units
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    ou_people = ous.create(properties={'ou': 'managed_people'})
    ou_groups = ous.create(properties={'ou': 'managed_groups'})

    # Configure MEP
    mep_templates = MEPTemplates(topology_st.standalone, DEFAULT_SUFFIX)
    mep_template1 = mep_templates.create(properties={
        'cn': 'MEP template',
        'mepRDNAttr': 'cn',
        'mepStaticAttr': 'objectclass: groupOfNames|objectclass: extensibleObject'.split('|'),
        'mepMappedAttr': 'cn: $cn|uid: $cn|gidNumber: $uidNumber'.split('|')
    })
    mep_configs = MEPConfigs(topology_st.standalone)
    mep_configs.create(properties={'cn': 'config',
                                   'originScope': ou_people.dn,
                                   'originFilter': 'objectclass=posixAccount',
                                   'managedBase': ou_groups.dn,
                                   'managedTemplate': mep_template1.dn})

    # Add an entry that meets the MEP scope
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                         rdn='ou={}'.format(ou_people.rdn))
    user = users.create(properties={
        'uid': 'test-user1',
        'cn': 'test-user',
        'sn': 'test-user',
        'uidNumber': '10011',
        'gidNumber': '20011',
        'homeDirectory': '/home/test-user1'
    })
    user.reset_password(USER_PASSWORD)
    user_bound_conn = user.bind(USER_PASSWORD)

    # Add group
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    user_group = groups.ensure_state(properties={'cn': 'group', 'member': user.dn})

    # Check if a managed group entry was created
    mep_group = Group(topology_st.standalone, dn='cn={},{}'.format(user.rdn, ou_groups.dn))
    if not mep_group.exists():
        log.fatal("MEP group was not created for the user")
        assert False

    # Test MEP be txn pre op failure does not corrupt entry cache
    # Should get the same exception for both rename attempts
    # Try to remove the entry while bound as Admin (non-DM)
    managed_groups_user_conn = Groups(user_bound_conn, ou_groups.dn, rdn=None)
    managed_entry_user_conn = managed_groups_user_conn.get(user.rdn)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        managed_entry_user_conn.rename("cn=modrdn group")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        managed_entry_user_conn.rename("cn=modrdn group")

    # Mess with MEP so it fails
    mep_plugin.disable()
    users_mep_group = UserAccounts(topology_st.standalone, mep_group.dn, rdn=None)
    users_mep_group.create_test_user(1001)
    mep_plugin.enable()

    # Add another group to verify entry cache is not corrupted
    test_group = groups.create(properties={'cn': 'test_group'})

    # Try to delete user - it fails because managed entry can't be deleted
    with pytest.raises(ldap.NOT_ALLOWED_ON_NONLEAF):
        user.delete()

    # Verify membership is intact
    if not user_group.is_member(user.dn):
        log.fatal("Member was incorrectly removed from the group!!  Or so it seems")

        # Restart server and test again in case this was a cache issue
        topology_st.standalone.restart()
        if user_group.is_member(user.dn):
            log.info("The entry cache was corrupted")
            assert False

        assert False

    # Verify test group is still found in entry cache by deleting it
    test_group.delete()

    # Success
    log.info("Test PASSED")

def test_revert_cache(topology_st, request):
    """Tests that reversion of the entry cache does not occur
    during 'normal' failure (like schema violation) but only
    when a plugin fails

    :id: a2361285-b939-4da0-aa80-7fc54d12c981

    :setup: Standalone instance

    :steps:
         1. Create a user account (with a homeDirectory)
         2. Remove the error log file
         3. Add a second value of homeDirectory
         4. Check that error log does not contain entry cache reversion
         5. Configure memberof to trigger a failure
         6. Do a update the will fail in memberof plugin
         7. Check that error log does contain entry cache reversion

    :expectedresults:
         1. Succeeds
         2. Fails with OBJECT_CLASS_VIOLATION
         3. Succeeds
         4. Succeeds
         5. Succeeds
         6. Fails with OBJECT_CLASS_VIOLATION
         7. Succeeds
    """
    # Create an test user entry
    log.info('Create a user without nsMemberOF objectclass')
    try:
        users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX, rdn='ou=people')
        user = users.create_test_user()
        user.replace('objectclass', ['top', 'account', 'person', 'inetorgperson', 'posixAccount', 'organizationalPerson', 'nsAccount'])
    except ldap.LDAPError as e:
        log.fatal('Failed to create test user: error ' + e.args[0]['desc'])
        assert False

    # Remove the current error log file
    topology_st.standalone.stop()
    lpath = topology_st.standalone.ds_error_log._get_log_path()
    os.unlink(lpath)
    topology_st.standalone.start()

    #
    # Add a second value to 'homeDirectory'
    # leads to ldap.OBJECT_CLASS_VIOLATION
    # And check that the entry cache was not reverted
    try:
        topology_st.standalone.modify_s(user.dn, [(ldap.MOD_ADD, 'homeDirectory', ensure_bytes('/home/user_new'))])
        assert False
    except ldap.OBJECT_CLASS_VIOLATION:
        pass
    assert not topology_st.standalone.ds_error_log.match('.*WARN - flush_hash - Upon BETXN callback failure, entry cache is flushed during.*')

    # Prepare memberof so that it will fail during a next update
    # If memberof plugin can not add 'memberof' to the
    # member entry, it retries after adding
    # 'memberOfAutoAddOC' objectclass to the member.
    # If it fails again the plugin fails with 'object
    # violation'
    # To trigger this failure, set 'memberOfAutoAddOC'
    # to a value that does *not* allow 'memberof' attribute
    memberof = MemberOfPlugin(topology_st.standalone)
    memberof.enable()
    memberof.replace('memberOfAutoAddOC', 'account')
    memberof.replace('memberofentryscope', DEFAULT_SUFFIX)
    topology_st.standalone.restart()

    # Try to add the user to demo_group
    # It should fail because user entry has not 'nsmemberof' objectclass
    # As this is a BETXN plugin that fails it should revert the entry cache
    try:
        GROUP_DN = "cn=demo_group,ou=groups,"  + DEFAULT_SUFFIX
        topology_st.standalone.modify_s(GROUP_DN,
            [(ldap.MOD_REPLACE, 'member', ensure_bytes(user.dn))])
        assert False
    except ldap.OBJECT_CLASS_VIOLATION:
        pass
    assert topology_st.standalone.ds_error_log.match('.*WARN - flush_hash - Upon BETXN callback failure, entry cache is flushed during.*')

    def fin():
        user.delete()
        memberof = MemberOfPlugin(topology_st.standalone)
        memberof.replace('memberOfAutoAddOC', 'nsmemberof')

    request.addfinalizer(fin)

@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_revert_cache_noloop(topology_st, request):
    """Tests that when an entry is reverted, if an update
    hit the reverted entry then the retry loop is aborted
    and the update gets err=51
    NOTE: this test requires a dynamic so that the two updates
    occurs about the same time. If the test becomes fragile it is
    okay to make it flaky

    :id: 88ef0ba5-8c66-49e6-99c9-9e3f6183917f

    :setup: Standalone instance

    :steps:
         1. Create a user account (with a homeDirectory)
         2. Remove the error log file
         3. Configure memberof to trigger a failure
         4. Do in a loop 3 parallel updates (they will fail in
            memberof plugin) and an updated on the reverted entry
         5. Check that error log does contain entry cache reversion

    :expectedresults:
         1. Succeeds
         2. Success
         3. Succeeds
         4. Succeeds
         5. Succeeds
    """
    # Create an test user entry
    log.info('Create a user without nsMemberOF objectclass')
    try:
        users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX, rdn='ou=people')
        user = users.create_test_user()
        user.replace('objectclass', ['top', 'account', 'person', 'inetorgperson', 'posixAccount', 'organizationalPerson', 'nsAccount'])
    except ldap.LDAPError as e:
        log.fatal('Failed to create test user: error ' + e.args[0]['desc'])
        assert False

    # Remove the current error log file
    topology_st.standalone.stop()
    lpath = topology_st.standalone.ds_error_log._get_log_path()
    os.unlink(lpath)
    topology_st.standalone.start()

    # Prepare memberof so that it will fail during a next update
    # If memberof plugin can not add 'memberof' to the
    # member entry, it retries after adding
    # 'memberOfAutoAddOC' objectclass to the member.
    # If it fails again the plugin fails with 'object
    # violation'
    # To trigger this failure, set 'memberOfAutoAddOC'
    # to a value that does *not* allow 'memberof' attribute
    memberof = MemberOfPlugin(topology_st.standalone)
    memberof.enable()
    memberof.replace('memberOfAutoAddOC', 'account')
    memberof.replace('memberofentryscope', DEFAULT_SUFFIX)
    topology_st.standalone.restart()

    for i in range(50):
        # Try to add the user to demo_group
        # It should fail because user entry has not 'nsmemberof' objectclass
        # As this is a BETXN plugin that fails it should revert the entry cache
        try:
            GROUP_DN = "cn=demo_group,ou=groups,"  + DEFAULT_SUFFIX
            topology_st.standalone.modify(GROUP_DN,
                [(ldap.MOD_REPLACE, 'member', ensure_bytes(user.dn))])
            topology_st.standalone.modify(GROUP_DN,
                [(ldap.MOD_REPLACE, 'member', ensure_bytes(user.dn))])
            topology_st.standalone.modify(GROUP_DN,
                [(ldap.MOD_REPLACE, 'member', ensure_bytes(user.dn))])
        except ldap.OBJECT_CLASS_VIOLATION:
            pass

        user.replace('cn', ['new_value'])

    # Check that both a betxn failed and a reverted entry was
    # detected during an update
    assert topology_st.standalone.ds_error_log.match('.*WARN - flush_hash - Upon BETXN callback failure, entry cache is flushed during.*')
    assert topology_st.standalone.ds_error_log.match('.*cache_is_reverted_entry - Entry reverted.*')

    def fin():
        user.delete()
        memberof = MemberOfPlugin(topology_st.standalone)
        memberof.replace('memberOfAutoAddOC', 'nsmemberof')

    request.addfinalizer(fin)
if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
