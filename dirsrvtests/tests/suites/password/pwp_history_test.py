# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import time
import logging
import ldap
from lib389.tasks import *
from lib389.utils import ds_is_newer
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.passwd import password_hash
from lib389.pwpolicy import PwPolicyManager
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

USER_PWD = 'password'
USER_DN = f'uid=test_user_1000,ou=People,{DEFAULT_SUFFIX}'
SUBTREE_DN = f'ou=people,{DEFAULT_SUFFIX}'


def setup_pwp(inst, policy, pwp_mgr, history_count=3):
    """ Setup global password policy """
    dm = DirectoryManager(inst)
    dm.rebind()

    log.info(f'Configuring {policy} password policy with passwordInHistory={history_count}')

    policy_props = {
        'passwordHistory': 'on',
        'passwordInHistory': str(history_count),
        'passwordChange': 'on',
        'passwordStorageScheme': 'CLEAR',
    }

    try:
        if policy == 'global':
            pwp_mgr.set_global_policy(policy_props)
        elif policy == 'subtree':
            pwp_mgr.create_subtree_policy(SUBTREE_DN, policy_props)
        elif policy == 'user':
            pwp_mgr.create_user_policy(USER_DN, policy_props)
        else:
            raise ValueError(f'Invalid type of password policy: {policy}')
    except Exception as e:
        log.fatal(f'Failed to configure {policy} password policy: {e}')
        assert False


def modify_pwp(inst, policy, pwp_mgr, history_count):
    """Modify password policy history count"""
    dm = DirectoryManager(inst)
    dm.rebind()

    try:
        if policy == 'global':
            pwp_mgr.set_global_policy(properties={'passwordInHistory': str(history_count)})
        elif policy == 'subtree':
            policy_entry = pwp_mgr.get_pwpolicy_entry(SUBTREE_DN)
            policy_entry.replace('passwordInHistory', str(history_count))
        elif policy == 'user':
            policy_entry = pwp_mgr.get_pwpolicy_entry(USER_DN)
            policy_entry.replace('passwordInHistory', str(history_count))
        else:
            raise ValueError(f'Invalid type of password policy: {policy}')
        log.info(f'Modified {policy} passwordInHistory to {history_count}.')
    except ldap.LDAPError as e:
        log.fatal(f'Failed to modify {policy} password policy (passwordInHistory to {history_count}): {str(e)}')
        assert False
    except Exception as e:
        log.fatal(f'Failed to modify {policy} password policy: {str(e)}')
        assert False


def change_password(user, password, success=True):
    """Change password"""
    if success:
        try:
            user.set('userpassword', password)
        except ldap.LDAPError as e:
            log.fatal(f'Failed to attempt to change password: {str(e)}')
            log.fatal(f'password history: {str(user.get_attr_vals("passwordhistory"))}')
            assert False
    else:
        try:
            user.set('userpassword', password)
            log.info(f'Incorrectly able to to set password to existing or recent password: {password}.')
            log.fatal(f'password history: {str(user.get_attr_vals("passwordhistory"))}')
            assert False
        except ldap.CONSTRAINT_VIOLATION:
            log.info('Password change correctly rejected')
        except ldap.LDAPError as e:
            log.fatal('Failed to attempt to change password: ' + str(e))
            log.fatal(f'password history: {str(user.get_attr_vals("passwordhistory"))}')
            assert False
    time.sleep(0.5)


@pytest.fixture(scope="function")
def user(topology_st, request):
    """Add and remove a test user"""

    dm = DirectoryManager(topology_st.standalone)

    # Add aci so users can change their own password
    USER_ACI = '(targetattr="userpassword || passwordHistory")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    ou = ous.get('people')
    ou.add('aci', USER_ACI)

    # Create a user
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user()
    user.set('userpassword', USER_PWD)
    def fin():
        dm.rebind()
        user.delete()
        ou.remove('aci', USER_ACI)
    request.addfinalizer(fin)
    return user


