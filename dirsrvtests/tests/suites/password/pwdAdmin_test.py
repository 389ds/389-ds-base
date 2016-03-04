import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None
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


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_pwdAdmin_init(topology):
    '''
    Create our future Password Admin entry, set the password policy, and test
    that its working
    '''

    log.info('test_pwdAdmin_init: Creating Password Administator entries...')

    # Add Password Admin 1
    try:
        topology.standalone.add_s(Entry((ADMIN_DN, {'objectclass': "top extensibleObject".split(),
                                 'cn': ADMIN_NAME,
                                 'userpassword': ADMIN_PWD})))
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin_init: Failed to add test user' + ADMIN_DN + ': error ' + e.message['desc'])
        assert False

    # Add Password Admin 2
    try:
        topology.standalone.add_s(Entry((ADMIN2_DN, {'objectclass': "top extensibleObject".split(),
                                      'cn': ADMIN2_NAME,
                                      'userpassword': ADMIN_PWD})))
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin_init: Failed to add test user ' + ADMIN2_DN + ': error ' + e.message['desc'])
        assert False

    # Add Password Admin Group
    try:
        topology.standalone.add_s(Entry((ADMIN_GROUP_DN, {'objectclass': "top groupOfUNiqueNames".split(),
                                      'cn': 'password admin group',
                                      'uniquemember': ADMIN_DN,
                                      'uniquemember': ADMIN2_DN})))
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin_init:  Failed to add group' + ADMIN_GROUP_DN + ': error ' + e.message['desc'])
        assert False

    # Configure password policy
    log.info('test_pwdAdmin_init: Configuring password policy...')
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', 'on'),
                                                 (ldap.MOD_REPLACE, 'passwordCheckSyntax', 'on'),
                                                 (ldap.MOD_REPLACE, 'passwordMinCategories', '1'),
                                                 (ldap.MOD_REPLACE, 'passwordMinTokenLength', '1'),
                                                 (ldap.MOD_REPLACE, 'passwordExp', 'on'),
                                                 (ldap.MOD_REPLACE, 'passwordMinDigits', '1'),
                                                 (ldap.MOD_REPLACE, 'passwordMinSpecials', '1')])
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin_init: Failed configure password policy: ' +
                  e.message['desc'])
        assert False

    #
    # Add an aci to allow everyone all access (just makes things easier)
    #
    log.info('Add aci to allow password admin to add/update entries...')

    ACI_TARGET       = "(target = \"ldap:///%s\")" % SUFFIX
    ACI_TARGETATTR   = "(targetattr = *)"
    ACI_ALLOW        = "(version 3.0; acl \"Password Admin Access\"; allow (all) "
    ACI_SUBJECT      = "(userdn = \"ldap:///anyone\");)"
    ACI_BODY         = ACI_TARGET + ACI_TARGETATTR + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    try:
        topology.standalone.modify_s(SUFFIX, mod)
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin_init: Failed to add aci for password admin: ' +
                  e.message['desc'])
        assert False

    #
    # Bind as the future Password Admin
    #
    log.info('test_pwdAdmin_init: Bind as the Password Administator (before activating)...')
    try:
        topology.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError, e:
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
            topology.standalone.add_s(entry)
        except ldap.LDAPError, e:
            # We failed as expected
            failed_as_expected = True
            log.info('test_pwdAdmin_init: Add failed as expected: password (%s) result (%s)'
                    % (passwd, e.message['desc']))

        if not failed_as_expected:
            log.fatal('test_pwdAdmin_init: We were incorrectly able to add an entry ' +
                      'with an invalid password (%s)' % (passwd))
            assert False


