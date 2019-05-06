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
from lib389.utils import *

pytestmark = pytest.mark.tier2

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
        topology_st.standalone.log.error('Unexpected result ' + e.args[0]['desc'])
        assert False
        topology_st.standalone.log.error("Failed to add Password Administator %s, error: %s "
                                         % (ADMIN_DN, e.args[0]['desc']))
        assert False

    topology_st.standalone.log.info("Configuring password policy...")
    topology_st.standalone.config.replace_many(('nsslapd-pwpolicy-local', 'on'),
                                               ('passwordCheckSyntax', 'on'),
                                               ('passwordMinCategories', '1'),
                                               ('passwordMinTokenLength', '1'),
                                               ('passwordExp', 'on'),
                                               ('passwordMinDigits', '1'),
                                               ('passwordMinSpecials', '1'))

    #
    # Add an aci to allow everyone all access (just makes things easier)
    #
    topology_st.standalone.log.info("Add aci to allow password admin to add/update entries...")

    ACI_TARGET = "(target = \"ldap:///%s\")" % SUFFIX
    ACI_TARGETATTR = "(targetattr = *)"
    ACI_ALLOW = "(version 3.0; acl \"Password Admin Access\"; allow (all) "
    ACI_SUBJECT = "(userdn = \"ldap:///anyone\");)"
    ACI_BODY = ACI_TARGET + ACI_TARGETATTR + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ensure_bytes(ACI_BODY))]
    topology_st.standalone.modify_s(SUFFIX, mod)

    #
    # Bind as the Password Admin
    #
    topology_st.standalone.log.info("Bind as the Password Administator (before activating)...")
    topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)

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
                                            % (passwd, e.args[0]['desc']))

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
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # Update config
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ensure_bytes(ADMIN_DN))])

    # Bind as Password Admin
    topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)

    #
    # Start adding entries with invalid passwords, delete the entry after each pass.
    #
    for passwd in INVALID_PWDS:
        entry.setValues('userpassword', passwd)
        topology_st.standalone.log.info("Create a regular user entry %s with password (%s)..." % (ENTRY_DN, passwd))
        topology_st.standalone.add_s(entry)

        topology_st.standalone.log.info('Succesfully added entry (%s)' % ENTRY_DN)

        # Delete entry for the next pass
        topology_st.standalone.delete_s(ENTRY_DN)
    #
    # Add the entry for the next round of testing (modify password)
    #
    entry.setValues('userpassword', ADMIN_PWD)
    topology_st.standalone.add_s(entry)

    #
    # Deactivate the password admin and make sure invalid password updates fail
    #
    topology_st.standalone.log.info("Deactivate Password Administator and try invalid password updates...")

    # Bind as root DN
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # Update conf
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_DELETE, 'passwordAdminDN', None)])

    # Bind as Password Admin
    topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)

    #
    # Make invalid password updates that should fail
    #
    for passwd in INVALID_PWDS:
        failed_as_expected = False
        entry.setValues('userpassword', passwd)
        try:
            topology_st.standalone.modify_s(ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', ensure_bytes(passwd))])
        except ldap.LDAPError as e:
            # We failed as expected
            failed_as_expected = True
            topology_st.standalone.log.info('Password update failed as expected: password (%s) result (%s)'
                                            % (passwd, e.args[0]['desc']))

        if not failed_as_expected:
            topology_st.standalone.log.error("We were incorrectly able to add an invalid password (%s)"
                                             % (passwd))
            assert False

    #
    # Now activate a password administator
    #
    topology_st.standalone.log.info("Activate Password Administator and try updates again...")

    # Bind as root D
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # Update config
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordAdminDN', ensure_bytes(ADMIN_DN))])

    # Bind as Password Admin
    topology_st.standalone.simple_bind_s(ADMIN_DN, ADMIN_PWD)

    #
    # Make the same password updates, but this time they should succeed
    #
    for passwd in INVALID_PWDS:
        entry.setValues('userpassword', passwd)
        topology_st.standalone.modify_s(ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', ensure_bytes(passwd))])
        topology_st.standalone.log.info('Password update succeeded (%s)' % passwd)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