def test_history_is_not_overwritten(topology_st, user):
    """Test that passwordHistory user attribute is not overwritten

    :id: 1b311532-dd55-4072-88a9-1f960cb371bd
    :setup: Standalone instance, a test user
    :steps:
        1. Configure password history policy as bellow:
             passwordHistory: on
             passwordInHistory: 3
        2. Change the password 3 times
        3. Try to change the password 2 more times to see
           if it rewrites passwordHistory even on a failure attempt
        4. Try to change the password to the initial value (it should be
           still in history)
    :expectedresults:
        1. Password history policy should be configured successfully
        2. Success
        3. Password changes should be correctly rejected
           with Constrant Violation error
        4. Password change should be correctly rejected
           with Constrant Violation error
    """

    topology_st.standalone.config.replace_many(('passwordHistory', 'on'),
                                               ('passwordInHistory', '3'))
    log.info('Configured password policy.')
    time.sleep(1)

    # Bind as the test user
    user.rebind(USER_PWD)
    time.sleep(.5)

    # Change the password 3 times
    user.set('userpassword', 'password1')
    user.rebind('password1')
    time.sleep(.5)
    user.set('userpassword', 'password2')
    user.rebind('password2')
    time.sleep(.5)
    user.set('userpassword', 'password3')
    user.rebind('password3')
    time.sleep(.5)

    # Try to change the password 2 more times to see
    # if it rewrites passwordHistory even on a failure attempt
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user.set('userpassword', 'password2')
    time.sleep(.5)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user.set('userpassword', 'password1')
    time.sleep(.5)

    # Try to change the password to the initial value (it should be still in history)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user.set('userpassword', USER_PWD)


@pytest.mark.parametrize('policy',
                          [(pytest.param('global')),
                           (pytest.param('subtree')),
                           (pytest.param('user'))])
def test_basic(topology_st, user, policy):
    """Test basic password policy history feature functionality with dynamic count reduction

    :id: 83d74f7d-3036-4944-8839-1b40bbf265ff
    :setup: Standalone instance, a test user
    :steps:
        1. Configure password history policy (global, subtree, or user-level) as below:
             passwordHistory: on
             passwordInHistory: 3
             passwordChange: on
             passwordStorageScheme: CLEAR
        2. Attempt to change password to the same password (should fail)
        3. Change password four times (password1, password2, password3, password4)
        4. Check that we only have 3 passwords stored in history
        5. Attempt to change the password to previous passwords in history (should all fail)
        6. Reduce passwordInHistory from 3 to 2 (dynamic reduction)
        7. Verify that last 2 passwords cannot be used and only 2 passwords stored in history
        8. Verify that password1 can be used again (oldest password becomes available)
        9. Reset password by Directory Manager (admin reset) to 'password-reset'
        10. Try and change the password to the previous password before the reset (should fail)
        11. Test passwordInHistory set to 0: current password still blocked, older passwords available
        12. Set passwordInHistory back to 2: current password blocked, previously stored passwords available
    :expectedresults:
        1. Password history policy should be configured successfully for the specified policy type
        2. Password change should be correctly rejected with Constraint Violation error
        3. Password should be successfully changed four times
        4. Exactly 3 passwords should be stored in history
        5. Password changes should be correctly rejected with Constraint Violation error
        6. Policy change should be successful, history count reduced
        7. Only 2 passwords should remain in history, recent passwords still blocked
        8. Oldest password (password1) should become available for reuse
        9. Password should be successfully reset by admin
        10. Password change should be correctly rejected with Constraint Violation error
        11. When history count is 0: current password blocked, historical passwords allowed
        12. When history count is restored: current password blocked, old passwords allowed
    """
    pwp_mgr = PwPolicyManager(topology_st.standalone)

    # Configure password history policy and add a test user
    setup_pwp(topology_st.standalone, policy, pwp_mgr)

    # Bind as the test user
    user.rebind(USER_PWD)

    # Test that password history is enforced - Attempt to change password to the same password
    change_password(user, USER_PWD, success=False)

    # Keep changing password until we fill the password history (3)
    for pwd in ['password1', 'password2', 'password3', 'password4']:
        change_password(user, pwd, success=True)
        user.rebind(pwd)
        time.sleep(.5)
    # Password history [password1, password2, password3], current password is "password4"

    # Check that we only have 3 passwords stored in history
    pwds = user.get_attr_vals('passwordHistory')
    if len(pwds) != 3:
        log.fatal(f'Incorrect number of passwords stored in history: {len(pwds)}')
        log.error(f'password history: {pwds}')
        assert False
    else:
        log.info('Correct number of passwords found in history.')

    # Attempt to change the password to previous passwords
    for pwd in ['password1', 'password2', 'password3']:
        change_password(user, pwd, success=False)

    # Change the history count to 2
    modify_pwp(topology_st.standalone, policy, pwp_mgr, 2)
    # Password history [password2, password3], current password is "password4"

    # Verify that last 2 password cannot be used
    user.rebind('password4')
    for pwd in ['password2', 'password3']:
        change_password(user, pwd, success=False)

    # Verify that only 2 passwords are stored in history
    pwds = user.get_attr_vals('passwordHistory')
    if len(pwds) != 2:
        log.fatal(f'Incorrect number of passwords stored in history: {len(pwds)}')
        log.error(f'password history: {pwds}')
        assert False
    else:
        log.info('Correct number of passwords found in history.')

    # Verify password1 can be used again
    change_password(user, 'password1', success=True)
    # Password history [password3, password4], current password is "password1"

    # Reset password by Directory Manager(admin reset)
    dm = DirectoryManager(topology_st.standalone)
    dm.rebind()
    time.sleep(.5)
    change_password(user, 'password-reset', success=True)
    # Password history [password4, password1], current password is "password-reset"

    # Try and change the password to the previous password before the reset
    user.rebind('password-reset')
    change_password(user, 'password1', success=False)

    if ds_is_newer("1.4.1.2"):
        # Test passwordInHistory to 0
        modify_pwp(topology_st.standalone, policy, pwp_mgr, 0)
        # Password history empty, current password is "password-reset"

        # Verify current password still cannot be used
        user.rebind('password-reset')
        change_password(user, 'password-reset', success=False)

        # Verify the older passwords in the entry (passwordhistory) can be used
        change_password(user, 'password4', success=True)
        change_password(user, 'password1', success=True)

    # Need to make one successful update so history list is reset
    user.set('userpassword', 'password5')

    # Set the history count back to a positive value and make sure things still work
    # as expected
    modify_pwp(topology_st.standalone, policy, pwp_mgr, 2)
    # Password history empty, current password is "password5"

    # Verify current password still cannot be used
    user.rebind('password5')
    change_password(user, 'password5', success=False)

    # Test that old password that was in history before changing the history count to 0 is not being checked
    change_password(user, 'password4', success=True)
    change_password(user, 'password1', success=True)

    # Done
    log.info('Test suite PASSED.')


