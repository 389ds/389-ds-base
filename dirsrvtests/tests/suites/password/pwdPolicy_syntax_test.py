# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
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
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnits

pytestmark = pytest.mark.tier1

USER_DN = 'uid=user,ou=People,%s' % DEFAULT_SUFFIX
USER_RDN = 'user'
USER_ACI = '(targetattr="userpassword")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def password_policy(topology_st):
    """Set global password policy"""

    log.info('Enable global password policy. Check for syntax.')
    topology_st.standalone.config.set('passwordCheckSyntax', 'on')
    topology_st.standalone.config.set('nsslapd-pwpolicy-local', 'off')
    topology_st.standalone.config.set('passwordMinCategories', '1')

    # Add self user modification and anonymous aci
    USER_SELF_MOD_ACI = '(targetattr="userpassword")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'
    ANON_ACI = "(targetattr=\"*\")(version 3.0; acl \"Anonymous Read access\"; allow (read,search,compare) userdn = \"ldap:///anyone\";)"
    suffix = Domain(topology_st.standalone, DEFAULT_SUFFIX)
    suffix.add('aci', USER_SELF_MOD_ACI)
    suffix.add('aci', ANON_ACI)


@pytest.fixture(scope="module")
def create_user(topology_st):
    """Create the test user."""
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    users.create(properties={
        'uid': USER_RDN,
        'cn': USER_RDN,
        'sn': USER_RDN,
        'uidNumber': '3000',
        'gidNumber': '4000',
        'homeDirectory': '/home/user',
        'description': 'd_e_s_c',
        'loginShell': USER_RDN,
        'userPassword': PASSWORD
    })


def setPolicy(inst, attr, value):
    """Bind as Root DN, set policy, and then bind as user"""

    inst.simple_bind_s(DN_DM, PASSWORD)

    # Set the policy value
    value = str(value)
    inst.config.set(attr, value)

    policy = inst.config.get_attr_val_utf8(attr)
    assert policy == value


def resetPasswd(inst):
    """Reset the user password for the next test"""

    # First, bind as the ROOT DN so we can set the password
    inst.simple_bind_s(DN_DM, PASSWORD)

    # Now set the password
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.get(USER_RDN)
    user.reset_password(PASSWORD)


def tryPassword(inst, policy_attr, value, reset_value, pw_bad, pw_good, msg):
    """Attempt to change the users password
    inst: DirSrv Object
    password: password
    msg - error message if failure
    """

    setPolicy(inst, policy_attr, value)
    inst.simple_bind_s(USER_DN, PASSWORD)
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.get(USER_RDN)
    try:
        user.reset_password(pw_bad)
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
    user.reset_password(pw_good)

    # Reset for the next test
    resetPasswd(inst)
    setPolicy(inst, policy_attr, reset_value)


