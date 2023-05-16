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

from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.idm.domain import Domain

from lib389._constants import SUFFIX, DN_DM, PASSWORD, DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
ADMIN_NAME = 'passwd_admin'
ADMIN_DN = 'cn=%s,%s' % (ADMIN_NAME, SUFFIX)
ADMIN2_NAME = 'passwd_admin2'
ADMIN2_DN = 'cn=%s,%s' % (ADMIN2_NAME, SUFFIX)
ADMIN_PWD = 'ntaheonusheoasuhoau_9'
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

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)

    # Add Password Admin 1
    admin1_user = users.create(properties={
        'uid': 'admin1',
        'cn' : 'admin1',
        'sn' : 'strator',
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/admin1',
        'userPassword': ADMIN_PWD
    })

    # Add Password Admin 2
    admin2_user = users.create(properties={
        'uid': 'admin2',
        'cn' : 'admin2',
        'sn' : 'strator',
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/admin2',
        'userPassword': ADMIN_PWD
    })

    # Add Password Admin Group
    admin_group = groups.create(properties={
        'cn': 'password admin group'
    })

    admin_group.add_member(admin1_user.dn)
    admin_group.add_member(admin2_user.dn)

    # Configure password policy
    log.info('test_pwdAdmin_init: Configuring password policy...')

    topology_st.standalone.config.replace_many(
                                            ('nsslapd-pwpolicy-local', 'on'),
                                            ('passwordCheckSyntax', 'on'),
                                            ('passwordMinCategories', '1'),
                                            ('passwordMinTokenLength', '2'),
                                            ('passwordExp', 'on'),
                                            ('passwordMinDigits', '1'),
                                            ('passwordMinSpecials', '1'),
                                            ('passwordHistory', 'on'),
                                            ('passwordStorageScheme', 'clear'),
                                            ('nsslapd-enable-upgrade-hash', 'off')
                                        )

    #
    # Add an aci to allow everyone all access (just makes things easier)
    #
    log.info('Add aci to allow password admin to add/update entries...')

    domain = Domain(topology_st.standalone, DEFAULT_SUFFIX)

    ACI_TARGET = "(target = \"ldap:///%s\")" % SUFFIX
    ACI_TARGETATTR = "(targetattr = *)"
    ACI_ALLOW = "(version 3.0; acl \"Password Admin Access\"; allow (all) "
    ACI_SUBJECT = "(userdn = \"ldap:///anyone\");)"
    ACI_BODY = ACI_TARGET + ACI_TARGETATTR + ACI_ALLOW + ACI_SUBJECT

    domain.add('aci', ACI_BODY)

    #
    # Bind as the future Password Admin
    #
    log.info('test_pwdAdmin_init: Bind as the Password Administrator (before activating)...')
    admin_conn = admin1_user.bind(ADMIN_PWD)

    #
    # Setup our test entry, and test password policy is working
    #

    # Connect up an admin authed users connection.
    admin_users = UserAccounts(admin_conn, DEFAULT_SUFFIX)

    #
    # Start by attempting to add an entry with an invalid password
    #
    log.info('test_pwdAdmin_init: Attempt to add entries with invalid passwords, these adds should fail...')
    for passwd in INVALID_PWDS:
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            admin_users.create(properties={
                'uid': 'example',
                'cn' : 'example',
                'sn' : 'example',
                'uidNumber' : '1000',
                'gidNumber' : '2000',
                'homeDirectory' : '/home/example',
                'userPassword': passwd
            })

    return (admin_group, admin1_user, admin2_user)