def test_prehashed_pwd(topology_st):
    """Test password history is updated with a pre-hashed password change

    :id: 24d08663-f36a-44ab-8f02-b8a3f502925b
    :setup: Standalone instance
    :steps:
        1. Configure password history policy as bellow:
             passwordHistory: on
             passwordChange: on
             nsslapd-allow-hashed-passwords: on
        2. Create ACI to allow users change their password
        3. Add a test user
        4. Attempt to change password using non hased value
        5. Bind with non hashed value
        6. Create a hash value for update
        7. Update user password with hash value
        8. Bind with hashed password cleartext
        9. Check users passwordHistory

    :expectedresults:
        1. Password history policy should be configured successfully
        2. ACI applied correctly
        3. User successfully added
        4. Password change accepted
        5. Successful bind
        6. Hash value created
        7. Password change accepted
        8. Successful bind
        9. Users passwordHistory should contain 2 enteries
    """

    # Configure password history policy and add a test user
    try:
        topology_st.standalone.config.replace_many(('passwordHistory', 'on'),
                                                   ('passwordChange', 'on'),
                                                   ('nsslapd-allow-hashed-passwords', 'on'))
        log.info('Configured password policy.')
    except ldap.LDAPError as e:
        log.fatal('Failed to configure password policy: ' + str(e))
        assert False
    time.sleep(1)

    # Add aci so users can change their own password
    USER_ACI = '(targetattr="userpassword || passwordHistory")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    ou = ous.get('people')
    ou.add('aci', USER_ACI)

    # Create user
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create(properties=TEST_USER_PROPERTIES)
    user.set('userpassword', 'password')
    user.rebind('password')

    # Change user pwd to generate a history of 1 entry
    user.replace('userpassword', 'password1')
    user.rebind('password1')

    #Create pwd hash
    pwd_hash = password_hash('password2', scheme='PBKDF2_SHA256', bin_dir=topology_st.standalone.ds_paths.bin_dir)
    #log.info(pwd_hash)

    # Update user pwd hash
    user.replace('userpassword', pwd_hash)
    time.sleep(2)

    # Bind with hashed password
    user.rebind('password2')

    # Check password history
    pwds = user.get_attr_vals('passwordHistory')
    if len(pwds) != 2:
        log.fatal('Incorrect number of passwords stored in history: %d' %
                  len(pwds))
        log.error('password history: ' + str(pwds))
        assert False
    else:
        log.info('Correct number of passwords found in history.')

    # Done
    log.info('Test suite PASSED.')

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
