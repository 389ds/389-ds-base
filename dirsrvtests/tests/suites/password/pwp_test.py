"""
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
"""

import os
import pytest
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts, UserAccount
from lib389._constants import DEFAULT_SUFFIX
from lib389.config import Config
from lib389.idm.group import Group
from lib389.utils import ds_is_older
import ldap
import time

pytestmark = pytest.mark.tier1

if ds_is_older('1.4'):
    DEFAULT_PASSWORD_STORAGE_SCHEME = 'SSHA512'
else:
    DEFAULT_PASSWORD_STORAGE_SCHEME = 'PBKDF2_SHA256'

def _create_user(topo, uid, cn, uidNumber, userpassword):
    """
    Will Create user
    """
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX).create(properties={
        'uid': uid,
        'sn': cn.split(' ')[-1],
        'cn': cn,
        'givenname': cn.split(' ')[0],
        'uidNumber': uidNumber,
        'gidNumber': uidNumber,
        'mail': f'{uid}@example.com',
        'userpassword': userpassword,
        'homeDirectory': f'/home/{uid}'
    })
    return user


def _change_password_with_own(topo, user_dn, password, new_password):
    """
    Change user password with user self
    """
    conn = UserAccount(topo.standalone, user_dn).bind(password)
    real_user = UserAccount(conn, user_dn)
    real_user.replace('userpassword', new_password)


def _change_password_with_root(topo, user_dn, new_password):
    """
    Root will change user password
    """
    UserAccount(topo.standalone, user_dn).replace('userpassword', new_password)


@pytest.fixture(scope="function")
def _fix_password(topo, request):
    user = _create_user(topo, 'dbyers', 'Danny Byers', '1001', 'dbyers1')
    user.replace('userpassword', 'dbyers1')

    def fin():
        user.delete()
    request.addfinalizer(fin)


def test_passwordchange_to_no(topo, _fix_password):
    """Change password fo a user even password even though pw policy is set to no

    :id: 16c64ef0-5a20-11ea-a902-8c16451d917b
    :setup: Standalone
    :steps:
        1. Adding  an user with uid=dbyers
        2. Set Password change to Must Not Change After Reset
        3. Setting  Password policy to May Not Change Password
        4. Try to change password fo a user even password even though pw policy is set to no
        5. Set Password change to May Change Password
        6. Try to change password fo a user even password
        7. Try to change password with invalid credentials.  Should see error message.
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """
    # Adding  an user with uid=dbyers
    user = f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}'
    config = Config(topo.standalone)
    # Set Password change to Must Not Change After Reset
    config.replace_many(
        ('passwordmustchange', 'off'),
        ('passwordchange', 'off'))
    # Try to change password fo a user even password even though pw policy is set to no
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        _change_password_with_own(topo, user, 'dbyers1', 'AB')
    # Set Password change to May Change Password
    config.replace('passwordchange', 'on')
    _change_password_with_own(topo, user, 'dbyers1', 'dbyers1')
    # Try to change password with invalid credentials.  Should see error message.
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'AB', 'dbyers1')


