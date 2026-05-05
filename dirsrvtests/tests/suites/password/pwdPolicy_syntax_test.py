# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
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
from test389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX, PASSWORD, DN_DM
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.pwpolicy import PwPolicyManager

pytestmark = pytest.mark.tier1

USER_DN = 'uid=user,ou=People,%s' % DEFAULT_SUFFIX
USER_RDN = 'user'
PEOPLE_DN = 'ou=people,%s' % DEFAULT_SUFFIX
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


def setSubtreePolicy(inst, policy_entry, attr, value):
    """Bind as Root DN and set an attribute on a subtree password policy entry."""

    inst.simple_bind_s(DN_DM, PASSWORD)
    value = str(value)
    policy_entry.replace(attr, value)
    policy = policy_entry.get_attr_val_utf8(attr)
    assert policy == value


def tryPasswordSubtree(inst, policy_entry, policy_attr, value, reset_value, pw_bad, pw_good, msg):
    """Like tryPassword but applies policy_attr on a local PwPolicyEntry."""

    setSubtreePolicy(inst, policy_entry, policy_attr, value)
    inst.simple_bind_s(USER_DN, PASSWORD)
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.get(USER_RDN)
    try:
        user.reset_password(pw_bad)
        log.fatal('Invalid password was unexpectedly accepted (%s)' %
                  (policy_attr))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Invalid password correctly rejected by subtree %s:  %s' %
                 (policy_attr, msg))
        pass
    except ldap.LDAPError as e:
        log.fatal("Failed to change password: " + str(e))
        assert False

    user.reset_password(pw_good)

    resetPasswd(inst)
    setSubtreePolicy(inst, policy_entry, policy_attr, reset_value)


def _run_password_syntax_checks(try_pw):
    """Run the standard password syntax checks; try_pw matches tryPassword call shape."""

    # Min Length
    try_pw('passwordMinLength', 10, 2, 'passwd',
           'password123', 'length too short')
    # Min Digit
    try_pw('passwordMinDigits', 2, 0, 'passwd',
           'password123', 'does not contain minimum number of digits')
    # Min Alphas
    try_pw('passwordMinAlphas', 2, 0, 'p123456789',
           'password123', 'does not contain minimum number of alphas')
    # Max Repeats
    try_pw('passwordMaxRepeats', 2, 0, 'passsword',
           'password123', 'too many repeating characters')
    # Min Specials
    try_pw('passwordMinSpecials', 2, 0, 'passwd',
           'password_#$',
           'does not contain minimum number of special characters')
    # Min Lowers
    try_pw('passwordMinLowers', 2, 0, 'PASSWORD123',
           'password123',
           'does not contain minimum number of lowercase characters')
    # Min Uppers
    try_pw('passwordMinUppers', 2, 0, 'password',
           'PASSWORD',
           'does not contain minimum number of lowercase characters')
    # Min 8-bit (non-ASCII UTF-8 bytes; high-bit bytes count toward num_8bit)
    try_pw('passwordMin8Bit', 1, 0, 'Za12_ab_XY_9qM',
           'Za12_ab_\u00fcXY_9qM#',
           'does not contain minimum number of 8-bit characters')

    if ds_is_newer('1.4.0.13'):
        # Dictionary check
        try_pw('passwordDictCheck', 'on', 'on', 'PASSWORD',
               '13_#Kad472h', 'Password found in dictionary')

        # Palindromes
        try_pw('passwordPalindrome', 'on', 'on', 'Za12_#_21aZ',
               '13_#Kad472h', 'Password is palindrome')

        # Sequences
        try_pw('passwordMaxSequence', 3, 0, 'Za1_1234',
               '13_#Kad472h', 'Max monotonic sequence is not allowed')
        try_pw('passwordMaxSequence', 3, 0, 'Za1_4321',
               '13_#Kad472h', 'Max monotonic sequence is not allowed')
        try_pw('passwordMaxSequence', 3, 0, 'Za1_abcd',
               '13_#Kad472h', 'Max monotonic sequence is not allowed')
        try_pw('passwordMaxSequence', 3, 0, 'Za1_dcba',
               '13_#Kad472h', 'Max monotonic sequence is not allowed')

        # Sequence Sets
        try_pw('passwordMaxSeqSets', 2, 0, 'Za1_123--123',
               '13_#Kad472h', 'Max monotonic sequence is not allowed')

        # Max characters in a character class
        try_pw('passwordMaxClassChars', 3, 0, 'Za1_9376',
               '13_#Kad472h', 'Too may consecutive characters from the same class')
        try_pw('passwordMaxClassChars', 3, 0, 'Za1_#$&!',
               '13_#Kad472h', 'Too may consecutive characters from the same class')
        try_pw('passwordMaxClassChars', 3, 0, 'Za1_ahtf',
               '13_#Kad472h', 'Too may consecutive characters from the same class')
        try_pw('passwordMaxClassChars', 3, 0, 'Za1_HTSE',
               '13_#Kad472h', 'Too may consecutive characters from the same class')

        # Bad words
        try_pw('passwordBadWords', 'redhat', 'none', 'Za1_redhat',
               '13_#Kad472h', 'Password contains a bad word')

        # User Attributes
        try_pw('passwordUserAttributes', 'description', 0, 'Za1_d_e_s_c',
               '13_#Kad472h', 'Password found in user entry')


