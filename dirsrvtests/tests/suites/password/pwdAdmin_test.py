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

from lib389._constants import SUFFIX, DN_DM, PASSWORD, DEFAULT_SUFFIX

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
ADMIN_NAME = 'passwd_admin'
ADMIN_DN = 'cn=%s,%s' % (ADMIN_NAME, SUFFIX)
ADMIN2_NAME = 'passwd_admin2'
ADMIN2_DN = 'cn=%s,%s' % (ADMIN2_NAME, SUFFIX)
ADMIN_PWD = 'adminPassword_1'
ADMIN_GROUP_DN = 'cn=password admin group,%s' % (SUFFIX)
ENTRY_NAME = 'Joe Schmo'
ENTRY_DN = 'cn=%s,%s' % (ENTRY_NAME, SUFFIX)
INVALID_PWDS = ('2_Short', 'No_Number', 'N0Special', '{SSHA}bBy8UdtPZwu8uZna9QOYG3Pr41RpIRVDl8wddw==')


@pytest.fixture(scope="module")
def password_policy(topology_st):
    """Set up password policy
    Create a Password Admin entry;
    Set up password policy attributes in config;
    Add an aci to give everyone full access;
    Test that the setup works
    """

    log.info('test_pwdAdmin_init: Creating Password Administrator entries...')

    # Add Password Admin 1
    try:
        topology_st.standalone.add_s(Entry((ADMIN_DN, {'objectclass': "top extensibleObject".split(),
                                                       'cn': ADMIN_NAME,
                                                       'userpassword': ADMIN_PWD})))
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin_init: Failed to add test user' + ADMIN_DN + ': error ' + e.message['desc'])
        assert False

    # Add Password Admin 2
    try:
        topology_st.standalone.add_s(Entry((ADMIN2_DN, {'objectclass': "top extensibleObject".split(),
                                                        'cn': ADMIN2_NAME,
                                                        'userpassword': ADMIN_PWD})))
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin_init: Failed to add test user ' + ADMIN2_DN + ': error ' + e.message['desc'])
        assert False

    # Add Password Admin Group
    try:
        topology_st.standalone.add_s(Entry((ADMIN_GROUP_DN, {'objectclass': "top groupOfUNiqueNames".split(),
                                                             'cn': 'password admin group',
                                                             'uniquemember': ADMIN_DN,
                                                             'uniquemember': ADMIN2_DN})))
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin_init:  Failed to add group' + ADMIN_GROUP_DN + ': error ' + e.message['desc'])
        assert False

    # Configure password policy
    log.info('test_pwdAdmin_init: Configuring password policy...')
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', 'on'),
                                                    (ldap.MOD_REPLACE, 'passwordCheckSyntax', 'on'),
                                                    (ldap.MOD_REPLACE, 'passwordMinCategories', '1'),
                                                    (ldap.MOD_REPLACE, 'passwordMinTokenLength', '1'),
                                                    (ldap.MOD_REPLACE, 'passwordExp', 'on'),
                                                    (ldap.MOD_REPLACE, 'passwordMinDigits', '1'),
                                                    (ldap.MOD_REPLACE, 'passwordMinSpecials', '1')])
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin_init: Failed configure password policy: ' +
                  e.message['desc'])
        assert False

    #
    # Add an aci to allow everyone all access (just makes things easier)
    #
    log.info('Add aci to allow password admin to add/update entries...')

    ACI_TARGET = "(target = \"ldap:///%s\")" % SUFFIX
    ACI_TARGETATTR = "(targetattr = *)"
    ACI_ALLOW = "(version 3.0; acl \"Password Admin Access\"; allow (all) "
    ACI_SUBJECT = "(userdn = \"ldap:///anyone\");)"
    ACI_BODY = ACI_TARGET + ACI_TARGETATTR + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    try:
        topology_st.standalone.modify_s(SUFFIX, mod)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin_init: Failed to add aci for password admin: ' +
                  e.message['desc'])
        assert False

    #
    # Bind as the future Password Admin
    #
    log.info('test_pwdAdmin_init: Bind as the Password Administrator (before activating)...')
    try:
        topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin_init: Failed to bind as the Password Admin: ' +
                  e.message['desc'])
        assert False

    #
    # Setup our test entry, and test password policy is working
    #
    entry = Entry(ENTRY_DN)
    entry.setValues('objectclass', 'top', 'person')
    entry.setValues('sn', ENTRY_NAME)
    entry.setValues('cn', ENTRY_NAME)

    #
    # Start by attempting to add an entry with an invalid password
    #
    log.info('test_pwdAdmin_init: Attempt to add entries with invalid passwords, these adds should fail...')
    for passwd in INVALID_PWDS:
        failed_as_expected = False
        entry.setValues('userpassword', passwd)
        log.info('test_pwdAdmin_init: Create a regular user entry %s with password (%s)...' %
                 (ENTRY_DN, passwd))
        try:
            topology_st.standalone.add_s(entry)
        except ldap.LDAPError as e:
            # We failed as expected
            failed_as_expected = True
            log.info('test_pwdAdmin_init: Add failed as expected: password (%s) result (%s)'
                     % (passwd, e.message['desc']))

        if not failed_as_expected:
            log.fatal('test_pwdAdmin_init: We were incorrectly able to add an entry ' +
                      'with an invalid password (%s)' % (passwd))
            assert False