def test_password_check_syntax(topo, _fix_password):
    """Password check syntax

    :id: 1e6fcc9e-5a20-11ea-9659-8c16451d917b
    :setup: Standalone
    :steps:
        1. Sets Password check syntax to on
        2. Try to change to a password that violates length.  Should get error
        3. Attempt to Modify password to db which is in error to policy
        4. change min pw length to 5
        5. Attempt to Modify password to dby3rs which is in error to policy
        6. Attempt to Modify password to danny which is in error to policy
        7. Attempt to Modify password to byers which is in error to policy
        8. Change min pw length to 6
        9. Try to change the password
        10. Trying to set to a password containing value of sn
        11. Sets policy to not check pw syntax
        12. Test that when checking syntax is off, you can use small passwords
        13. Test that when checking syntax is off, trivial passwords can be used
        14. Changing password minimum length from 6 to 10
        15. Setting policy to Check Password Syntax again
        16. Try to change to a password that violates length
        17. Reset Password
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Fail
        17. Success
    """
    config = Config(topo.standalone)
    # Sets Password check syntax to on
    config.replace('passwordchecksyntax', 'on')
    # Try to change to a password that violates length.  Should get error
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1', 'dbyers2')
    # Attempt to Modify password to db which is in error to policy
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1', 'db')
    # change min pw length to 5
    config.replace('passwordminlength', '5')
    # Attempt to Modify password to dby3rs which is in error to policy
    # Attempt to Modify password to danny which is in error to policy
    # Attempt to Modify password to byers which is in error to policy
    for password in ['dbyers', 'Danny', 'byers']:
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1', password)
    # Change min pw length to 6
    config.replace('passwordminlength', '6')
    # Try to change the password
    # Trying to set to a password containing value of sn
    for password in ['dby3rs1', 'dbyers2', '67Danny89', 'YAByers8']:
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1', password)
    # Sets policy to not check pw syntax
    # Test that when checking syntax is off, you can use small passwords
    # Test that when checking syntax is off, trivial passwords can be used
    config.replace('passwordchecksyntax', 'off')
    for password, new_pass in [('dbyers1', 'db'), ('db', 'dbyers'), ('dbyers', 'dbyers1')]:
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', password, new_pass)
    # Changing password minimum length from 6 to 10
    # Setting policy to Check Password Syntax again
    config.replace_many(
        ('passwordminlength', '10'),
        ('passwordchecksyntax', 'on'))
    # Try to change to a password that violates length
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1', 'db')
    UserAccount(topo.standalone, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}').replace('userpassword', 'dbyers1')


