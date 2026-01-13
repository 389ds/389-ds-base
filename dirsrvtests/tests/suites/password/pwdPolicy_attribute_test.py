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
from lib389.pwpolicy import PwPolicyManager
from lib389.idm.user import UserAccount, UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389._constants import (DEFAULT_SUFFIX, DN_DM, PASSWORD)
from lib389.idm.directorymanager import DirectoryManager

pytestmark = pytest.mark.tier1

OU_PEOPLE = 'ou=people,{}'.format(DEFAULT_SUFFIX)
TEST_USER_NAME = 'simplepaged_test'
TEST_USER_DN = 'uid={},{}'.format(TEST_USER_NAME, OU_PEOPLE)
TEST_USER_PWD = 'simplepaged_test'
PW_POLICY_CONT_USER = 'cn="cn=nsPwPolicyEntry,uid=simplepaged_test,' \
                      'ou=people,dc=example,dc=com",' \
                      'cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'
PW_POLICY_CONT_PEOPLE = 'cn="cn=nsPwPolicyEntry,' \
                        'ou=people,dc=example,dc=com",' \
                        'cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def add_test_user(topology_st, request):
    """User for binding operation"""
    topology_st.standalone.config.set('nsslapd-auditlog-logging-enabled', 'on')
    log.info('Adding test user {}')
    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': TEST_USER_NAME, 'userpassword': TEST_USER_PWD})
    try:
        user = users.create(properties=user_props)
    except:
        pass  # debug only

    USER_ACI = '(targetattr="*")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///%s";)' % user.dn
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    ou_people = ous.get('people')
    ou_people.add('aci', USER_ACI)

    def fin():
        log.info('Deleting user {}'.format(user.dn))
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    request.addfinalizer(fin)
    return user


@pytest.fixture(scope="function")
def password_policy(topology_st, request, add_test_user):
    """Set up password policy for subtree and user"""

    pwp = PwPolicyManager(topology_st.standalone)
    policy_props = {}
    log.info(f"Create password policy for subtree {OU_PEOPLE}")
    try:
        pwp.create_subtree_policy(OU_PEOPLE, policy_props)
    except ldap.ALREADY_EXISTS:
        log.info(f"Subtree password policy for {OU_PEOPLE} already exist, skipping")

    log.info(f"Create password policy for user {TEST_USER_DN}")
    try:
        pwp.create_user_policy(TEST_USER_DN, policy_props)
    except ldap.ALREADY_EXISTS:
        log.info(f"User password policy for {TEST_USER_DN} already exist, skipping")

    def fin():
        log.info(f"Delete password policy for subtree {OU_PEOPLE}")
        try:
            pwp.delete_local_policy(OU_PEOPLE)
        except ValueError:
            log.info(f"Subtree password policy for {OU_PEOPLE} doesn't exist, skipping")

        log.info(f"Delete password policy for user {TEST_USER_DN}")
        try:
            pwp.delete_local_policy(TEST_USER_DN)
        except ValueError:
            log.info(f"User password policy for {TEST_USER_DN} doesn't exist, skipping")

    request.addfinalizer(fin)


@pytest.mark.skipif(ds_is_older('1.4.3.3'), reason="Not implemented")
def test_pwdReset_by_user_DM(topology_st, add_test_user):
    """Test new password policy attribute "pwdReset"

    :id: 232bc7dc-8cb6-11eb-9791-98fa9ba19b65
    :customerscenario: True
    :setup: Standalone instance, Add a new user with a password
    :steps:
        1. Enable passwordMustChange
        2. Bind as the user and change the password
        3. Check that the pwdReset attribute is set to TRUE
        4. Bind as the Directory manager and attempt to change the pwdReset to FALSE
        5. Check that pwdReset is NOT SET to FALSE
    :expectedresults:
        1. Success
        2. Success
        3. Successful bind as DS user, pwdReset as DS user fails w UNWILLING_TO_PERFORM
        4. Success
        5. Success
    """

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    log.info('Set password policy passwordMustChange on')
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    our_user.replace('userpassword', PASSWORD)
    time.sleep(5)

    # Check that pwdReset is TRUE
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    log.info('Binding as the Directory manager and attempt to change the pwdReset to FALSE')
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology_st.standalone.config.replace('pwdReset', 'FALSE')

    log.info('Check that pwdReset is NOT SET to FALSE')
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    log.info('Resetting password for {}'.format(TEST_USER_PWD))
    our_user.reset_password(TEST_USER_PWD)