def test_pwdAdmin(topology):
    '''
        Test that password administrators/root DN can
        bypass password syntax/policy.

        We need to test how passwords are modified in
        existing entries, and when adding new entries.

        Create the Password Admin entry, but do not set
        it as an admin yet.  Use the entry to verify invalid
        passwords are caught.  Then activate the password
        admin and make sure it can bypass password policy.
    '''

    #
    # Now activate a password administator, bind as root dn to do the config
    # update, then rebind as the password admin
    #
    log.info('test_pwdAdmin: Activate the Password Administator...')

    #
    # Setup our test entry, and test password policy is working
    #
    entry = Entry(ENTRY_DN)
    entry.setValues('objectclass', 'top', 'person')
    entry.setValues('sn', ENTRY_NAME)
    entry.setValues('cn', ENTRY_NAME)

    # Bind as Root DN
    try:
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Root DN failed to authenticate: ' +
                  e.message['desc'])
        assert False

    # Set the password admin
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ADMIN_DN)])
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Failed to add password admin to config: ' +
                  e.message['desc'])
        assert False

    # Bind as Password Admin
    try:
        topology.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError, e:
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
            topology.standalone.add_s(entry)
        except ldap.LDAPError, e:
            log.fatal('test_pwdAdmin: Failed to add entry with password (%s) result (%s)'
                      % (passwd, e.message['desc']))
            assert False

        log.info('test_pwdAdmin: Successfully added entry (%s)' % ENTRY_DN)

        # Delete entry for the next pass
        try:
            topology.standalone.delete_s(ENTRY_DN)
        except ldap.LDAPError, e:
            log.fatal('test_pwdAdmin: Failed to delete entry: %s' %
                      (e.message['desc']))
            assert False

    #
    # Add the entry for the next round of testing (modify password)
    #
    entry.setValues('userpassword', ADMIN_PWD)
    try:
        topology.standalone.add_s(entry)
    except ldap.LDAPError, e:
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
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Root DN failed to authenticate: ' +
                  e.message['desc'])
        assert False

    # Remove password admin
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_DELETE, 'passwordAdminDN', None)])
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Failed to remove password admin from config: ' +
                  e.message['desc'])
        assert False

    # Bind as Password Admin (who is no longer an admin)
    try:
        topology.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError, e:
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
            topology.standalone.modify_s(ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', passwd)])
        except ldap.LDAPError, e:
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
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Root DN failed to authenticate: ' + e.message['desc'])
        assert False

    # Update config - set the password admin
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ADMIN_DN)])
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Failed to add password admin to config: ' +
                  e.message['desc'])
        assert False

    # Bind as Password Admin
    try:
        topology.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Failed to bind as the Password Admin: ' +
                  e.message['desc'])
        assert False

    #
    # Make the same password updates, but this time they should succeed
    #
    for passwd in INVALID_PWDS:
        try:
            topology.standalone.modify_s(ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', passwd)])
        except ldap.LDAPError, e:
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
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Root DN failed to authenticate: ' + e.message['desc'])
        assert False

    # Update config - set the password admin group
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ADMIN_GROUP_DN)])
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Failed to add password admin to config: ' +
                  e.message['desc'])
        assert False

    # Bind as admin2
    try:
        topology.standalone.simple_bind_s(ADMIN2_DN, ADMIN_PWD)
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Failed to bind as the Password Admin2: ' +
                  e.message['desc'])
        assert False

    # Make some invalid password updates, but they should succeed
    for passwd in INVALID_PWDS:
        try:
            topology.standalone.modify_s(ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', passwd)])
        except ldap.LDAPError, e:
            log.fatal('test_pwdAdmin: Password update failed unexpectedly: password (%s) result (%s)'
                    % (passwd, e.message['desc']))
            assert False
        log.info('test_pwdAdmin: Password update succeeded (%s)' % passwd)

    # Cleanup - bind as Root DN for the other tests
    try:
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError, e:
        log.fatal('test_pwdAdmin: Root DN failed to authenticate: ' + e.message['desc'])
        assert False


def test_pwdAdmin_config_validation(topology):
    '''
    Test config validation:

    - Test adding multiple passwordAdminDN attributes
    - Test adding invalid values(non-DN's)
    '''
    # Add mulitple attributes - one already eists so just try and add as second one
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_ADD, 'passwordAdminDN', ENTRY_DN)])
        log.fatal('test_pwdAdmin_config_validation: Incorrectly was able to add two config attributes')
        assert False
    except ldap.LDAPError, e:
        log.info('test_pwdAdmin_config_validation: Failed as expected: ' +
                 e.message['desc'])

    # Attempt to set invalid DN
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_ADD, 'passwordAdminDN', 'ZZZZZ')])
        log.fatal('test_pwdAdmin_config_validation: Incorrectly was able to add invalid DN')
        assert False
    except ldap.LDAPError, e:
        log.info('test_pwdAdmin_config_validation: Failed as expected: ' +
                 e.message['desc'])


def test_pwdAdmin_final(topology):
    topology.standalone.delete()
    log.info('pwdAdmin test suite PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_pwdAdmin_init(topo)
    test_pwdAdmin(topo)
    test_pwdAdmin_config_validation(topo)
    test_pwdAdmin_final(topo)


if __name__ == '__main__':
    run_isolated()

