import os
import sys
import time
import ldap
import logging
import socket
import pytest
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from constants import *

log = logging.getLogger(__name__)

installation_prefix = None

CONFIG_DN  = 'cn=config'
ADMIN_NAME = 'passwd_admin'
ADMIN_DN   = 'cn=%s,%s' % (ADMIN_NAME, SUFFIX)
ADMIN_PWD  = 'adminPassword_1'
ENTRY_NAME = 'Joe Schmo'
ENTRY_DN   = 'cn=%s,%s' % (ENTRY_NAME, SUFFIX)
INVALID_PWDS = ('2_Short', 'No_Number', 'N0Special', '{SSHA}bBy8UdtPZwu8uZna9QOYG3Pr41RpIRVDl8wddw==')

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
        At the beginning, It may exists a standalone instance.
        It may also exists a backup for the standalone instance.

        Principle:
            If standalone instance exists:
                restart it
            If backup of standalone exists:
                create/rebind to standalone

                restore standalone instance from backup
            else:
                Cleanup everything
                    remove instance
                    remove backup
                Create instance
                Create backup
    '''
    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix

    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the backups
    backup_standalone = standalone.checkBackupFS()

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()
    if instance_standalone:
        # assuming the instance is already stopped, just wait 5 sec max
        standalone.stop(timeout=5)
        standalone.start(timeout=10)

    if backup_standalone:
        # The backup exist, assuming it is correct
        # we just re-init the instance with it
        if not instance_standalone:
            standalone.create()
            # Used to retrieve configuration information (dbdir, confdir...)
            standalone.open()

        # restore standalone instance from backup
        standalone.stop(timeout=10)
        standalone.restoreFS(backup_standalone)
        standalone.start(timeout=10)

    else:
        # We should be here only in two conditions
        #      - This is the first time a test involve standalone instance
        #      - Something weird happened (instance/backup destroyed)
        #        so we discard everything and recreate all

        # Remove the backup. So even if we have a specific backup file
        # (e.g backup_standalone) we clear backup that an instance may have created
        if backup_standalone:
            standalone.clearBackupFS()

        # Remove the instance
        if instance_standalone:
            standalone.delete()

        # Create the instance
        standalone.create()

        # Used to retrieve configuration information (dbdir, confdir...)
        standalone.open()

        # Time to create the backups
        standalone.stop(timeout=10)
        standalone.backupfile = standalone.backupFS()
        standalone.start(timeout=10)

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    #
    # Here we have standalone instance up and running
    # Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyStandalone(standalone)


def test_ticket47900(topology):
    """
        Test that password administrators/root DN can
        bypass password syntax/policy.

        We need to test how passwords are modified in
        existing entries, and when adding new entries.
        
        Create the Password Admin entry, but do not set
        it as an admin yet.  Use the entry to verify invalid
        passwords are caught.  Then activate the password 
        admin and make sure it can bypass password policy.
    """

    # Prepare the Password Administator
    entry = Entry(ADMIN_DN)
    entry.setValues('objectclass', 'top', 'person')
    entry.setValues('sn', ADMIN_NAME)
    entry.setValues('cn', ADMIN_NAME)
    entry.setValues('userpassword', ADMIN_PWD)

    topology.standalone.log.info("Creating Password Administator entry %s..." % ADMIN_DN)
    try:
        topology.standalone.add_s(entry)
    except ldap.LDAPError, e:
        topology.standalone.log.error('Unexpected result ' + e.message['desc'])
        assert False
        topology.standalone.log.error("Failed to add Password Administator %s, error: %s "
                % (ADMIN_DN, e.message['desc']))
        assert False

    topology.standalone.log.info("Configuring password policy...")
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local' , 'on'),
                                                 (ldap.MOD_REPLACE, 'passwordCheckSyntax', 'on'),
                                                 (ldap.MOD_REPLACE, 'passwordMinCategories' , '1'),
                                                 (ldap.MOD_REPLACE, 'passwordMinTokenLength' , '1'),
                                                 (ldap.MOD_REPLACE, 'passwordExp' , 'on'),
                                                 (ldap.MOD_REPLACE, 'passwordMinDigits' , '1'),
                                                 (ldap.MOD_REPLACE, 'passwordMinSpecials' , '1')])
    except ldap.LDAPError, e:
        topology.standalone.log.error('Failed configure password policy: ' + e.message['desc'])
        assert False

    #
    # Add an aci to allow everyone all access (just makes things easier)
    #
    topology.standalone.log.info("Add aci to allow password admin to add/update entries...")

    ACI_TARGET       = "(target = \"ldap:///%s\")" % SUFFIX
    ACI_TARGETATTR   = "(targetattr = *)"
    ACI_ALLOW        = "(version 3.0; acl \"Password Admin Access\"; allow (all) "
    ACI_SUBJECT      = "(userdn = \"ldap:///anyone\");)"
    ACI_BODY         = ACI_TARGET + ACI_TARGETATTR + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    try:
        topology.standalone.modify_s(SUFFIX, mod)
    except ldap.LDAPError, e:
        topology.standalone.log.error('Failed to add aci for password admin: ' + e.message['desc'])
        assert False

    #
    # Bind as the Password Admin
    #
    topology.standalone.log.info("Bind as the Password Administator (before activating)...")
    try:
        topology.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError, e:
        topology.standalone.log.error('Failed to bind as the Password Admin: ' + e.message['desc'])
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
    topology.standalone.log.info("Attempt to add entries with invalid passwords, these adds should fail...")
    for passwd in INVALID_PWDS:
        failed_as_expected = False
        entry.setValues('userpassword', passwd)
        topology.standalone.log.info("Create a regular user entry %s with password (%s)..." % (ENTRY_DN, passwd))
        try:
            topology.standalone.add_s(entry)
        except ldap.LDAPError, e:
            # We failed as expected
            failed_as_expected = True
            topology.standalone.log.info('Add failed as expected: password (%s) result (%s)'
                    % (passwd, e.message['desc']))

        if not failed_as_expected:
            topology.standalone.log.error("We were incorrectly able to add an entry " +
                    "with an invalid password (%s)" % (passwd))
            assert False


    #
    # Now activate a password administator, bind as root dn to do the config
    # update, then rebind as the password admin
    #
    topology.standalone.log.info("Activate the Password Administator...")
    
    # Bind as Root DN
    try:
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError, e:
        topology.standalone.log.error('Root DN failed to authenticate: ' + e.message['desc'])
        assert False 
        
    # Update config
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ADMIN_DN)])
    except ldap.LDAPError, e:
        topology.standalone.log.error('Failed to add password admin to config: ' + e.message['desc'])
        assert False
    
    # Bind as Password Admin
    try:
        topology.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError, e:
        topology.standalone.log.error('Failed to bind as the Password Admin: ' + e.message['desc'])
        assert False

    #
    # Start adding entries with invalid passwords, delete the entry after each pass.
    #
    for passwd in INVALID_PWDS:
        entry.setValues('userpassword', passwd)
        topology.standalone.log.info("Create a regular user entry %s with password (%s)..." % (ENTRY_DN, passwd))
        try:
            topology.standalone.add_s(entry)
        except ldap.LDAPError, e:
            topology.standalone.log.error('Failed to add entry with password (%s) result (%s)'
                    % (passwd, e.message['desc']))
            assert False

        topology.standalone.log.info('Succesfully added entry (%s)' % ENTRY_DN)

        # Delete entry for the next pass
        try:
            topology.standalone.delete_s(ENTRY_DN)
        except ldap.LDAPError, e:
            topology.standalone.log.error('Failed to delete entry: %s' % (e.message['desc']))
            assert False


    #
    # Add the entry for the next round of testing (modify password)
    #
    entry.setValues('userpassword', ADMIN_PWD)
    try:
        topology.standalone.add_s(entry)
    except ldap.LDAPError, e:
        topology.standalone.log.error('Failed to add entry with valid password (%s) result (%s)'
                % (passwd, e.message['desc']))
        assert False

    #
    # Deactivate the password admin and make sure invalid password updates fail
    #
    topology.standalone.log.info("Deactivate Password Administator and try invalid password updates...")
    
    # Bind as root DN
    try:
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError, e:
        topology.standalone.log.error('Root DN failed to authenticate: ' + e.message['desc'])
        assert False 

    # Update config
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_DELETE, 'passwordAdminDN', None)])
    except ldap.LDAPError, e:
        topology.standalone.log.error('Failed to remove password admin from config: ' + e.message['desc'])
        assert False

    # Bind as Password Admin
    try:
        topology.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError, e:
        topology.standalone.log.error('Failed to bind as the Password Admin: ' + e.message['desc'])
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
            topology.standalone.log.info('Password update failed as expected: password (%s) result (%s)'
                    % (passwd, e.message['desc']))

        if not failed_as_expected:
            topology.standalone.log.error("We were incorrectly able to add an invalid password (%s)"
                    % (passwd))
            assert False

    #
    # Now activate a password administator
    #
    topology.standalone.log.info("Activate Password Administator and try updates again...")
    
    # Bind as root DN
    try:
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError, e:
        topology.standalone.log.error('Root DN failed to authenticate: ' + e.message['desc'])
        assert False 

    # Update config
    try:
        topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ADMIN_DN)])
    except ldap.LDAPError, e:
        topology.standalone.log.error('Failed to add password admin to config: ' + e.message['desc'])
        assert False
        
    # Bind as Password Admin
    try:
        topology.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError, e:
        topology.standalone.log.error('Failed to bind as the Password Admin: ' + e.message['desc'])
        assert False

    #
    # Make the same password updates, but this time they should succeed
    #
    for passwd in INVALID_PWDS:
        entry.setValues('userpassword', passwd)
        try:
            topology.standalone.modify_s(ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', passwd)])
        except ldap.LDAPError, e:
            topology.standalone.log.error('Password update failed unexpectedly: password (%s) result (%s)'
                    % (passwd, e.message['desc']))
            assert False
        topology.standalone.log.info('Password update succeeded (%s)' % passwd) 
    #
    # Test passed
    #
    topology.standalone.log.info('Test 47900 Passed.')


def test_ticket47900_final(topology):
    topology.standalone.stop(timeout=10)


def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix = None

    topo = topology(True)
    test_ticket47900(topo)

if __name__ == '__main__':
    run_isolated()