@pytest.mark.skipif(ds_is_older('1.4.3.3'), reason="Not implemented")
def test_pwd_reset(topology_st, add_test_user):
    """Test new password policy attribute "pwdReset"

    :id: 03db357b-4800-411e-a36e-28a534293004
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Reset user's password
        3. Check that the pwdReset attribute is set to TRUE
        4. Bind as the user and change its password
        5. Check that pwdReset is now set to FALSE
        6. Reset password policy configuration
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    # Set password policy config
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    time.sleep(.5)

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    our_user.replace('userpassword', PASSWORD)
    time.sleep(.5)

    # Check that pwdReset is TRUE
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Bind as user and change its own password
    our_user.rebind(PASSWORD)
    our_user.replace('userpassword', PASSWORD)
    time.sleep(.5)

    # Check that pwdReset is FALSE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert our_user.get_attr_val_utf8('pwdReset') == 'FALSE'

    # Reset password policy config
    topology_st.standalone.config.replace('passwordMustChange', 'off')

    # Reset user's password
    our_user.replace('userpassword', TEST_USER_PWD)


@pytest.mark.parametrize('subtree_pwchange,user_pwchange,exception',
                         [('on', 'off', ldap.UNWILLING_TO_PERFORM),
                          ('off', 'off', ldap.UNWILLING_TO_PERFORM),
                          ('off', 'on', False), ('on', 'on', False)])
def test_change_pwd(topology_st, add_test_user, password_policy,
                    subtree_pwchange, user_pwchange, exception):
    """Verify that 'passwordChange' attr works as expected
    User should have a priority over a subtree.

    :id: 2c884432-2ba1-4662-8e5d-2cd49f77e5fa
    :parametrized: yes
    :setup: Standalone instance, a test user,
            password policy entries for a user and a subtree
    :steps:
        1. Set passwordChange on the user and the subtree
           to various combinations
        2. Bind as test user
        3. Try to change password
        4. Clean up - change the password to default while bound as DM
    :expectedresults:
        1. passwordChange should be successfully set
        2. Bind should be successful
        3. Subtree/User passwordChange - result, accordingly:
           off/on, on/on - success;
           on/off, off/off - UNWILLING_TO_PERFORM
        4. Operation should be successful
    """

    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user = users.get(TEST_USER_NAME)

    log.info('Set passwordChange to "{}" - {}'.format(subtree_pwchange, OU_PEOPLE))
    pwp = PwPolicyManager(topology_st.standalone)
    subtree_policy = pwp.get_pwpolicy_entry(OU_PEOPLE)
    subtree_policy.set('passwordChange', subtree_pwchange)

    time.sleep(1)

    log.info('Set passwordChange to "{}" - {}'.format(user_pwchange, TEST_USER_DN))
    pwp2 = PwPolicyManager(topology_st.standalone)
    user_policy = pwp2.get_pwpolicy_entry(TEST_USER_DN)
    user_policy.set('passwordChange', user_pwchange)
    user_policy.set('passwordExp', 'on')

    time.sleep(1)

    try:
        log.info('Bind as user and modify userPassword')
        user.rebind(TEST_USER_PWD)
        if exception:
            with pytest.raises(exception):
                user.reset_password('new_pass')
        else:
            user.reset_password('new_pass')
    except ldap.LDAPError as e:
        log.error('Failed to change userpassword for {}: error {}'.format(
            TEST_USER_DN, e.args[0]['info']))
        raise e
    finally:
        log.info('Bind as DM')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        user.reset_password(TEST_USER_PWD)


def test_pwd_min_age(topology_st, add_test_user, password_policy):
    """If we set passwordMinAge to some value, for example to 10, then it
    should not allow the user to change the password within 10 seconds after
    his previous change.

    :id: 85b98516-8c82-45bd-b9ec-90bd1245e09c
    :setup: Standalone instance, a test user,
            password policy entries for a user and a subtree
    :steps:
        1. Set passwordMinAge to 10 on the user pwpolicy entry
        2. Set passwordMinAge to 10 on the subtree pwpolicy entry
        3. Set passwordMinAge to 10 on the cn=config entry
        4. Bind as test user
        5. Try to change the password two times in a row
        6. Wait 12 seconds
        7. Try to change the password
        8. Clean up - change the password to default while bound as DM
    :expectedresults:
        1. passwordMinAge should be successfully set on the user pwpolicy entry
        2. passwordMinAge should be successfully set on the subtree pwpolicy entry
        3. passwordMinAge should be successfully set on the cn=config entry
        4. Bind should be successful
        5. The password should be successfully changed
        6. 12 seconds have passed
        7. Constraint Violation error should be raised
        8. Operation should be successful
    """

    num_seconds = '10'
    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user = users.get(TEST_USER_NAME)

    log.info('Set passwordminage to "{}" - {}'.format(num_seconds, OU_PEOPLE))
    pwp = PwPolicyManager(topology_st.standalone)
    subtree_policy = pwp.get_pwpolicy_entry(OU_PEOPLE)
    subtree_policy.set('passwordminage', num_seconds)

    log.info('Set passwordminage to "{}" - {}'.format(num_seconds, TEST_USER_DN))
    user_policy = pwp.get_pwpolicy_entry(TEST_USER_DN)
    user_policy.set('passwordminage', num_seconds)

    log.info('Set passwordminage to "{}" - {}'.format(num_seconds, DN_CONFIG))
    topology_st.standalone.config.set('passwordminage', num_seconds)

    time.sleep(1)

    log.info('Bind as user and modify userPassword')
    user.rebind(TEST_USER_PWD)
    user.reset_password('new_pass')

    time.sleep(1)

    log.info('Bind as user and modify userPassword straight away after previous change')
    user.rebind('new_pass')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user.reset_password('new_new_pass')

    log.info('Wait {} second'.format(int(num_seconds) + 2))
    time.sleep(int(num_seconds) + 2)

    try:
        log.info('Bind as user and modify userPassword')
        user.rebind('new_pass')
        user.reset_password(TEST_USER_PWD)
    except ldap.LDAPError as e:
        log.error('Failed to change userpassword for {}: error {}'.format(
            TEST_USER_DN, e.args[0]['info']))
        raise e
    finally:
        log.info('Bind as DM')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        user.reset_password(TEST_USER_PWD)


def test_pwdpolicysubentry(topology_st, password_policy):
    """Verify that 'pwdpolicysubentry' attr works as expected
    User should have a priority over a subtree.

    :id: 4ab0c62a-623b-40b4-af67-99580c77b36c
    :setup: Standalone instance, a test user,
            password policy entries for a user and a subtree
    :steps:
        1. Create a subtree policy
        2. Create a user policy
        3. Search for 'pwdpolicysubentry' in the user entry
        4. Delete the user policy
        5. Search for 'pwdpolicysubentry' in the user entry
    :expectedresults:
        1. Success
        2. Success
        3. Should point to the user policy entry
        4. Success
        5. Should point to the subtree policy entry

    """

    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user = users.get(TEST_USER_NAME)

    pwp_subentry = user.get_attr_vals_utf8('pwdpolicysubentry')[0]
    assert 'nsPwPolicyEntry_subtree' not in pwp_subentry
    assert 'nsPwPolicyEntry_user' in pwp_subentry

    pwp = PwPolicyManager(topology_st.standalone)
    pwp.delete_local_policy(TEST_USER_DN)
    pwp_subentry = user.get_attr_vals_utf8('pwdpolicysubentry')[0]
    assert 'nsPwPolicyEntry_subtree' in pwp_subentry
    assert 'nsPwPolicyEntry_user' not in pwp_subentry


@pytest.fixture(scope="function")
def shadowUser(request, topology_st):
    """ Create a user with shadowAccount objectclass """
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    shadowUser = users.create(properties={
        'objectclass': ['top', 'person', 'organizationalPerson',
                        'inetOrgPerson', 'extensibleObject', 'shadowAccount'],
        'sn': '1',
        'cn': 'shadowUser',
        'uid': 'shadowUser',
        'uidNumber': '1',
        'gidNumber': '11',
        'homeDirectory': '/home/shadowUser',
        'displayName': 'Shadow User',
        'givenname': 'Shadow',
        'mail':f'shadowuser@{DEFAULT_SUFFIX}',
        'userpassword': 'password'
    })

    def fin():
        if shadowUser.exists():
            shadowUser.delete()

    request.addfinalizer(fin)

    return shadowUser


def days_to_secs(days):
    """ Convert days to seconds """
    return days * 86400


def check_shadow_attr_value(inst, user_dn, attr_type, expected):
    """ Check that shadowAccount attribute has expected value """
    dm = DirectoryManager(inst)
    dm.rebind()
    user = UserAccount(inst, user_dn)
    assert user.present(attr_type), f'Entry {user_dn} does not have {attr_type} attribute'
    actual = int(user.get_attr_val_utf8(attr_type))
    assert actual == expected, f'{attr_type} of entry {user_dn} is {actual}, expected {expected}'
    log.info(f'{attr_type} of entry {user_dn} has expected value {actual}')


def setup_pwp(inst, pwp_mgr, policy, dn=None, policy_props=None):
    """ Setup password policy """

    log.info(f'Setting up {policy} password policy for {dn}')
    dm = DirectoryManager(inst)
    dm.rebind()

    log.info(f'Configuring {policy} password policy')

    assert policy == 'global' or dn, 'dn is required for non-global policy'

    if not policy_props:
        policy_props = {
            'passwordMinAge': str(days_to_secs(1)),
            'passwordExp': 'on',
            'passwordMaxAge': str(days_to_secs(10)),
            'passwordWarning': str(days_to_secs(3))
        }

    if policy == 'global':
        pwp_mgr.set_global_policy(policy_props)
    elif policy == 'subtree':
        pwp_mgr.create_subtree_policy(dn, policy_props)
    elif policy == 'user':
        pwp_mgr.create_user_policy(dn, policy_props)
    else:
        raise ValueError(f'Invalid type of password policy: {policy}')


def modify_pwp(inst, pwp_mgr, policy, dn=None, policy_props=None):
    """ Modify password policy """
    dm = DirectoryManager(inst)
    dm.rebind()

    assert policy == 'global' or dn, 'dn is required for non-global policy'

    if not policy_props:
        policy_props = {
            'passwordMinAge': str(days_to_secs(3)),
            'passwordMaxAge': str(days_to_secs(30)),
            'passwordWarning': str(days_to_secs(9))
        }

    if policy == 'global':
        pwp_mgr.set_global_policy(properties=policy_props)
    elif policy in ['subtree', 'user']:
        policy_entry = pwp_mgr.get_pwpolicy_entry(dn)
        policy_entry.replace_many(*policy_props.items())
    else:
        raise ValueError(f'Invalid type of password policy: {policy}')
    log.info(f'Modified {policy} policy with {policy_props}.')

@pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")
def test_shadowaccount_no_policy(topology_st, shadowUser):
    """Check shadowAccount under no password policy

    :id: a1b2c3d4-5e6f-7890-abcd-ef1234567890
    :setup: Standalone instance
    :steps:
        1. Add a user with shadowAccount objectclass
        2. Bind as the user
        3. Check shadowLastChange attribute is set correctly
    :expectedresults:
        1. User is added successfully
        2. Bind is successful
        3. shadowLastChange is set correctly (days since epoch)
    """

    edate = int(time.time() / (60 * 60 * 24))

    log.info(f"Bind as {shadowUser.dn}")
    shadowUser.rebind('password')
    check_shadow_attr_value(topology_st.standalone, shadowUser.dn,
                            'shadowLastChange', edate)


@pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")
def test_shadowaccount_global_policy(topology_st, shadowUser, request):
    """Check shadowAccount with global password policy

    :id: b2c3d4e5-6f7a-8901-bcde-f23456789012
    :setup: Standalone instance
    :steps:
        1. Set global password policy
        2. Add a second shadowAccount user
        3. Bind as each user and check shadowAccount attributes
        4. Modify global password policy
        5. Change user password (as the user, not DM)
        6. Re-bind with new password
        7. Check shadowAccount attributes are updated
        8. Clean up - delete second user and reset policy
    :expectedresults:
        1. Global password policy is set successfully
        2. Second user is added
        3. shadowAccount attributes match policy values for both users
        4. Password policy is modified successfully
        5. Password is changed successfully
        6. Re-bind with new password is successful
        7. shadowAccount attributes are updated to match new policy values
        8. Cleanup is successful
    """
    inst = topology_st.standalone

    log.info('Create second shadowAccount user')
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    shadowUser2 = users.create(properties={
        'objectclass': ['top', 'person', 'organizationalPerson',
                        'inetOrgPerson', 'extensibleObject', 'shadowAccount'],
        'sn': '2',
        'cn': 'shadowUser2',
        'uid': 'shadowUser2',
        'uidNumber': '2',
        'gidNumber': '22',
        'homeDirectory': '/home/shadowUser2',
        'displayName': 'Shadow User 2',
        'givenname': 'Shadow2',
        'mail': f'shadowuser2@{DEFAULT_SUFFIX}',
        'userpassword': 'password'
    })

    def fin():
        log.info('Clean up - delete second user and reset global policy')
        dm = DirectoryManager(inst)
        dm.rebind()
        try:
            shadowUser2.delete()
        except Exception:
            pass
        inst.config.replace('passwordMinAge', '0')
        inst.config.replace('passwordMaxAge', '8640000')
        inst.config.replace('passwordWarning', '86400')
        inst.config.replace('passwordExp', 'off')
    request.addfinalizer(fin)

    log.info('Configure global password policy')
    pwp_mgr = PwPolicyManager(inst)
    setup_pwp(inst, pwp_mgr, 'global')

    edate = int(time.time() / (60 * 60 * 24))

    log.info('Verify attributes of shadowUser (user 1)')
    shadowUser.rebind('password')
    check_shadow_attr_value(inst, shadowUser.dn,
                            'shadowLastChange', edate)
    check_shadow_attr_value(inst, shadowUser.dn,
                            'shadowMin', 1)
    check_shadow_attr_value(inst, shadowUser.dn,
                            'shadowMax', 10)
    check_shadow_attr_value(inst, shadowUser.dn,
                            'shadowWarning', 3)

    log.info('Verify attributes of shadowUser2 (user 2)')
    shadowUser2.rebind('password')
    check_shadow_attr_value(inst, shadowUser2.dn,
                            'shadowLastChange', edate)
    check_shadow_attr_value(inst, shadowUser2.dn,
                            'shadowMin', 1)
    check_shadow_attr_value(inst, shadowUser2.dn,
                            'shadowMax', 10)
    check_shadow_attr_value(inst, shadowUser2.dn,
                            'shadowWarning', 3)

    log.info('Modify global password policy')
    modify_pwp(inst, pwp_mgr, 'global')

    log.info('Change shadowUser2 password as the user')
    shadowUser2.rebind('password')
    shadowUser2.replace('userpassword', 'password2')
    time.sleep(1)

    log.info('Re-bind as shadowUser2 with new password')
    shadowUser2.rebind('password2')

    log.info('Verify modified shadowUser2 attributes')
    check_shadow_attr_value(inst, shadowUser2.dn,
                            'shadowMin', 3)
    check_shadow_attr_value(inst, shadowUser2.dn,
                            'shadowMax', 30)
    check_shadow_attr_value(inst, shadowUser2.dn,
                            'shadowWarning', 9)


@pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")
def test_shadowaccount_subtree_policy(topology_st, request):
    """Check shadowAccount with subtree level password policy

    :id: c3d4e5f6-7a8b-9012-cdef-345678901234
    :setup: Standalone instance
    :steps:
        1. Create subtree password policy for DEFAULT_SUFFIX with passwordMustChange on
        2. Add a new shadowAccount user under the subtree
        3. Check shadowLastChange is 0 (since passwordMustChange is on)
        4. Verify search as user fails with UNWILLING_TO_PERFORM
        5. Change user password (as user)
        6. Re-bind with new password
        7. Check shadowAccount attributes are updated with correct values
        8. Clean up subtree password policy
    :expectedresults:
        1. Subtree password policy is created successfully
        2. User is added successfully
        3. shadowLastChange is 0 until password is changed
        4. Search fails with UNWILLING_TO_PERFORM as expected
        5. Password is changed successfully
        6. Re-bind with new password is successful
        7. shadowAccount attributes are updated to match policy values
        8. Subtree password policy is deleted successfully
    """
    inst = topology_st.standalone
    subtree_dn = DEFAULT_SUFFIX

    log.info('Configure subtree password policy with passwordMustChange on')
    properties = {
        'passwordMustChange': 'on',
        'passwordExp': 'on',
        'passwordMinAge': str(days_to_secs(2)),
        'passwordMaxAge': str(days_to_secs(20)),
        'passwordWarning': str(days_to_secs(6)),
        'passwordChange': 'on',
        'passwordStorageScheme': 'clear'
    }

    pwp_mgr = PwPolicyManager(inst)
    setup_pwp(inst, pwp_mgr, 'subtree', dn=subtree_dn, policy_props=properties)

    def fin():
        log.info('Clean up: delete subtree password policy')
        dm = DirectoryManager(inst)
        dm.rebind()
        try:
            pwp_mgr.delete_local_policy(subtree_dn)
        except Exception:
            pass
        try:
            subtree_user.delete()
        except Exception:
            pass
    request.addfinalizer(fin)

    log.info('Add a new shadowAccount user under the subtree')
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    subtree_user = users.create(properties={
        'objectclass': ['top', 'person', 'organizationalPerson',
                        'inetOrgPerson', 'extensibleObject', 'shadowAccount'],
        'sn': '3',
        'cn': 'subtreeUser',
        'uid': 'subtreeUser',
        'uidNumber': '3',
        'gidNumber': '33',
        'homeDirectory': '/home/subtreeUser',
        'displayName': 'Subtree User',
        'givenname': 'Subtree',
        'mail': f'subtreeuser@{DEFAULT_SUFFIX}',
        'userpassword': 'password'
    })

    dm = DirectoryManager(inst)
    dm.rebind()

    log.info('Verify shadowLastChange is 0 since passwordMustChange is on')
    check_shadow_attr_value(inst, subtree_user.dn,
                            'shadowLastChange', 0)
    check_shadow_attr_value(inst, subtree_user.dn,
                            'shadowMin', 2)
    check_shadow_attr_value(inst, subtree_user.dn,
                            'shadowMax', 20)
    check_shadow_attr_value(inst, subtree_user.dn,
                            'shadowWarning', 6)

    log.info(f'Bind as {subtree_user.dn} and verify search fails with UNWILLING_TO_PERFORM')
    subtree_user.rebind('password')
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        subtree_user.exists()

    log.info('Modify subtree password policy')
    dm.rebind()
    modify_properties = {
        'passwordMinAge': str(days_to_secs(4)),
        'passwordMaxAge': str(days_to_secs(40)),
        'passwordWarning': str(days_to_secs(12))
    }
    modify_pwp(inst, pwp_mgr, 'subtree', dn=subtree_dn, policy_props=modify_properties)

    log.info(f'Change {subtree_user.dn} password as the user')
    subtree_user.rebind('password')
    subtree_user.replace('userpassword', 'password0')
    time.sleep(1)

    log.info(f'Re-bind as {subtree_user.dn} with new password')
    subtree_user.rebind('password0')

    edate = int(time.time() / (60 * 60 * 24))

    log.info('Verify shadowLastChange is now set to today after password change')
    check_shadow_attr_value(inst, subtree_user.dn,
                            'shadowLastChange', edate)
    check_shadow_attr_value(inst, subtree_user.dn,
                            'shadowMin', 4)
    check_shadow_attr_value(inst, subtree_user.dn,
                            'shadowMax', 40)
    check_shadow_attr_value(inst, subtree_user.dn,
                            'shadowWarning', 12)


@pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")
def test_shadowaccount_user_policy(topology_st, request):
    """Check shadowAccount with user level password policy

    :id: d4e5f6a7-8b9c-0123-def0-456789012345
    :setup: Standalone instance
    :steps:
        1. Create a new shadowAccount user
        2. Create user password policy
        3. Verify shadowAccount attributes match policy values
        4. Modify user password policy
        5. Change user password
        6. Re-bind with new password
        7. Check shadowAccount attributes are updated
        8. Clean up user password policy
    :expectedresults:
        1. User is created successfully
        2. User password policy is created successfully
        3. shadowAccount attributes match policy values
        4. Password policy is modified successfully
        5. Password is changed successfully
        6. Re-bind with new password is successful
        7. shadowAccount attributes are updated to match new policy values
        8. User password policy is deleted successfully
    """
    inst = topology_st.standalone

    log.info('Create a new shadowAccount user for user policy test')
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user_policy_user = users.create(properties={
        'objectclass': ['top', 'person', 'organizationalPerson',
                        'inetOrgPerson', 'extensibleObject', 'shadowAccount'],
        'sn': '4',
        'cn': 'userPolicyUser',
        'uid': 'userPolicyUser',
        'uidNumber': '4',
        'gidNumber': '44',
        'homeDirectory': '/home/userPolicyUser',
        'displayName': 'User Policy User',
        'givenname': 'UserPolicy',
        'mail': f'userpolicyuser@{DEFAULT_SUFFIX}',
        'userpassword': 'password'
    })

    pwp_mgr = PwPolicyManager(inst)

    def fin():
        log.info('Clean up: delete user password policy and user')
        dm = DirectoryManager(inst)
        dm.rebind()
        try:
            pwp_mgr.delete_local_policy(user_policy_user.dn)
        except Exception:
            pass
        try:
            user_policy_user.delete()
        except Exception:
            pass
    request.addfinalizer(fin)

    log.info('Configure user password policy')
    properties = {
        'passwordExp': 'on',
        'passwordMinAge': str(days_to_secs(2)),
        'passwordMaxAge': str(days_to_secs(20)),
        'passwordWarning': str(days_to_secs(6)),
        'passwordChange': 'on',
        'passwordStorageScheme': 'clear'
    }
    setup_pwp(inst, pwp_mgr, 'user', dn=user_policy_user.dn, policy_props=properties)

    edate = int(time.time() / (60 * 60 * 24))

    dm = DirectoryManager(inst)
    dm.rebind()

    log.info('Verify shadowAccount attributes match user policy')
    check_shadow_attr_value(inst, user_policy_user.dn,
                            'shadowLastChange', edate)
    check_shadow_attr_value(inst, user_policy_user.dn,
                            'shadowMin', 2)
    check_shadow_attr_value(inst, user_policy_user.dn,
                            'shadowMax', 20)
    check_shadow_attr_value(inst, user_policy_user.dn,
                            'shadowWarning', 6)

    log.info('Modify user password policy')
    modify_properties = {
        'passwordMinAge': str(days_to_secs(4)),
        'passwordMaxAge': str(days_to_secs(40)),
        'passwordWarning': str(days_to_secs(12))
    }
    modify_pwp(inst, pwp_mgr, 'user', dn=user_policy_user.dn, policy_props=modify_properties)

    log.info(f'Change {user_policy_user.dn} password as the user')
    user_policy_user.rebind('password')
    user_policy_user.replace('userpassword', 'password0')
    time.sleep(1)

    log.info(f'Re-bind as {user_policy_user.dn} with new password')
    user_policy_user.rebind('password0')

    edate = int(time.time() / (60 * 60 * 24))

    log.info('Verify shadowAccount attributes are updated after password change')
    check_shadow_attr_value(inst, user_policy_user.dn,
                            'shadowLastChange', edate)
    check_shadow_attr_value(inst, user_policy_user.dn,
                            'shadowMin', 4)
    check_shadow_attr_value(inst, user_policy_user.dn,
                            'shadowMax', 40)
    check_shadow_attr_value(inst, user_policy_user.dn,
                            'shadowWarning', 12)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