def test_pwdAdmin(topology_st, password_policy):
    """Test that password administrators/root DN can
    bypass password syntax/policy

    :id: 5ce316d8-88ef-4248-8b63-90736985dad5
    :setup: Standalone instance, Password Admin entry,
        Password policy configured as below:
            nsslapd-pwpolicy-local: on
            passwordCheckSyntax: on
            passwordMinCategories: 1
            passwordMinTokenLength: 1
            passwordExp: on
            passwordMinDigits: 1
            passwordMinSpecials: 1
    :steps:
        1. Bind as Root DN
        2. Set passwordAdminDn to our admin entry DN
        3. Bind as Password Admin
        4. Add entries with invalid passwords
        5. Delete the entries after each pass
        6. Add the entry for the next round of testing
        7. Bind as root DN
        8. Remove passwordAdminDN attribute
        9. Bind as Password Admin (admin rights are revoked)
        10. Make invalid password updates
        11. Bind as root DN to make the update
        12. Set passwordAdminDn to our admin entry DN
        13. Bind as Password Admin
        14. Make the same password updates
        15. Bind as root DN to make the update
        16. Set passwordAdminDn to admin group entry
        17. Bind as admin2
        18. Make some invalid password updates
        19. Bind back as Root DN
    :expectedresults:
        1. Bind should be successful
        2. passwordAdminDn should be set successful
        3. Bind should be successful
        4. The entries should be successfully added
        5. The entries should be successfully deleted
        6. The entry should be successfully added
        7. Bind should be successful
        8. passwordAdminDn should be removed successful
        9. Bind should be successful
        10. Invalid password updates should fail
        11. Bind should be successful
        12. passwordAdminDn should be set successful
        13. Bind should be successful
        14. The same password updates should pass now
        15. Bind should be successful
        16. passwordAdminDn should be set successful
        17. Bind should be successful
        18. The same password updates should pass now
        19. Bind should be successful
    """

    #
    # Now activate a password administator, bind as root dn to do the config
    # update, then rebind as the password admin
    #
    log.info('test_pwdAdmin: Activate the Password Administator...')

    #
    # Get our test entry
    #
    entry = Entry(ENTRY_DN)
    entry.setValues('objectclass', 'top', 'person')
    entry.setValues('sn', ENTRY_NAME)
    entry.setValues('cn', ENTRY_NAME)

    # Bind as Root DN
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Root DN failed to authenticate: ' +
                  e.message['desc'])
        assert False

    # Set the password admin
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ADMIN_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Failed to add password admin to config: ' +
                  e.message['desc'])
        assert False

    # Bind as Password Admin
    try:
        topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Failed to bind as the Password Admin: ' +
                  e.message['desc'])
        assert False

    #
    # Start adding entries with invalid passwords, delete the entry after each pass.
    #
    for passwd in INVALID_PWDS:
        entry.setValues('userpassword', passwd)
        log.info('test_pwdAdmin: Create a regular user entry %s with password (%s)...' %
                 (ENTRY_DN, passwd))
        try:
            topology_st.standalone.add_s(entry)
        except ldap.LDAPError as e:
            log.fatal('test_pwdAdmin: Failed to add entry with password (%s) result (%s)'
                      % (passwd, e.message['desc']))
            assert False

        log.info('test_pwdAdmin: Successfully added entry (%s)' % ENTRY_DN)

        # Delete entry for the next pass
        try:
            topology_st.standalone.delete_s(ENTRY_DN)
        except ldap.LDAPError as e:
            log.fatal('test_pwdAdmin: Failed to delete entry: %s' %
                      (e.message['desc']))
            assert False

    #
    # Add the entry for the next round of testing (modify password)
    #
    entry.setValues('userpassword', ADMIN_PWD)
    try:
        topology_st.standalone.add_s(entry)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Failed to add entry with valid password (%s) result (%s)' %
                  (passwd, e.message['desc']))
        assert False

    #
    # Deactivate the password admin and make sure invalid password updates fail
    #
    log.info('test_pwdAdmin: Deactivate Password Administator and ' +
             'try invalid password updates...')

    # Bind as root DN
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Root DN failed to authenticate: ' +
                  e.message['desc'])
        assert False

    # Remove password admin
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_DELETE, 'passwordAdminDN', None)])
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Failed to remove password admin from config: ' +
                  e.message['desc'])
        assert False

    # Bind as Password Admin (who is no longer an admin)
    try:
        topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Failed to bind as the Password Admin: ' +
                  e.message['desc'])
        assert False

    #
    # Make invalid password updates that should fail
    #
    for passwd in INVALID_PWDS:
        failed_as_expected = False
        entry.setValues('userpassword', passwd)
        try:
            topology_st.standalone.modify_s(ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', passwd)])
        except ldap.LDAPError as e:
            # We failed as expected
            failed_as_expected = True
            log.info('test_pwdAdmin: Password update failed as expected: password (%s) result (%s)'
                     % (passwd, e.message['desc']))

        if not failed_as_expected:
            log.fatal('test_pwdAdmin: We were incorrectly able to add an invalid password (%s)'
                      % (passwd))
            assert False

    #
    # Now activate a password administator
    #
    log.info('test_pwdAdmin: Activate Password Administator and try updates again...')

    # Bind as root DN to make the update
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Root DN failed to authenticate: ' + e.message['desc'])
        assert False

    # Update config - set the password admin
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ADMIN_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Failed to add password admin to config: ' +
                  e.message['desc'])
        assert False

    # Bind as Password Admin
    try:
        topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Failed to bind as the Password Admin: ' +
                  e.message['desc'])
        assert False

    #
    # Make the same password updates, but this time they should succeed
    #
    for passwd in INVALID_PWDS:
        try:
            topology_st.standalone.modify_s(ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', passwd)])
        except ldap.LDAPError as e:
            log.fatal('test_pwdAdmin: Password update failed unexpectedly: password (%s) result (%s)'
                      % (passwd, e.message['desc']))
            assert False
        log.info('test_pwdAdmin: Password update succeeded (%s)' % passwd)

    #
    # Test Password Admin Group
    #
    log.info('test_pwdAdmin: Testing password admin group...')

    # Bind as root DN to make the update
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Root DN failed to authenticate: ' + e.message['desc'])
        assert False

    # Update config - set the password admin group
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ADMIN_GROUP_DN)])
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Failed to add password admin to config: ' +
                  e.message['desc'])
        assert False

    # Bind as admin2
    try:
        topology_st.standalone.simple_bind_s(ADMIN2_DN, ADMIN_PWD)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Failed to bind as the Password Admin2: ' +
                  e.message['desc'])
        assert False

    # Make some invalid password updates, but they should succeed
    for passwd in INVALID_PWDS:
        try:
            topology_st.standalone.modify_s(ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', passwd)])
        except ldap.LDAPError as e:
            log.fatal('test_pwdAdmin: Password update failed unexpectedly: password (%s) result (%s)'
                      % (passwd, e.message['desc']))
            assert False
        log.info('test_pwdAdmin: Password update succeeded (%s)' % passwd)

    # Cleanup - bind as Root DN for the other tests
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_pwdAdmin: Root DN failed to authenticate: ' + e.message['desc'])
        assert False


def test_pwdAdmin_config_validation(topology_st, password_policy):
    """Check passwordAdminDN for valid and invalid values

    :id: f7049482-41e8-438b-ae18-cdd2612c783a
    :setup: Standalone instance, Password Admin entry,
        Password policy configured as below:
            nsslapd-pwpolicy-local: on
            passwordCheckSyntax: on
            passwordMinCategories: 1
            passwordMinTokenLength: 1
            passwordExp: on
            passwordMinDigits: 1
            passwordMinSpecials: 1
    :steps:
        1. Add multiple attributes - one already exists so just try and add the second one
        2. Set passwordAdminDN attribute to an invalid value (ZZZZZ)
    :expectedresults:
        1. The operation should fail
        2. The operation should fail
    """

    # Add multiple attributes - one already exists so just try and add the second one
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_ADD, 'passwordAdminDN', ENTRY_DN)])
        log.fatal('test_pwdAdmin_config_validation: Incorrectly was able to add two config attributes')
        assert False
    except ldap.LDAPError as e:
        log.info('test_pwdAdmin_config_validation: Failed as expected: ' +
                 e.message['desc'])

    # Attempt to set invalid DN
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_ADD, 'passwordAdminDN', 'ZZZZZ')])
        log.fatal('test_pwdAdmin_config_validation: Incorrectly was able to add invalid DN')
        assert False
    except ldap.LDAPError as e:
        log.info('test_pwdAdmin_config_validation: Failed as expected: ' +
                 e.message['desc'])


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
