# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st

log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
ADMIN_NAME = 'passwd_admin'
ADMIN_DN = 'cn=%s,%s' % (ADMIN_NAME, SUFFIX)
ADMIN_PWD = 'adminPassword_1'
ENTRY_NAME = 'Joe Schmo'
ENTRY_DN = 'cn=%s,%s' % (ENTRY_NAME, SUFFIX)
INVALID_PWDS = ('2_Short', 'No_Number', 'N0Special', '{SSHA}bBy8UdtPZwu8uZna9QOYG3Pr41RpIRVDl8wddw==')


def test_ticket47900(topology_st):
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

    topology_st.standalone.log.info("Creating Password Administator entry %s..." % ADMIN_DN)
    try:
        topology_st.standalone.add_s(entry)
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Unexpected result ' + e.message['desc'])
        assert False
        topology_st.standalone.log.error("Failed to add Password Administator %s, error: %s "
                                         % (ADMIN_DN, e.message['desc']))
        assert False

    topology_st.standalone.log.info("Configuring password policy...")
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', 'on'),
                                                    (ldap.MOD_REPLACE, 'passwordCheckSyntax', 'on'),
                                                    (ldap.MOD_REPLACE, 'passwordMinCategories', '1'),
                                                    (ldap.MOD_REPLACE, 'passwordMinTokenLength', '1'),
                                                    (ldap.MOD_REPLACE, 'passwordExp', 'on'),
                                                    (ldap.MOD_REPLACE, 'passwordMinDigits', '1'),
                                                    (ldap.MOD_REPLACE, 'passwordMinSpecials', '1')])
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Failed configure password policy: ' + e.message['desc'])
        assert False

    #
    # Add an aci to allow everyone all access (just makes things easier)
    #
    topology_st.standalone.log.info("Add aci to allow password admin to add/update entries...")

    ACI_TARGET = "(target = \"ldap:///%s\")" % SUFFIX
    ACI_TARGETATTR = "(targetattr = *)"
    ACI_ALLOW = "(version 3.0; acl \"Password Admin Access\"; allow (all) "
    ACI_SUBJECT = "(userdn = \"ldap:///anyone\");)"
    ACI_BODY = ACI_TARGET + ACI_TARGETATTR + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    try:
        topology_st.standalone.modify_s(SUFFIX, mod)
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Failed to add aci for password admin: ' + e.message['desc'])
        assert False

    #
    # Bind as the Password Admin
    #
    topology_st.standalone.log.info("Bind as the Password Administator (before activating)...")
    try:
        topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Failed to bind as the Password Admin: ' + e.message['desc'])
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
    topology_st.standalone.log.info("Attempt to add entries with invalid passwords, these adds should fail...")
    for passwd in INVALID_PWDS:
        failed_as_expected = False
        entry.setValues('userpassword', passwd)
        topology_st.standalone.log.info("Create a regular user entry %s with password (%s)..." % (ENTRY_DN, passwd))
        try:
            topology_st.standalone.add_s(entry)
        except ldap.LDAPError as e:
            # We failed as expected
            failed_as_expected = True
            topology_st.standalone.log.info('Add failed as expected: password (%s) result (%s)'
                                            % (passwd, e.message['desc']))

        if not failed_as_expected:
            topology_st.standalone.log.error("We were incorrectly able to add an entry " +
                                             "with an invalid password (%s)" % (passwd))
            assert False

    #
    # Now activate a password administator, bind as root dn to do the config
    # update, then rebind as the password admin
    #
    topology_st.standalone.log.info("Activate the Password Administator...")

    # Bind as Root DN
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Root DN failed to authenticate: ' + e.message['desc'])
        assert False

    # Update config
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ADMIN_DN)])
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Failed to add password admin to config: ' + e.message['desc'])
        assert False

    # Bind as Password Admin
    try:
        topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Failed to bind as the Password Admin: ' + e.message['desc'])
        assert False

    #
    # Start adding entries with invalid passwords, delete the entry after each pass.
    #
    for passwd in INVALID_PWDS:
        entry.setValues('userpassword', passwd)
        topology_st.standalone.log.info("Create a regular user entry %s with password (%s)..." % (ENTRY_DN, passwd))
        try:
            topology_st.standalone.add_s(entry)
        except ldap.LDAPError as e:
            topology_st.standalone.log.error('Failed to add entry with password (%s) result (%s)'
                                             % (passwd, e.message['desc']))
            assert False

        topology_st.standalone.log.info('Succesfully added entry (%s)' % ENTRY_DN)

        # Delete entry for the next pass
        try:
            topology_st.standalone.delete_s(ENTRY_DN)
        except ldap.LDAPError as e:
            topology_st.standalone.log.error('Failed to delete entry: %s' % (e.message['desc']))
            assert False

    #
    # Add the entry for the next round of testing (modify password)
    #
    entry.setValues('userpassword', ADMIN_PWD)
    try:
        topology_st.standalone.add_s(entry)
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Failed to add entry with valid password (%s) result (%s)'
                                         % (passwd, e.message['desc']))
        assert False

    #
    # Deactivate the password admin and make sure invalid password updates fail
    #
    topology_st.standalone.log.info("Deactivate Password Administator and try invalid password updates...")

    # Bind as root DN
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Root DN failed to authenticate: ' + e.message['desc'])
        assert False

    # Update config
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_DELETE, 'passwordAdminDN', None)])
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Failed to remove password admin from config: ' + e.message['desc'])
        assert False

    # Bind as Password Admin
    try:
        topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Failed to bind as the Password Admin: ' + e.message['desc'])
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
            topology_st.standalone.log.info('Password update failed as expected: password (%s) result (%s)'
                                            % (passwd, e.message['desc']))

        if not failed_as_expected:
            topology_st.standalone.log.error("We were incorrectly able to add an invalid password (%s)"
                                             % (passwd))
            assert False

    #
    # Now activate a password administator
    #
    topology_st.standalone.log.info("Activate Password Administator and try updates again...")

    # Bind as root DN
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Root DN failed to authenticate: ' + e.message['desc'])
        assert False

    # Update config
    try:
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ADMIN_DN)])
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Failed to add password admin to config: ' + e.message['desc'])
        assert False

    # Bind as Password Admin
    try:
        topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('Failed to bind as the Password Admin: ' + e.message['desc'])
        assert False

    #
    # Make the same password updates, but this time they should succeed
    #
    for passwd in INVALID_PWDS:
        entry.setValues('userpassword', passwd)
        try:
            topology_st.standalone.modify_s(ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', passwd)])
        except ldap.LDAPError as e:
            topology_st.standalone.log.error('Password update failed unexpectedly: password (%s) result (%s)'
                                             % (passwd, e.message['desc']))
            assert False
        topology_st.standalone.log.info('Password update succeeded (%s)' % passwd)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
