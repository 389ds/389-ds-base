# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX, PASSWORD, DN_DM

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

USER_DN = 'uid=user,ou=People,%s' % DEFAULT_SUFFIX

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def password_policy(topology_st):
    """Set global password policy"""

    log.info('Enable global password policy. Check for syntax.')
    topology_st.standalone.config.set('passwordCheckSyntax', 'on')
    topology_st.standalone.config.set('nsslapd-pwpolicy-local', 'off')
    topology_st.standalone.config.set('passwordMinCategories', '1')


@pytest.fixture(scope="module")
def test_user(topology_st):
    """Create the test user."""

    topology_st.standalone.add_s(Entry((
        USER_DN, {
            'objectClass': 'top account simplesecurityobject'.split(),
            'uid': 'user',
            'userpassword': PASSWORD
        })))


def setPolicy(inst, attr, value):
    """Bind as ROot DN, set polcy, and then bind as user"""

    try:
        inst.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal("Failed to bind as Directory Manager: " + str(e))
        assert False

    value = str(value)
    """
    if value == '0':
        # Remove the policy attribute
        try:
            inst.modify_s("cn=config",
                [(ldap.MOD_DELETE, attr, None)])
        except ldap.LDAPError as e:
            log.fatal("Failed to rmeove password policy %s: %s" %
                      (attr, str(e)))
            assert False
    else:
    """
    # Set the policy value
    inst.config.set(attr, value)

    try:
        inst.simple_bind_s(USER_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal("Failed to bind: " + str(e))
        assert False


def resetPasswd(inst):
    """Reset the user password for the next test"""

    # First, bind as the ROOT DN so we can set the password
    try:
        inst.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal("Failed to bind as Directory Manager: " + str(e))
        assert False

    # Now set the password
    try:
        inst.modify_s(USER_DN,
                      [(ldap.MOD_REPLACE, 'userpassword', ensure_bytes(PASSWORD))])
    except ldap.LDAPError as e:
        log.fatal("Failed to reset user password: " + str(e))
        assert False


def tryPassword(inst, policy_attr, value, reset_value, pw_bad, pw_good, msg):
    """Attempt to change the users password
    inst: DirSrv Object
    password: password
    msg - error message if failure
    """

    setPolicy(inst, policy_attr, value)
    try:
        inst.modify_s(USER_DN,
                      [(ldap.MOD_REPLACE, 'userpassword', ensure_bytes(pw_bad))])
        log.fatal('Invalid password was unexpectedly accepted (%s)' %
                  (policy_attr))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Invalid password correctly rejected by %s:  %s' %
                 (policy_attr, msg))
        pass
    except ldap.LDAPError as e:
        log.fatal("Failed to change password: " + str(e))
        assert False

    # Change password that is allowed
    try:
        inst.modify_s(USER_DN,
                      [(ldap.MOD_REPLACE, 'userpassword', ensure_bytes(pw_good))])
    except ldap.LDAPError as e:
        log.fatal("Failed to change password: " + str(e))
        assert False

    # Reset for the next test
    resetPasswd(inst)
    setPolicy(inst, policy_attr, reset_value)