def test_pwdAdmin_bypass(topology_st, password_policy):
    """Test that password administrators/root DN can
    bypass password syntax/policy

    :id: 743bfe33-a1f7-482b-8807-efeb7aa57348
    :setup: Standalone instance, Password Admin entry,
        Password policy configured as below:
        nsslapd-pwpolicy-local: on
        passwordCheckSyntax: on
        passwordMinCategories: 1
        passwordMinTokenLength: 2
        passwordExp: on
        passwordMinDigits: 1
        passwordMinSpecials: 1
    :steps:
        1: Add users with invalid passwords
    :expectedresults:
        1: Users should be added successfully.
    """

    #
    # Now activate a password administator, bind as root dn to do the config
    # update, then rebind as the password admin
    #
    log.info('test_pwdAdmin: Activate the Password Administator...')

    # Extract our fixture data.

    (admin_group, admin1_user, admin2_user) = password_policy

    # Set the password admin

    topology_st.standalone.config.set('passwordAdminDN', admin1_user.dn)

    #
    # Get our test entry
    #

    admin_conn = admin1_user.bind(ADMIN_PWD)
    admin_users = UserAccounts(admin_conn, DEFAULT_SUFFIX)

    #
    # Start adding entries with invalid passwords, delete the entry after each pass.
    #
    for passwd in INVALID_PWDS:
        u1 = admin_users.create(properties={
            'uid': 'example',
            'cn' : 'example',
            'sn' : 'example',
            'uidNumber' : '1000',
            'gidNumber' : '2000',
            'homeDirectory' : '/home/example',
            'userPassword': passwd
        })
        u1.delete()


def test_pwdAdmin_no_admin(topology_st, password_policy):
    """Test that password administrators/root DN can
    bypass password syntax/policy

    :id: 74347798-7cc7-4ce7-ad5c-06387ffde02c
    :setup: Standalone instance, Password Admin entry,
        Password policy configured as below:
        nsslapd-pwpolicy-local: on
        passwordCheckSyntax: on
        passwordMinCategories: 1
        passwordMinTokenLength: 2
        passwordExp: on
        passwordMinDigits: 1
        passwordMinSpecials: 1
    :steps:
        1: Create a user
        2: Attempt to set passwords on the user that are invalid
    :expectedresults:
        1: Success
        2: The passwords should NOT be set
    """
    (admin_group, admin1_user, admin2_user) = password_policy

    # Remove password admin

    # Can't use pytest.raises. because this may or may not exist
    try:
        topology_st.standalone.config.remove_all('passwordAdminDN')
    except ldap.NO_SUCH_ATTRIBUTE:
        pass

    #
    # Add the entry for the next round of testing (modify password)
    #
    admin_conn = admin1_user.bind(ADMIN_PWD)
    admin_users = UserAccounts(admin_conn, DEFAULT_SUFFIX)

    u2 = admin_users.create(properties={
        'uid': 'example',
        'cn' : 'example',
        'sn' : 'example',
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/example',
        'userPassword': ADMIN_PWD
    })

    #
    # Make invalid password updates that should fail
    #
    for passwd in INVALID_PWDS:
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            u2.replace('userPassword', passwd)


def test_pwdAdmin_modify(topology_st, password_policy):
    """Test that password administrators/root DN can modify
    passwords rather than adding them.

    :id: 85326527-8eeb-401f-9d1b-4ef55dee45a4
    :setup: Standalone instance, Password Admin entry,
        Password policy configured as below:
        nsslapd-pwpolicy-local: on
        passwordCheckSyntax: on
        passwordMinCategories: 1
        passwordMinTokenLength: 2
        passwordExp: on
        passwordMinDigits: 1
        passwordMinSpecials: 1
    :steps:
        1: Retrieve the user
        2: Replace the password with invalid content
    :expectedresults:
        1: Success
        2: The password should be set
    """
    (admin_group, admin1_user, admin2_user) = password_policy

    # Update config - set the password admin
    topology_st.standalone.config.set('passwordAdminDN', admin1_user.dn)

    admin_conn = admin1_user.bind(ADMIN_PWD)
    admin_users = UserAccounts(admin_conn, DEFAULT_SUFFIX)

    u3 = admin_users.get('example')
    #
    # Make the same password updates, but this time they should succeed
    #
    for passwd in INVALID_PWDS:
        u3.replace('userPassword', passwd)


