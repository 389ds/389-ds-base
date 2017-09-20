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
    """

    USER_DN = 'uid=testuser,' + DEFAULT_SUFFIX

    #
    # Configure password history policy and add a test user
    #
    try:
        topology_st.standalone.modify_s("cn=config",
                                        [(ldap.MOD_REPLACE,
                                          'passwordHistory', 'on'),
                                         (ldap.MOD_REPLACE,
                                          'passwordInHistory', '3'),
                                         (ldap.MOD_REPLACE,
                                          'passwordChange', 'on'),
                                         (ldap.MOD_REPLACE,
                                          'passwordStorageScheme', 'CLEAR')])
        log.info('Configured password policy.')
    except ldap.LDAPError as e:
        log.fatal('Failed to configure password policy: ' + str(e))
        assert False
    time.sleep(1)

    try:
        topology_st.standalone.add_s(Entry((USER_DN, {
            'objectclass': ['top', 'extensibleObject'],
            'sn': 'user',
            'cn': 'test user',
            'uid': 'testuser',
            'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add test user' + USER_DN + ': error ' + str(e))
        assert False

    #
    # Test that password history is enforced.
    #
    try:
        topology_st.standalone.simple_bind_s(USER_DN, 'password')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user: ' + str(e))
        assert False

    # Attempt to change password to the same password
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword', 'password')])
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

    # password1
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword', 'password1')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change password: ' + str(e))
        assert False
    try:
        topology_st.standalone.simple_bind_s(USER_DN, 'password1')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user using "password1": ' + str(e))
        assert False
    time.sleep(1)

    # password2
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword', 'password2')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change password: ' + str(e))
        assert False
    try:
        topology_st.standalone.simple_bind_s(USER_DN, 'password2')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user using "password2": ' + str(e))
        assert False
    time.sleep(1)

    # password3
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword', 'password3')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change password: ' + str(e))
        assert False
    try:
        topology_st.standalone.simple_bind_s(USER_DN, 'password3')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user using "password3": ' + str(e))
        assert False
    time.sleep(1)

    # password4
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword', 'password4')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change password: ' + str(e))
        assert False
    try:
        topology_st.standalone.simple_bind_s(USER_DN, 'password4')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user using "password4": ' + str(e))
        assert False
    time.sleep(1)

    #
    # Check that we only have 3 passwords stored in history
    #
    try:
        entry = topology_st.standalone.search_s(USER_DN, ldap.SCOPE_BASE,
                                                'objectclass=*',
                                                ['passwordHistory'])
        pwds = entry[0].getValues('passwordHistory')
        if len(pwds) != 3:
            log.fatal('Incorrect number of passwords stored in histry: %d' %
                      len(pwds))
            assert False
        else:
            log.info('Correct number of passwords found in history.')
    except ldap.LDAPError as e:
        log.fatal('Failed to get user entry: ' + str(e))
        assert False

    #
    # Attempt to change the password to previous passwords
    #
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword', 'password1')])
        log.info('Incorrectly able to to set password to previous password1.')
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False
    time.sleep(1)

    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword', 'password2')])
        log.info('Incorrectly able to to set password to previous password2.')
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword', 'password3')])
        log.info('Incorrectly able to to set password to previous password3.')
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

    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword',
                                                   'password-reset')])
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to reset password: ' + str(e))
        assert False
    time.sleep(1)

    # Try and change the password to the previous password before the reset
    try:
        topology_st.standalone.simple_bind_s(USER_DN, 'password-reset')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user: ' + str(e))
        assert False
    time.sleep(1)

    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword', 'password4')])
        log.info('Incorrectly able to to set password to previous password4.')
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    log.info('Test suite PASSED.')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