def test_basic(topology_st, test_user, password_policy):
    """Ensure that on a password change, the policy syntax
    is enforced correctly.

    :id: e8de7029-7fa6-4e96-9eb6-4a121f4c8fb3
    :setup: Standalone instance, a test user,
            global password policy with:
            passwordCheckSyntax - on; nsslapd-pwpolicy-local - off;
            passwordMinCategories - 1
    :steps:
        1. Set passwordMinLength to 10 in cn=config
        2. Set userPassword to 'passwd' in cn=config
        3. Set userPassword to 'password123' in cn=config
        4. Set passwordMinLength to 2 in cn=config
        5. Set passwordMinDigits to 2 in cn=config
        6. Set userPassword to 'passwd' in cn=config
        7. Set userPassword to 'password123' in cn=config
        8. Set passwordMinDigits to 0 in cn=config
        9. Set passwordMinAlphas to 2 in cn=config
        10. Set userPassword to 'p123456789' in cn=config
        11. Set userPassword to 'password123' in cn=config
        12. Set passwordMinAlphas to 0 in cn=config
        13. Set passwordMaxRepeats to 2 in cn=config
        14. Set userPassword to 'password' in cn=config
        15. Set userPassword to 'password123' in cn=config
        16. Set passwordMaxRepeats to 0 in cn=config
        17. Set passwordMinSpecials to 2 in cn=config
        18. Set userPassword to 'passwd' in cn=config
        19. Set userPassword to 'password_#$' in cn=config
        20. Set passwordMinSpecials to 0 in cn=config
        21. Set passwordMinLowers to 2 in cn=config
        22. Set userPassword to 'PASSWORD123' in cn=config
        23. Set userPassword to 'password123' in cn=config
        24. Set passwordMinLowers to 0 in cn=config
        25. Set passwordMinUppers to 2 in cn=config
        26. Set userPassword to 'password' in cn=config
        27. Set userPassword to 'PASSWORD' in cn=config
        28. Set passwordMinUppers to 0 in cn=config
    :expectedresults:
        1. passwordMinLength should be successfully set
        2. Password should be rejected because length too short
        3. Password should be accepted
        4. passwordMinLength should be successfully set
        5. passwordMinDigits should be successfully set
        6. Password should be rejected because
           it does not contain minimum number of digits
        7. Password should be accepted
        8. passwordMinDigits should be successfully set
        9. passwordMinAlphas should be successfully set
        10. Password should be rejected because
            it does not contain minimum number of alphas
        11. Password should be accepted
        12. passwordMinAlphas should be successfully set
        13. passwordMaxRepeats should be successfully set
        14. Password should be rejected because too many repeating characters
        15. Password should be accepted
        16. passwordMaxRepeats should be successfully set
        17. passwordMinSpecials should be successfully set
        18. Password should be rejected because
            it does not contain minimum number of special characters
        19. Password should be accepted
        20. passwordMinSpecials should be successfully set
        21. passwordMinLowers should be successfully set
        22. Password should be rejected because
            it does not contain minimum number of lowercase characters
        23. Password should be accepted
        24. passwordMinLowers should be successfully set
        25. passwordMinUppers should be successfully set
        26. Password should be rejected because
            it does not contain minimum number of lowercase characters
        27. Password should be accepted
        28. passwordMinUppers should be successfully set
    """

    #
    # Test each syntax category
    #

    # Min Length
    tryPassword(topology_st.standalone, 'passwordMinLength', 10, 2, 'passwd',
                'password123', 'length too short')
    # Min Digit
    tryPassword(topology_st.standalone, 'passwordMinDigits', 2, 0, 'passwd',
                'password123', 'does not contain minimum number of digits')
    # Min Alphas
    tryPassword(topology_st.standalone, 'passwordMinAlphas', 2, 0, 'p123456789',
                'password123', 'does not contain minimum number of alphas')
    # Max Repeats
    tryPassword(topology_st.standalone, 'passwordMaxRepeats', 2, 0, 'passsword',
                'pasword123', 'too many repeating characters')
    # Min Specials
    tryPassword(topology_st.standalone, 'passwordMinSpecials', 2, 0, 'passwd',
                'password_#$',
                'does not contain minimum number of special characters')
    # Min Lowers
    tryPassword(topology_st.standalone, 'passwordMinLowers', 2, 0, 'PASSWORD123',
                'password123',
                'does not contain minimum number of lowercase characters')
    # Min Uppers
    tryPassword(topology_st.standalone, 'passwordMinUppers', 2, 0, 'password',
                'PASSWORD',
                'does not contain minimum number of lowercase characters')
    # Min 8-bits - "ldap" package only accepts ascii strings at the moment

    log.info('pwdPolicy tests PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