def test_basic(topology_st, create_user, password_policy):
    """Ensure that on a password change, the policy syntax
    is enforced correctly.

    :id: e8de7029-7fa6-4e96-9eb6-4a121f4c8fb3
    :customerscenario: True
    :setup: Standalone instance, a test user,
            global password policy with:
            passwordCheckSyntax - on; nsslapd-pwpolicy-local - off;
            passwordMinCategories - 1
    :steps:
        1. Run all syntax checks against cn=config (global policy), including
           passwordMin8Bit using a UTF-8 password containing a Latin-1 letter.
        2. Run the same checks against a subtree password policy on
           ou=people,dc=example,dc=com (with passwordMinCategories 1 on the
           subtree entry), then remove the subtree policy.
    :expectedresults:
        1. Each policy attribute behaves as documented for self-service password change.
        2. Subtree policy enforcement matches global for every check; cleanup succeeds.
    """

    standalone = topology_st.standalone

    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    ou = ous.get('people')
    ou.add('aci', USER_ACI)

    _run_password_syntax_checks(
        lambda *args: tryPassword(standalone, *args))

    log.info('\n\nRepeat syntax checks against subtree password policy at %s', PEOPLE_DN)
    pwp = None
    try:
        pwp = PwPolicyManager(standalone)
        pwp_entry = pwp.create_subtree_policy(
            PEOPLE_DN,
            {'passwordchange': 'on',
             'passwordCheckSyntax': 'on',
             'passwordMinCategories': '1'})
        _run_password_syntax_checks(
            lambda *args: tryPasswordSubtree(standalone, pwp_entry, *args))
    finally:
        standalone.simple_bind_s(DN_DM, PASSWORD)
        if pwp is not None:
            try:
                pwp.delete_local_policy(PEOPLE_DN)
            except ValueError:
                pass
        standalone.config.set('nsslapd-pwpolicy-local', 'off')


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


@pytest.mark.skipif(ds_is_older("1.4.1.18"), reason="Not implemented")
def test_config_set_few_bad_words(topology_st, create_user, password_policy):
    """Test that we can successfully set multiple values to passwordBadWords

    :id: 2977094c-921c-4b2f-af91-4c7a45ded48b
    :setup: Standalone instance
    :steps:
        1. Set passwordBadWords to "fedora redhat"
        2. Verify passwordBadWords has the values
        3. Verify passwordBadWords enforced the policy
        4. Set global passwordBadWords again (distinct from subtree list)
        5. Add subtree password policy for ou=people with passwordBadWords "ubuntu debian"
        6. Verify subtree passwordBadWords and enforcement (comma / space separated)
        7. Remove subtree policy and restore nsslapd-pwpolicy-local
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. Operation should be successful
        5. Operation should be successful
        6. Operation should be successful
        7. Cleanup succeeds
    """

    standalone = topology_st.standalone
    standalone.simple_bind_s(DN_DM, PASSWORD)
    standalone.log.info('Set passwordBadWords to "fedora redhat"')
    standalone.config.set('passwordBadWords', 'fedora redhat')

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
                        '13_#Kad472h', 'Password contains a bad word')

    standalone.log.info(
        '\n\nSubtree password policy at %s with passwordBadWords ubuntu debian '
        '(distinct from global fedora/redhat)' % PEOPLE_DN)
    standalone.simple_bind_s(DN_DM, PASSWORD)
    standalone.config.set('passwordBadWords', 'fedora redhat')

    # Test passwordBadWords using a local password policy
    pwp = PwPolicyManager(standalone)
    try:
        pwp_entry = pwp.create_subtree_policy(
            PEOPLE_DN,
            {'passwordchange': 'on',
             'passwordCheckSyntax': 'on',
             'passwordBadWords': 'ubuntu debian'})

        subtree_attrs = pwp_entry.get_attr_val_utf8('passwordBadWords')
        assert 'ubuntu' in subtree_attrs.lower()
        assert 'debian' in subtree_attrs.lower()
        assert 'fedora' not in subtree_attrs.lower()
        assert 'redhat' not in subtree_attrs.lower()

        subtree_attr_variants = ['ubuntu, debian', 'ubuntu,debian', 'ubuntu debian']
        subtree_values = ['Za1_ubuntu_debian', 'Za1_debian', 'Za1_ubuntu']
        for attr in subtree_attr_variants:
            for value in subtree_values:
                tryPasswordSubtree(
                    standalone, pwp_entry, 'passwordBadWords', attr, 'none', value,
                    '13_#Kad472h',
                    'Password contains a bad word')
    finally:
        standalone.simple_bind_s(DN_DM, PASSWORD)
        try:
            pwp.delete_local_policy(PEOPLE_DN)
        except ValueError:
            pass
        standalone.config.remove_all('passwordBadWords')
        standalone.config.set('nsslapd-pwpolicy-local', 'off')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