def test_pwdAdmin_group(topology_st, password_policy):
    """Test that password admin group can bypass policy.

    :id: 4d62ae34-0f25-486e-b823-afd2b431e9b0
    :setup: Standalone instance, Password Admin entry,
        Password policy configured as below:
        nsslapd-pwpolicy-local: on
        passwordCheckSyntax: on
        passwordMinCategories: 1
        passwordMinTokenLength: 2
        passwordExp: on
        passwordMinDigits: 1
        passwordMinSpecials: 1
    :steps:
        1: Add group to passwordadmin dn
        2: Attempt to set invalid passwords.
    :expectedresults:
        1: Success.
        2: Password should be set.
    """
    (admin_group, admin1_user, admin2_user) = password_policy

    # Update config - set the password admin group
    topology_st.standalone.config.set('passwordAdminDN', admin_group.dn)

    # Bind as admin2, who is in the group.

    admin2_conn = admin2_user.bind(ADMIN_PWD)
    admin2_users = UserAccounts(admin2_conn, DEFAULT_SUFFIX)

    u4 = admin2_users.get('example')

    # Make some invalid password updates, but they should succeed
    for passwd in INVALID_PWDS:
        u4.replace('userPassword', passwd)


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

    (admin_group, admin1_user, admin2_user) = password_policy
    # Add multiple attributes - one already exists so just try and add the second one
    topology_st.standalone.config.set('passwordAdminDN', admin_group.dn)
    with pytest.raises(ldap.OBJECT_CLASS_VIOLATION):
        topology_st.standalone.config.add('passwordAdminDN', admin1_user.dn)

    # Attempt to set invalid DN
    with pytest.raises(ldap.INVALID_SYNTAX):
        topology_st.standalone.config.set('passwordAdminDN', 'zzzzzzzzzzzz')


def test_pwd_admin_config_test_skip_updates(topology_st, password_policy):
    """Check passwordAdminDN does not update entry password state attributes

    :id: 964f1430-795b-4f4d-85b2-abaffe66ddcb

    :setup: Standalone instance
    :steps:
        1. Add test entry
        2. Update password
        3. Password history updated
        4. Enable "skip info update"
        5. Update password again
        6. New password not in history
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    inst = topology_st.standalone
    passwd_in_history = "Secret123"
    password_not_in_history = "ShouldNotBeInHistory"
    (admin_group, admin1_user, admin2_user) = password_policy

    # Update config
    inst.config.set('passwordAdminDN', admin_group.dn)

    # Add test entry
    admin_conn = admin1_user.bind(ADMIN_PWD)
    admin_users = UserAccounts(admin_conn, DEFAULT_SUFFIX)
    admin_users.create(properties={
        'uid': 'skipInfoUpdate',
        'cn': 'skipInfoUpdate',
        'sn': 'skipInfoUpdate',
        'uidNumber': '1001',
        'gidNumber': '2002',
        'homeDirectory': '/home/skipInfoUpdate',
        'userPassword': "abdcefghijk"
    })

    # Update password to populate history
    user = admin_users.get('skipInfoUpdate')
    user.replace('userPassword', passwd_in_history)
    user.replace('userPassword', passwd_in_history)
    time.sleep(1)

    # Check password history was updated
    passwords = user.get_attr_vals_utf8('passwordHistory')
    log.debug(f"passwords in history for {user.dn}: {str(passwords)}")
    found = False
    for passwd in passwords:
        if passwd_in_history in passwd:
            found = True
    assert found

    # Disable password state info updates
    inst.config.set('passwordAdminSkipInfoUpdate', 'on')
    time.sleep(1)

    # Update password
    user.replace('userPassword', password_not_in_history)
    user.replace('userPassword', password_not_in_history)
    time.sleep(1)

    # Check it is not in password history
    passwords = user.get_attr_vals_utf8('passwordHistory')
    log.debug(f"Part 2: passwords in history for {user.dn}: {str(passwords)}")
    found = False
    for passwd in passwords:
        if password_not_in_history in passwd:
            found = True
    assert not found


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
