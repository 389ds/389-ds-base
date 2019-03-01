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
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389._constants import DN_DM, DEFAULT_SUFFIX, PASSWORD

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_basic(topology_st):
    """Test basic password policy history feature functionality

    :id: 83d74f7d-3036-4944-8839-1b40bbf265ff
    :setup: Standalone instance
    :steps:
        1. Configure password history policy as bellow:
             passwordHistory: on
             passwordInHistory: 3
             passwordChange: on
             passwordStorageScheme: CLEAR
        2. Add a test user
        3. Attempt to change password to the same password
        4. Change password four times
        5. Check that we only have 3 passwords stored in history
        6. Attempt to change the password to previous passwords
        7. Reset password by Directory Manager (admin reset)
        8. Try and change the password to the previous password before the reset
        9. Test passwordInHistory set to "0" rejects only the current password
        10. Test passwordInHistory set to "2" rejects previous passwords


    :expectedresults:
        1. Password history policy should be configured successfully
        2. User should be added successfully
        3. Password change should be correctly rejected
           with Constrant Violation error
        4. Password should be successfully changed
        5. Only 3 passwords should be stored in history
        6. Password changes should be correctly rejected
           with Constrant Violation error
        7. Password should be successfully reset
        8. Password change should be correctly rejected
           with Constrant Violation error
        9. Success
        10. Success
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
    user.set('userpassword', 'password2')
    user.rebind('password2')
    user.set('userpassword', 'password3')
    user.rebind('password3')
    user.set('userpassword', 'password4')
    user.rebind('password4')
    time.sleep(1)

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
        log.error('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False
    try:
        user.set('userpassword', 'password2')
        log.fatal('Incorrectly able to to set password to previous password2.')
        log.error('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False
    try:
        user.set('userpassword', 'password3')
        log.fatal('Incorrectly able to to set password to previous password3.')
        log.error('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    #
    # Reset password by Directory Manager(admin reset)
    #
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as rootDN: ' + str(e))
        assert False
    user.set('userpassword', 'password-reset')
    time.sleep(1)

    # Try and change the password to the previous password before the reset
    try:
        user.rebind('password-reset')
        user.set('userpassword', 'password4')
        log.fatal('Incorrectly able to to set password to previous password4.')
        log.error('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    #
    # Test passwordInHistory to 0
    #
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as rootDN: ' + str(e))
        assert False

    try:
        topology_st.standalone.config.replace('passwordInHistory', '0')
        log.info('Configured passwordInHistory to 0.')
    except ldap.LDAPError as e:
        log.fatal('Failed to configure password policy (passwordInHistory to 0): ' + str(e))
        assert False

    # Verify the older passwords in the entry (passwordhistory) are ignored
    user.rebind('password-reset')
    user.set('userpassword', 'password4')
    try:
        user.set('userpassword', 'password4')
        log.fatal('Incorrectly able to to set password to current password4.')
        log.error('password history: ' + str(user.get_attr_vals('passwordhistory')))
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
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as rootDN: ' + str(e))
        assert False

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
        log.error('password history: ' + str(user.get_attr_vals('passwordhistory')))
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
        log.error('password history: ' + str(user.get_attr_vals('passwordhistory')))
        assert False

    # Done
    log.info('Test suite PASSED.')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
