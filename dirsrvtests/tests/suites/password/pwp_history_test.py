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
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

USER_PWD = 'password'


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


def test_basic(topology_st, user):
    """Test basic password policy history feature functionality

    :id: 83d74f7d-3036-4944-8839-1b40bbf265ff
    :setup: Standalone instance, a test user
    :steps:
        1. Configure password history policy as bellow:
             passwordHistory: on
             passwordInHistory: 3
             passwordChange: on
             passwordStorageScheme: CLEAR
        2. Attempt to change password to the same password
        3. Change password four times
        4. Check that we only have 3 passwords stored in history
        5. Attempt to change the password to previous passwords
        6. Reset password by Directory Manager (admin reset)
        7. Try and change the password to the previous password before the reset
        8. Test passwordInHistory set to "0" rejects only the current password
        9. Test passwordInHistory set to "2" rejects previous passwords
    :expectedresults:
        1. Password history policy should be configured successfully
        2. Password change should be correctly rejected
           with Constrant Violation error
        3. Password should be successfully changed
        4. Only 3 passwords should be stored in history
        5. Password changes should be correctly rejected
           with Constrant Violation error
        6. Password should be successfully reset
        7. Password change should be correctly rejected
           with Constrant Violation error
        8. Success
        9. Success
    """

    #
    # Configure password history policy and add a test user
    #
    try:
        topology_st.standalone.config.replace_many(('passwordHistory', 'on'),
                                                   ('passwordInHistory', '3'),
                                                   ('passwordChange', 'on'),
                                                   ('passwordStorageScheme', 'CLEAR'),
                                                   ('nsslapd-auditlog-logging-enabled', 'on'))
        log.info('Configured password policy.')
    except ldap.LDAPError as e:
        log.fatal('Failed to configure password policy: ' + str(e))
        assert False
    time.sleep(1)

    # Bind as the test user
    user.rebind(USER_PWD)

    #
    # Test that password history is enforced.
    #
    # Attempt to change password to the same password
    try:
        user.set('userpassword', 'password')
        log.info('Incorrectly able to to set password to existing password.')
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    #
    # Keep changing password until we fill the password history (3)
    #
    user.set('userpassword', 'password1')
    user.rebind('password1')
    time.sleep(.5)
    user.set('userpassword', 'password2')
    user.rebind('password2')
    time.sleep(.5)
    user.set('userpassword', 'password3')
    user.rebind('password3')
    time.sleep(.5)
    user.set('userpassword', 'password4')
    user.rebind('password4')
    time.sleep(.5)

    #
    # Check that we only have 3 passwords stored in history
    #
    pwds = user.get_attr_vals('passwordHistory')
    if len(pwds) != 3:
        log.fatal('Incorrect number of passwords stored in history: %d' %
                  len(pwds))
        log.error('password history: ' + str(pwds))
        assert False
    else:
        log.info('Correct number of passwords found in history.')

    #
    # Attempt to change the password to previous passwords
    #
    try:
        user.set('userpassword', 'password1')
        log.fatal('Incorrectly able to to set password to previous password1.')
        log.fatal('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False
    try:
        user.set('userpassword', 'password2')
        log.fatal('Incorrectly able to to set password to previous password2.')
        log.fatal('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False
    try:
        user.set('userpassword', 'password3')
        log.fatal('Incorrectly able to to set password to previous password3.')
        log.fatal('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    #
    # Reset password by Directory Manager(admin reset)
    #
    dm = DirectoryManager(topology_st.standalone)
    dm.rebind()
    time.sleep(.5)
    user.set('userpassword', 'password-reset')
    time.sleep(1)

    # Try and change the password to the previous password before the reset
    try:
        user.rebind('password-reset')
        user.set('userpassword', 'password4')
        log.fatal('Incorrectly able to to set password to previous password4.')
        log.fatal('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    if ds_is_newer("1.4.1.2"):
        #
        # Test passwordInHistory to 0
        #
        dm = DirectoryManager(topology_st.standalone)
        dm.rebind()
        try:
            topology_st.standalone.config.replace('passwordInHistory', '0')
            log.info('Configured passwordInHistory to 0.')
        except ldap.LDAPError as e:
            log.fatal('Failed to configure password policy (passwordInHistory to 0): ' + str(e))
            assert False
        time.sleep(1)

        # Verify the older passwords in the entry (passwordhistory) are ignored
        user.rebind('password-reset')
        user.set('userpassword', 'password4')
        time.sleep(.5)
        try:
            user.set('userpassword', 'password4')
            log.fatal('Incorrectly able to to set password to current password4.')
            log.fatal('password history: ' + str(user.get_attr_vals('passwordhistory')))
            assert False
        except ldap.CONSTRAINT_VIOLATION:
            log.info('Password change correctly rejected')
        except ldap.LDAPError as e:
            log.fatal('Failed to attempt to change password: ' + str(e))
            assert False

    # Need to make one successful update so history list is reset
    user.set('userpassword', 'password5')

    #
    # Set the history count back to a positive value and make sure things still work
    # as expected
    #
    dm = DirectoryManager(topology_st.standalone)
    dm.rebind()
    try:
        topology_st.standalone.config.replace('passwordInHistory', '2')
        log.info('Configured passwordInHistory to 2.')
    except ldap.LDAPError as e:
        log.fatal('Failed to configure password policy (passwordInHistory to 2): ' + str(e))
        assert False
    time.sleep(1)

    try:
        user.rebind('password5')
        user.set('userpassword', 'password5')
        log.fatal('Incorrectly able to to set password to current password5.')
        log.fatal('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    # Test that old password that was in history is not being checked
    try:
        user.set('userpassword', 'password1')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        log.fatal('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False

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
    log.info(pwd_hash)

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