def test_too_big_password(topo, _fix_password):
    """Test for long long password

    :id: 299a3fb4-5a20-11ea-bba8-8c16451d917b
    :setup: Standalone
    :steps:
        1. Setting policy to keep password histories
        2. Changing number of password in history to 3
        3. Modify password from dby3rs1 to dby3rs2
        4. Checking that the passwordhistory attribute has been added
        5. Add a password test for long long password
        6. Changing number of password in history to 6 and passwordhistory off
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    config = Config(topo.standalone)
    # Setting policy to keep password histories
    config.replace_many(
        ('passwordchecksyntax', 'off'),
        ('passwordhistory', 'on'))
    assert config.get_attr_val_utf8('passwordinhistory') == '6'
    # Changing number of password in history to 3
    config.replace('passwordinhistory', '3')
    # Modify password from dby3rs1 to dby3rs2
    _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1', 'dbyers2')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers2', 'dbyers1')
    # Checking that the passwordhistory attribute has been added
    assert UserAccount(topo.standalone, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}').get_attr_val_utf8('passwordhistory')
    # Add a password test for long long password
    long_pass = 50*'0123456789'+'LENGTH=510'
    _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers2', long_pass)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', long_pass, long_pass)
    _change_password_with_root(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1')
    # Changing number of password in history to 6 and passwordhistory off
    config.replace_many(('passwordhistory', 'off'),
                        ('passwordinhistory', '6'))


def test_pwminage(topo, _fix_password):
    """Test pwminage

    :id: 2df7bf32-5a20-11ea-ad23-8c16451d917b
    :setup: Standalone
    :steps:
        1. Get pwminage; should be 0 currently
        2. Sets policy to pwminage 3
        3. Change current password
        4. Try to change password again
        5. Try now after 3 secs is up,  should work.
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Fail
        5. Success
    """
    config = Config(topo.standalone)
    # Get pwminage; should be 0 currently
    assert config.get_attr_val_utf8('passwordminage') == '0'
    # Sets policy to pwminage 3
    config.replace('passwordminage', '3')
    # Change current password
    _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1', 'dbyers2')
    # Try to change password again
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers2', 'dbyers1')
    for _ in range(3):
        time.sleep(1)
    # Try now after 3 secs is up,  should work.
    _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers2', 'dbyers1')
    config.replace('passwordminage', '0')


def test_invalid_credentials(topo, _fix_password):
    """Test bind again with valid password: We should be locked

    :id: 3233ca78-5a20-11ea-8d35-8c16451d917b
    :setup: Standalone
    :steps:
        1. Search if passwordlockout is off
        2. Turns on passwordlockout
        3. sets lockout duration to 3 seconds
        4. Changing pw failure count reset duration to 3 sec and passwordminlength to 10
        5. Try to bind with invalid credentials
        6. Change password to password lockout forever
        7. Try to bind with invalid credentials
        8. Now bind again with valid password: We should be locked
        9. Delete dby3rs before exiting
        10. Reset server
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Fail
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
    """
    config = Config(topo.standalone)
    # Search if passwordlockout is off
    assert config.get_attr_val_utf8('passwordlockout') == 'off'
    # Turns on passwordlockout
    # sets lockout duration to 3 seconds
    # Changing pw failure count reset duration to 3 sec and passwordminlength to 10
    config.replace_many(
        ('passwordlockout', 'on'),
        ('passwordlockoutduration', '3'),
        ('passwordresetfailurecount', '3'),
        ('passwordminlength', '10'))
    # Try to bind with invalid credentials
    for _ in range(3):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'Invalid', 'dbyers1')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'Invalid', 'dbyers1')
    for _ in range(3):
        time.sleep(1)
    _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1', 'dbyers1')
    # Change password to password lockout forever
    config.replace('passwordunlock', 'off')
    # Try to bind with invalid credentials
    for _ in range(3):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'Invalid', 'dbyers1')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'Invalid', 'dbyers1')
    for _ in range(3):
        time.sleep(1)
    # Now bind again with valid password: We should be locked
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1', 'dbyers1')
    # Delete dby3rs before exiting
    _change_password_with_root(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1')
    time.sleep(1)
    _change_password_with_own(topo, f'uid=dbyers,ou=People,{DEFAULT_SUFFIX}', 'dbyers1', 'dbyers1')
    # Reset server
    config.replace_many(
        ('passwordinhistory', '6'),
        ('passwordlockout', 'off'),
        ('passwordlockoutduration', '3600'),
        ('passwordminlength', '6'),
        ('passwordresetfailurecount', '600'),
        ('passwordunlock', 'on'))


def test_expiration_date(topo, _fix_password):
    """Test check the expiration date is still in the future

    :id: 3691739a-5a20-11ea-8712-8c16451d917b
    :setup: Standalone
    :steps:
        1. Password expiration
        2. Add a user with a password expiration date
        3. Modify their password
        4. Check the expiration date is still in the future
        5. Modify the password expiration date
        6. Check the expiration date is still in the future
        7. Change policy so that user can change passwords
        8. Deleting user
        9. Adding user
        10. Set password history ON
        11. Modify password Once
        12. Try to change the password with same one
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Fail
    """
    # Add a user with a password expiration date
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX).create_test_user()
    user.replace_many(
        ('userpassword', 'bind4now'),
        ('passwordExpirationTime', '20380119031404Z'))
    # Modify their password
    user.replace('userPassword', 'secreter')
    # Check the expiration date is still in the future
    assert user.get_attr_val_utf8('passwordExpirationTime') == '20380119031404Z'
    # Modify the password expiration date
    user.replace('passwordExpirationTime', '20380119031405Z')
    # Check the expiration date is still in the future
    assert user.get_attr_val_utf8('passwordExpirationTime') == '20380119031405Z'
    config = Config(topo.standalone)
    # Change policy so that user can change passwords
    config.replace('passwordchange', 'on')
    # Deleting user
    UserAccount(topo.standalone, f'uid=test_user_1000,ou=People,{DEFAULT_SUFFIX}').delete()
    # Adding user
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX).create_test_user()
    # Set password history ON
    config.replace('passwordhistory', 'on')
    # Modify password Once
    user.replace('userPassword', 'secreter')
    time.sleep(1)
    assert DEFAULT_PASSWORD_STORAGE_SCHEME in user.get_attr_val_utf8('userPassword')
    # Try to change the password with same one
    for _ in range(3):
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            _change_password_with_own(topo, user.dn, 'secreter', 'secreter')
    user.delete()


def test_passwordlockout(topo, _fix_password):
    """Test adding admin user diradmin to Directory Administrator group

    :id: 3ffcffda-5a20-11ea-a3af-8c16451d917b
    :setup: Standalone
    :steps:
        1. Account Lockout must be cleared on successful password change
        2. Adding admin user diradmin
        3. Adding admin user diradmin to Directory Administrator group
        4. Turn on passwordlockout
        5. Sets lockout duration to 30 seconds
        6. Sets failure count reset duration to 30 sec
        7. Sets max password bind failure count to 3
        8. Reset password retry count (to 0)
        9. Try to bind with invalid credentials(3 times)
        10. Try to bind with valid pw, should give lockout error
        11. Reset password using admin login
        12. Try to login as the user to check the unlocking of account. Will also change
            the password back to original
        13. Change to account lockout forever until reset
        14. Reset password retry count (to 0)
        15. Try to bind with invalid credentials(3 times)
        16. Try to bind with valid pw, should give lockout error
        17. Reset password using admin login
        18. Try to login as the user to check the unlocking of account. Will also change the
            password back to original
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Fail
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Fail
        16. Success
        17. Success
        18. Success
    """
    config = Config(topo.standalone)
    # Adding admin user diradmin
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX).create_test_user()
    user.replace('userpassword', 'dby3rs2')
    admin = _create_user(topo, 'diradmin', 'Anuj Borah', '1002', 'diradmin')
    # Adding admin user diradmin to Directory Administrator group
    Group(topo.standalone, f'cn=user_passwd_reset,ou=permissions,{DEFAULT_SUFFIX}').add('member', admin.dn)
    # Turn on passwordlockout
    # Sets lockout duration to 30 seconds
    # Sets failure count reset duration to 30 sec
    # Sets max password bind failure count to 3
    # Reset password retry count (to 0)
    config.replace_many(
        ('passwordlockout', 'on'),
        ('passwordlockoutduration', '30'),
        ('passwordresetfailurecount', '30'),
        ('passwordmaxfailure', '3'),
        ('passwordhistory', 'off'))
    user.replace('passwordretrycount', '0')
    # Try to bind with invalid credentials(3 times)
    for _ in range(3):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _change_password_with_own(topo, user.dn, 'Invalid', 'secreter')
    # Try to bind with valid pw, should give lockout error
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, user.dn, 'Invalid', 'secreter')
    # Reset password using admin login
    conn = admin.bind('diradmin')
    UserAccount(conn, user.dn).replace('userpassword', 'dby3rs2')
    time.sleep(1)
    # Try to login as the user to check the unlocking of account. Will also change
    # the password back to original
    _change_password_with_own(topo, user.dn, 'dby3rs2', 'secreter')
    # Change to account lockout forever until reset
    # Reset password retry count (to 0)
    config.replace('passwordunlock', 'off')
    user.replace('passwordretrycount', '0')
    # Try to bind with invalid credentials(3 times)
    for _ in range(3):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            _change_password_with_own(topo, user.dn, 'Invalid', 'secreter')
    # Try to bind with valid pw, should give lockout error
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        _change_password_with_own(topo, user.dn, 'Invalid', 'secreter')
    # Reset password using admin login
    UserAccount(conn, user.dn).replace('userpassword', 'dby3rs2')
    time.sleep(1)
    # Try to login as the user to check the unlocking of account. Will also change the
    # password back to original
    _change_password_with_own(topo, user.dn, 'dby3rs2', 'secreter')


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