def test_basic(topology_st, create_user, password_policy):
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
        29. Test passwordDictCheck
        30. Test passwordPalindrome
        31. Test passwordMaxSequence for forward number sequence
        32. Test passwordMaxSequence for backward number sequence
        33. Test passwordMaxSequence for forward alpha sequence
        34. Test passwordMaxSequence for backward alpha sequence
        35. Test passwordMaxClassChars for digits
        36. Test passwordMaxClassChars for specials
        37. Test passwordMaxClassChars for lowers
        38. Test passwordMaxClassChars for uppers
        39. Test passwordBadWords using 'redhat' and 'fedora'
        40. Test passwordUserAttrs using description attribute

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
        29. The passwordDictCheck test succeeds
        30. The passwordPalindrome test succeeds
        31. Test passwordMaxSequence for forward number sequence succeeds
        32. Test passwordMaxSequence for backward number sequence succeeds
        33. Test passwordMaxSequence for forward alpha sequence succeeds
        34. Test passwordMaxSequence for backward alpha sequence succeeds
        35. Test passwordMaxClassChars for digits succeeds
        36. Test passwordMaxClassChars for specials succeeds
        37. Test passwordMaxClassChars for lowers succeeds
        38. Test passwordMaxClassChars for uppers succeeds
        39. The passwordBadWords test succeeds
        40. The passwordUserAttrs test succeeds
    """

    #
    # Test each syntax category
    #
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    ou = ous.get('people')
    ou.add('aci', USER_ACI)

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
                'password123', 'too many repeating characters')
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

    if ds_is_newer('1.4.0.13'):
        # Dictionary check
        tryPassword(topology_st.standalone, 'passwordDictCheck', 'on', 'on', 'PASSWORD',
                    '13_#Kad472h', 'Password found in dictionary')

        # Palindromes
        tryPassword(topology_st.standalone, 'passwordPalindrome', 'on', 'on', 'Za12_#_21aZ',
                    '13_#Kad472h', 'Password is palindrome')

        # Sequences
        tryPassword(topology_st.standalone, 'passwordMaxSequence', 3, 0, 'Za1_1234',
                    '13_#Kad472h', 'Max monotonic sequence is not allowed')
        tryPassword(topology_st.standalone, 'passwordMaxSequence', 3, 0, 'Za1_4321',
                    '13_#Kad472h', 'Max monotonic sequence is not allowed')
        tryPassword(topology_st.standalone, 'passwordMaxSequence', 3, 0, 'Za1_abcd',
                    '13_#Kad472h', 'Max monotonic sequence is not allowed')
        tryPassword(topology_st.standalone, 'passwordMaxSequence', 3, 0, 'Za1_dcba',
                    '13_#Kad472h', 'Max monotonic sequence is not allowed')

        # Sequence Sets
        tryPassword(topology_st.standalone, 'passwordMaxSeqSets', 2, 0, 'Za1_123--123',
                    '13_#Kad472h', 'Max monotonic sequence is not allowed')

        # Max characters in a character class
        tryPassword(topology_st.standalone, 'passwordMaxClassChars', 3, 0, 'Za1_9376',
                    '13_#Kad472h', 'Too may consecutive characters from the same class')
        tryPassword(topology_st.standalone, 'passwordMaxClassChars', 3, 0, 'Za1_#$&!',
                    '13_#Kad472h', 'Too may consecutive characters from the same class')
        tryPassword(topology_st.standalone, 'passwordMaxClassChars', 3, 0, 'Za1_ahtf',
                    '13_#Kad472h', 'Too may consecutive characters from the same class')
        tryPassword(topology_st.standalone, 'passwordMaxClassChars', 3, 0, 'Za1_HTSE',
                    '13_#Kad472h', 'Too may consecutive characters from the same class')

        # Bad words
        tryPassword(topology_st.standalone, 'passwordBadWords', 'redhat', 'none', 'Za1_redhat',
                    '13_#Kad472h', 'Too may consecutive characters from the same class')

        # User Attributes
        tryPassword(topology_st.standalone, 'passwordUserAttributes', 'description', 0, 'Za1_d_e_s_c',
                    '13_#Kad472h', 'Password found in user entry')


@pytest.mark.bz1816857
@pytest.mark.ds50875
@pytest.mark.skipif(ds_is_older("1.4.1.18"), reason="Not implemented")
def test_config_set_few_user_attributes(topology_st, create_user, password_policy):
    """Test that we can successfully set multiple values to passwordUserAttributes

    :id: 188e0aee-6e29-4857-910c-27d5606f8c08
    :setup: Standalone instance
    :steps:
        1. Set passwordUserAttributes to "description loginShell"
        2. Verify passwordUserAttributes has the values
        3. Verify passwordUserAttributes enforced the policy
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
    """

    standalone = topology_st.standalone
    standalone.simple_bind_s(DN_DM, PASSWORD)
    standalone.log.info('Set passwordUserAttributes to "description loginShell"')
    standalone.config.set('passwordUserAttributes', 'description loginshell')
    standalone.restart()

    standalone.log.info("Verify passwordUserAttributes has the values")
    user_attrs = standalone.config.get_attr_val_utf8('passwordUserAttributes')
    assert "description" in user_attrs
    assert "loginshell" in user_attrs
    standalone.log.info("Reset passwordUserAttributes")
    standalone.config.remove_all('passwordUserAttributes')

    standalone.log.info("Verify passwordUserAttributes enforced the policy")
    attributes = ['description, loginShell', 'description,loginShell', 'description loginShell']
    values = ['Za1_d_e_s_c', f'Za1_{USER_RDN}', f'Za1_d_e_s_c{USER_RDN}']
    for attr in attributes:
        for value in values:
            tryPassword(standalone, 'passwordUserAttributes', attr, 0, value,
                        '13_#Kad472h', 'Password found in user entry')


@pytest.mark.bz1816857
@pytest.mark.ds50875
@pytest.mark.skipif(ds_is_older("1.4.1.18"), reason="Not implemented")
def test_config_set_few_bad_words(topology_st, create_user, password_policy):
    """Test that we can successfully set multiple values to passwordBadWords

    :id: 2977094c-921c-4b2f-af91-4c7a45ded48b
    :setup: Standalone instance
    :steps:
        1. Set passwordBadWords to "fedora redhat"
        2. Verify passwordBadWords has the values
        3. Verify passwordBadWords enforced the policy
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
    """

    standalone = topology_st.standalone
    standalone.simple_bind_s(DN_DM, PASSWORD)
    standalone.log.info('Set passwordBadWords to "fedora redhat"')
    standalone.config.set('passwordBadWords', 'fedora redhat')

    standalone.restart()

    standalone.log.info("Verify passwordBadWords has the values")
    user_attrs = standalone.config.get_attr_val_utf8('passwordBadWords')
    assert "fedora" in user_attrs
    assert "redhat" in user_attrs
    standalone.log.info("Reset passwordBadWords")
    standalone.config.remove_all('passwordBadWords')

    standalone.log.info("Verify passwordBadWords enforced the policy")
    attributes = ['redhat, fedora', 'redhat,fedora', 'redhat fedora']
    values = ['Za1_redhat_fedora', 'Za1_fedora', 'Za1_redhat']
    for attr in attributes:
        for value in values:
            tryPassword(standalone, 'passwordBadWords', attr, 'none', value,
                        '13_#Kad472h', 'Too may consecutive characters from the same class')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
