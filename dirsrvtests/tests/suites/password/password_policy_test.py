# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
This test script will test password policy.
"""

import os
import pytest
import time
from lib389.topologies import topology_st as topo
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts, UserAccount
from lib389._constants import DEFAULT_SUFFIX
from lib389.pwpolicy import PwPolicyManager
import ldap


pytestmark = pytest.mark.tier1


def create_user(topo, uid, cn, sn, givenname, userpasseord, gid, ou):
    """
    Will create user
    """
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=ou).create(properties={
        'uid': uid,
        'cn': cn,
        'sn': sn,
        'givenname': givenname,
        'mail': f'{uid}@example.com',
        'userpassword': userpasseord,
        'homeDirectory': f'/home/{uid}',
        'uidNumber': gid,
        'gidNumber': gid
    })
    return user


@pytest.fixture(scope="module")
def _policy_setup(topo):
    """
    Will do pretest setup.
    """
    for suffix, ou in [(DEFAULT_SUFFIX, 'dirsec'), (f'ou=people,{DEFAULT_SUFFIX}', 'others')]:
        OrganizationalUnits(topo.standalone, suffix).create(properties={
            'ou': ou
        })
    for uid, cn, sn, givenname, userpasseord, gid, ou in [
        ('dbyers', 'Danny Byers', 'Byers', 'Danny', 'dby3rs1', '10001', 'ou=dirsec'),
        ('orla', 'Orla Hegarty', 'Hegarty', 'Orla', '000rla1', '10002', 'ou=dirsec'),
        ('joe', 'Joe Rath', 'Rath', 'Joe', '00j0e1', '10003', 'ou=people'),
        ('jack', 'Jack Rath', 'Rath', 'Jack', '00j6ck1', '10004', 'ou=people'),
        ('fred', 'Fred Byers', 'Byers', 'Fred', '00fr3d1', '10005', None),
        ('deep', 'Deep Blue', 'Blue', 'Deep', '00de3p1', '10006', 'ou=others, ou=people'),
        ('accntlusr', 'AccountControl User', 'ControlUser', 'Account', 'AcControl123', '10007', 'ou=dirsec'),
        ('nocntlusr', 'NoAccountControl User', 'ControlUser', 'NoAccount', 'NoControl123', '10008', 'ou=dirsec')
    ]:
        create_user(topo, uid, cn, sn, givenname, userpasseord, gid, ou)
    policy_props = {'passwordexp': 'off',
                    'passwordchange': 'off',
                    'passwordmustchange': 'off',
                    'passwordchecksyntax': 'off',
                    'passwordinhistory': '6',
                    'passwordhistory': 'off',
                    'passwordlockout': 'off',
                    'passwordlockoutduration': '3600',
                    'passwordmaxage': '8640000',
                    'passwordmaxfailure': '3',
                    'passwordminage': '0',
                    'passwordminlength': '6',
                    'passwordresetfailurecount': '600',
                    'passwordunlock': 'on',
                    'passwordStorageScheme': 'CLEAR',
                    'passwordwarning': '86400'
                    }
    pwp = PwPolicyManager(topo.standalone)
    for dn_dn in (f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}',
                  f'uid=joe,ou=People,{DEFAULT_SUFFIX}'):
        pwp.create_user_policy(dn_dn, policy_props)
    pwp.create_subtree_policy(f'ou=People,{DEFAULT_SUFFIX}', policy_props)


def change_password(topo, user_password_new_pass_list):
    """
    Will change password with self binding.
    """
    for user, password, new_pass in user_password_new_pass_list:
        real_user = UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}')
        conn = real_user.bind(password)
        UserAccount(conn, real_user.dn).replace('userpassword', new_pass)


def change_password_ultra_new(topo, user_password_new_pass_list):
    """
    Will change password with self binding.
    """
    for user, password, new_pass, ultra_new_pass in user_password_new_pass_list:
        real_user = UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}')
        conn = real_user.bind(password)
        UserAccount(conn, real_user.dn).replace('userpassword', new_pass)
        conn = real_user.bind(new_pass)
        UserAccount(conn, real_user.dn).replace('userpassword', ultra_new_pass)


def change_password_with_admin(topo, user_password_new_pass_list):
    """
    Will change password by root.
    """
    for user, password in user_password_new_pass_list:
        UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}').replace('userpassword', password)


def _do_transaction_for_pwp(topo, attr1, attr2):
    """
    Will change pwp parameters
    """
    pwp = PwPolicyManager(topo.standalone)
    orl = pwp.get_pwpolicy_entry(f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    joe = pwp.get_pwpolicy_entry(f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    people = pwp.get_pwpolicy_entry(f'ou=people,{DEFAULT_SUFFIX}')
    for instance in [orl, joe, people]:
        instance.replace(attr1, attr2)
    for instance in [orl, joe, people]:
        assert instance.get_attr_val_utf8(attr1) == attr2


@pytest.fixture(scope="function")
def _fixture_for_password_change(request, topo):
    pwp = PwPolicyManager(topo.standalone)
    orl = pwp.get_pwpolicy_entry(f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    for attribute in ('passwordMustChange', 'passwordmustchange'):
        orl.replace(attribute, 'off')
        assert orl.get_attr_val_utf8(attribute) == 'off'

    def final_task():
        people = pwp.get_pwpolicy_entry(f'ou=people,{DEFAULT_SUFFIX}')
        people.replace('passwordchange', 'on')
        assert people.get_attr_val_utf8('passwordchange') == 'on'
        # Administrator Reseting to original password
        change_password_with_admin(topo, [
            ('uid=joe,ou=people', '00j0e1'),
            ('uid=fred', '00fr3d1'),
            ('uid=jack,ou=people', '00j6ck1'),
            ('uid=deep,ou=others,ou=people', '00de3p1'),
            ('uid=orla,ou=dirsec', '000rla1'),
            ('uid=dbyers,ou=dirsec', 'Anuj')
        ])
        request.addfinalizer(final_task)


def test_password_change_section(topo, _policy_setup, _fixture_for_password_change):
    """Password Change Section.

    :id: 5d018c08-9388-11ea-8394-8c16451d917b
    :setup: Standalone
    :steps:
        1. Confirm that user is not been affected by fine grained password
        (As its is not belong to any password policy)
        2. Should be able to change password(As its is not belong to any password policy)
        3. Try to change password for user even though pw policy is set to no.
        Should get error message: unwilling to Perform !
        4. Set Password change to May Change Password.
        5. Administrator Reseting to original password !
        6. Attempt to Modify password to orla2 with an invalid first pw with error message.
        7. Changing current password from orla1 to orla2
        8. Changing current password from orla2 to orla1.
        9. Set Password change to Must Not Change After Reset
        10 Change password for joe,jack,deep even though pw policy is set to no with error message.
        11. Fred can change.(Fred is not belong to any pw policy)
        12. Changing pw policy to may change pw
        13. Set Password change to May Change Password
        14. Administrator Reseting to original password
        15. Try to change password with invalid credentials.  Should see error message.
        16. Changing current password for joe and fed.
        17. Changing current password for jack and deep with error message.(passwordchange not on)
        18. Changing pw policy to may change pw
        19. Set Password change to May Change Password
        20. Administrator Reseting to original password
        21. Try to change password with invalid credentials.  Should see error message.
        22. Changing current password
        23. Set Password change to Must Not Change After Reset
        24. Searching for passwordchange: Off
        25. Administrator Reseting to original password
        26. Try to change password with invalid credentials.  Should see error message
        27. Changing current password (('passwordchange', 'off') for joe)
    :expected results:
        1. Success(As its is not belong to any password policy)
        2. Success
        3. Fail(pw policy is set to no)
        4. Success
        5. Success
        6. Fail(invalid first pw)
        7. Success
        8. Success
        9. Success
        10. Fail(pw policy is set to no)
        11. Success((Fred is not belong to any pw policy))
        12. Success
        13. Success
        14. Success
        15. Fail(invalid credentials)
        16. Success((passwordchange  on))
        17. Fail(passwordchange not on)
        18. Success
        19. Success
        20. Success
        21. Fail(invalid credentials)
        22. Success
        23. Success
        24. Success
        25. Success
        26. Fail(invalid credentials)
        27. Success
    """
    # Confirm that uid=dbyers is not been affected by fine grained password
    dbyers = UserAccount(topo.standalone, f'uid=dbyers,ou=dirsec,{DEFAULT_SUFFIX}')
    conn = dbyers.bind('dby3rs1')
    dbyers_conn = UserAccount(conn, f'uid=dbyers,ou=dirsec,{DEFAULT_SUFFIX}')
    # Should be able to change password(As its is not belong to any password policy)
    dbyers_conn.replace('userpassword', "Anuj")
    # Try to change password for uid=orla even though pw policy is set to no.
    # Should get error message: unwilling to Perform !
    orla = UserAccount(topo.standalone, f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    conn = orla.bind('000rla1')
    orla_conn = UserAccount(conn, f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    # pw policy is set to no
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        orla_conn.replace('userpassword', "000rla2")
    pwp = PwPolicyManager(topo.standalone)
    orl = pwp.get_pwpolicy_entry(f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    # Set Password change to May Change Password.
    orl.replace('passwordchange', 'on')
    assert orl.get_attr_val_utf8('passwordchange') == 'on'
    # Administrator Reseting to original password !
    orla.replace('userpassword', '000rla1')
    # Attempt to Modify password to orla2 with an invalid first pw with error message.
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        conn = orla.bind('Invalid_password')
    # Changing current password from orla1 to orla2
    orla_conn.replace('userpassword', '000rla2')
    # Changing current password from orla2 to orla1.
    orla_conn = UserAccount(conn, f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    orla_conn.replace('userpassword', '000rla1')
    # Set Password change to Must Not Change After Reset
    joe = pwp.get_pwpolicy_entry(f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    people = pwp.get_pwpolicy_entry(f'ou=people,{DEFAULT_SUFFIX}')
    joe.replace_many(('passwordmustchange', 'off'), ('passwordchange', 'off'))
    people.replace_many(('passwordmustchange', 'off'), ('passwordchange', 'off'))
    for attr in ['passwordMustChange', 'passwordchange']:
        assert joe.get_attr_val_utf8(attr) == 'off'
    for attr in ['passwordMustChange', 'passwordchange']:
        assert people.get_attr_val_utf8(attr) == 'off'
    # Change password for uid,joe,jack,deep even though pw policy is set to no with error message.
    for user, password, pass_to_change in [
        ('joe', '00j0e1', '00j0e2'),
        ('jack', '00j6ck1', '00j6ck2'),
        ('deep,ou=others', '00de3p1', '00de3p2')
    ]:
        real_user = UserAccount(topo.standalone, f'uid={user},ou=people,{DEFAULT_SUFFIX}')
        conn = real_user.bind(password)
        real_conn = UserAccount(conn, real_user.dn)
        # pw policy is set to no
        with pytest.raises(ldap.UNWILLING_TO_PERFORM):
            real_conn.replace('userpassword', pass_to_change)
    real_user = UserAccount(topo.standalone, f'uid=fred,{DEFAULT_SUFFIX}')
    conn = real_user.bind('00fr3d1')
    # Fred can change.(Fred is not belong to any pw policy)
    real_conn = UserAccount(conn, real_user.dn)
    real_conn.replace('userpassword', '00fr3d2')
    # Changing pw policy to may change pw
    # Set Password change to May Change Password
    joe = pwp.get_pwpolicy_entry(f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    joe.replace('passwordchange', 'on')
    assert joe.get_attr_val_utf8('passwordchange') == 'on'
    # Administrator Reseting to original password
    change_password_with_admin(topo, [
        ('uid=joe,ou=people', '00j0e1'),
        ('uid=jack,ou=people', '00j6ck1'),
        ('uid=fred', '00fr3d1'),
        ('uid=deep,ou=others,ou=people', '00de3p1')
    ])
    # Try to change password with invalid credentials.  Should see error message.
    for user in [
        'uid=joe,ou=people',
        'uid=jack,ou=people',
        'uid=fred',
        'uid=deep,ou=others,ou=people'
    ]:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}').bind("bad")
    # Changing current password for joe and fed.
    for user, password, new_pass in [
        ('uid=joe,ou=people', '00j0e1', '00j0e2'),
        ('uid=fred', '00fr3d1', '00fr3d2')
    ]:
        real_user = UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}')
        conn = real_user.bind(password)
        UserAccount(conn, real_user.dn).replace('userpassword', new_pass)
    # Changing current password for jack and deep with error message.(passwordchange not on)
    for user, password, new_pass in [
        ('uid=jack,ou=people', '00j6ck1', '00j6ck2'),
        ('uid=deep,ou=others,ou=people', '00de3p1', '00de3p2')
    ]:
        real_user = UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}')
        conn = real_user.bind(password)
        with pytest.raises(ldap.UNWILLING_TO_PERFORM):
            UserAccount(conn, real_user.dn).replace('userpassword', new_pass)
    # Changing pw policy to may change pw
    # Set Password change to May Change Password
    people.replace('passwordchange', 'on')
    assert people.get_attr_val_utf8('passwordchange') == 'on'
    # Administrator Reseting to original password
    change_password_with_admin(topo, [
        ('uid=joe,ou=people', '00j0e1'),
        ('uid=jack,ou=people', '00j6ck1'),
        ('uid=fred', '00fr3d1'),
        ('uid=deep,ou=others,ou=people', '00de3p1')
    ])
    # Try to change password with invalid credentials.  Should see error message.
    for user in [
        'uid=joe,ou=people',
        'uid=jack,ou=people',
        'uid=fred',
        'uid=deep,ou=others,ou=people'
    ]:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}').bind("bad")
    # Changing current password
    change_password(topo, [
        ('uid=joe,ou=people', '00j0e1', '00j0e2'),
        ('uid=fred', '00fr3d1', '00fr3d2'),
        ('uid=jack,ou=people', '00j6ck1', '00j6ck2'),
        ('uid=deep,ou=others,ou=people', '00de3p1', '00de3p2')
    ])
    # Set Password change to Must Not Change After Reset
    joe.replace('passwordchange', 'off')
    assert joe.get_attr_val_utf8('passwordchange') == 'off'
    # Administrator Reseting to original password
    change_password_with_admin(topo, [
        ('uid=joe,ou=people', '00j0e1'),
        ('uid=fred', '00fr3d1'),
        ('uid=jack,ou=people', '00j6ck1'),
        ('uid=deep,ou=others,ou=people', '00de3p1')
    ])
    # Try to change password with invalid credentials.  Should see error message
    for user in [
        'uid=joe,ou=people',
        'uid=jack,ou=people',
        'uid=fred',
        'uid=deep,ou=others,ou=people'
    ]:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}').bind("bad")
    # Changing current password
    change_password(topo, [
        ('uid=fred', '00fr3d1', '00fr3d2'),
        ('uid=jack,ou=people', '00j6ck1', '00j6ck2'),
        ('uid=deep,ou=others,ou=people', '00de3p1', '00de3p2')
    ])
    # ('passwordchange', 'off') for joe
    real_user = UserAccount(topo.standalone, f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    conn = real_user.bind('00j0e1')
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        UserAccount(conn, real_user.dn).replace('userpassword', '00j0e2')


@pytest.fixture(scope="function")
def _fixture_for_syntax_section(request, topo):
    change_password_with_admin(topo, [
        ('uid=joe,ou=people', '00j0e1'),
        ('uid=fred', '00fr3d1'),
        ('uid=jack,ou=people', '00j6ck1'),
        ('uid=deep,ou=others,ou=people', '00de3p1'),
        ('uid=orla,ou=dirsec', '000rla1'),
        ('uid=dbyers,ou=dirsec', 'Anuj')
    ])
    pwp = PwPolicyManager(topo.standalone)
    orl = pwp.get_pwpolicy_entry(f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    joe = pwp.get_pwpolicy_entry(f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    people = pwp.get_pwpolicy_entry(f'ou=people,{DEFAULT_SUFFIX}')
    for instance in [orl, joe, people]:
        instance.replace('passwordchecksyntax', 'on')
        instance.replace('passwordChange', 'on')
        assert instance.get_attr_val_utf8('passwordchecksyntax') == 'on'

    def final_step():
        for instance1 in [orl, joe, people]:
            instance1.replace('passwordminlength', '6')
        change_password_with_admin(topo, [
            ('uid=orla,ou=dirsec', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1'),
            ('uid=dbyers,ou=dirsec', 'dby3rs1'),
            ('uid=fred', '00fr3d1')
        ])

    request.addfinalizer(final_step)


def test_password_syntax_section(topo, _policy_setup, _fixture_for_syntax_section):
    """Password Syntax Section.

    :id: 7bf1cb46-9388-11ea-9019-8c16451d917b
    :setup: Standalone
    :steps:
        1. Try to change password with invalid credentials. Should get error (invalid cred).
        2. Try to change to a password that violates length.  Should get error (constaint viol.).
        3. Attempt to Modify password to db which is in error to policy
        4. Changing password minimum length to 5 to check triviality
        5. Try to change password to the value of uid, which is trivial. Should get error.
        6. Try to change password to givenname which is trivial.  Should get error
        7. Try to change password to sn which is trivial.  Should get error
        8. Changing password minimum length back to 6
        9. Changing current password from ``*1`` to ``*2``
        10. Changing current password from ``*2`` to ``*1``
        11. Changing current password to the evil password
        12. Resetting to original password as cn=directory manager
        13. Setting policy to NOT Check Password Syntax
        14. Test that when checking syntax is off, you can use small passwords
        15. Test that when checking syntax is off, trivial passwords can be used
        16. Resetting to original password as cn=directory manager
        17. Changing password minimum length from 6 to 10
        18. Setting policy to Check Password Syntax again
        19. Try to change to a password that violates length
        20. Change to a password that meets length requirement
    :expected results:
        1. Fail(invalid cred)
        2. Fail(constaint viol.)
        3. Fail(Syntax error)
        4. Success
        5. Fail(trivial)
        6. Fail(password to givenname )
        7. Success
        8. Success
        9. Success
        10. Success
        11. Fail(evil password)
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
        17. Success
        18. Success
        19. Fail(violates length)
        20. Success
    """
    # Try to change password with invalid credentials. Should get error (invalid cred).
    for user in [
        'uid=joe,ou=people',
        'uid=jack,ou=people',
        'uid=fred',
        'uid=deep,ou=others,ou=people',
        'uid=dbyers,ou=dirsec',
        'uid=orla,ou=dirsec'
    ]:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}').bind("bad")
    # Try to change to a password that violates length.  Should get error (constaint viol.).
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rla1', 'db'),
            ('uid=joe,ou=people', '00j0e1', 'db'),
            ('uid=jack,ou=people', '00j6ck1', 'db'),
            ('uid=deep,ou=others,ou=people', '00de3p1', 'db')
        ])
    # Attempt to Modify password to db which is in error to policy(Syntax error)
    change_password_ultra_new(topo, [
        ('uid=dbyers,ou=dirsec', 'Anuj', 'db', 'dby3rs1'),
        ('uid=fred', '00fr3d1', 'db', '00fr3d1')
    ])
    # Changing password minimum length to 5 to check triviality
    pwp = PwPolicyManager(topo.standalone)
    orl = pwp.get_pwpolicy_entry(f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    joe = pwp.get_pwpolicy_entry(f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    people = pwp.get_pwpolicy_entry(f'ou=people,{DEFAULT_SUFFIX}')
    for instance in [orl, joe, people]:
        instance.replace('passwordminlength', '5')
    # Try to change password to the value of uid, which is trivial. Should get error.
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rla1', 'orla'),
            ('uid=joe,ou=people', '00j0e1', 'joe'),
            ('uid=jack,ou=people', '00j6ck1', 'jack'),
            ('uid=deep,ou=others,ou=people', '00de3p1', 'deep')
        ])
    # dbyers and fred can change
    change_password_ultra_new(topo, [
        ('uid=dbyers,ou=dirsec', 'dby3rs1', 'dbyers', 'dby3rs1'),
        ('uid=fred', '00fr3d1', 'fred', '00fr3d1')
    ])
    # Try to change password to givenname which is trivial.  Should get error
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rla1', 'orla'),
            ('uid=joe,ou=people', '00j0e1', 'joe'),
            ('uid=jack,ou=people', '00j6ck1', 'jack'),
            ('uid=deep,ou=others,ou=people', '00de3p1', 'deep')
        ])
    # dbyers and fred can change
    change_password_ultra_new(topo, [
        ('uid=dbyers,ou=dirsec', 'dby3rs1', 'danny', 'dby3rs1'),
        ('uid=fred', '00fr3d1', 'fred', '00fr3d1')
    ])
    # Try to change password to sn which is trivial.  Should get error
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rla1', 'Hegarty'),
            ('uid=joe,ou=people', '00j0e1', 'Rath'),
            ('uid=jack,ou=people', '00j6ck1', 'Rath'),
            ('uid=deep,ou=others,ou=people', '00de3p1', 'Blue')
        ])
    # dbyers and fred can change
    change_password_ultra_new(topo, [
        ('uid=dbyers,ou=dirsec', 'dby3rs1', 'Byers', 'dby3rs1'),
        ('uid=fred', '00fr3d1', 'Byers', '00fr3d1')
    ])
    # Changing password minimum length back to 6
    for instance1 in [orl, joe, people]:
        instance1.replace('passwordminlength', '6')
    # Changing current password from *1 to *2
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rla1', '000rLb2'),
        ('uid=dbyers,ou=dirsec', 'dby3rs1', 'dby3rs2'),
        ('uid=fred', '00fr3d1', '00fr3d2'),
        ('uid=joe,ou=people', '00j0e1', '00J0e2'),
        ('uid=jack,ou=people', '00j6ck1', '00J6ck2'),
        ('uid=deep,ou=others,ou=people', '00de3p1', '00De3p2')
    ])
    # Changing current password from *2 to *1
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb2', '000rLb1'),
        ('uid=dbyers,ou=dirsec', 'dby3rs2', 'dby3rs1'),
        ('uid=fred', '00fr3d2', '00fr3d1'),
        ('uid=joe,ou=people', '00J0e2', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck2', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p2', '00De3p1')
    ])
    # Changing current password to the evil password
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb1', r'{\;\\].'),
        ('uid=joe,ou=people', '00J0e1', r'{\;\\].'),
        ('uid=jack,ou=people', '00J6ck1', r'{\;\\].'),
        ('uid=deep,ou=others,ou=people', '00De3p1', r'{\;\\].')
    ])
    # dbyers and fred can change
    change_password(topo, [
        ('uid=dbyers,ou=dirsec', 'dby3rs1', r'{\;\\].'),
        ('uid=fred', '00fr3d1', r'{\;\\].')
    ])
    # Resetting to original password as cn=directory manager
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1'),
        ('uid=dbyers,ou=dirsec', 'dby3rs1'),
        ('uid=fred', '00fr3d1')
    ])
    # Setting policy to NOT Check Password Syntax
    # Searching for passwordminlength
    for instance in [orl, joe, people]:
        instance.replace('passwordchecksyntax', 'off')
    for instance in [orl, joe, people]:
        assert instance.get_attr_val_utf8('passwordchecksyntax') == 'off'
        assert instance.get_attr_val_utf8('passwordminlength') == '6'
    # Test that when checking syntax is off, you can use small passwords
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb1', 'db'),
        ('uid=joe,ou=people', '00J0e1', 'db'),
        ('uid=jack,ou=people', '00J6ck1', 'db'),
        ('uid=deep,ou=others,ou=people', '00De3p1', 'db'),
        ('uid=dbyers,ou=dirsec', 'dby3rs1', 'db'),
        ('uid=fred', '00fr3d1', 'db')
    ])
    # Test that when checking syntax is off, trivial passwords can be used
    change_password(topo, [
        ('uid=orla,ou=dirsec', 'db', 'orla'),
        ('uid=joe,ou=people', 'db', 'joe'),
        ('uid=jack,ou=people', 'db', 'jack'),
        ('uid=deep,ou=others,ou=people', 'db', 'deep'),
        ('uid=dbyers,ou=dirsec', 'db', 'dbyers'),
        ('uid=fred', 'db', 'fred')
    ])
    # Resetting to original password as cn=directory manager
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1'),
        ('uid=dbyers,ou=dirsec', 'dby3rs1'),
        ('uid=fred', '00fr3d1')
    ])
    # Changing password minimum length from 6 to 10
    # Setting policy to Check Password Syntax again
    for instance in [orl, joe, people]:
        instance.replace_many(
            ('passwordchecksyntax', 'on'),
            ('passwordminlength', '10'))
    # Try to change to a password that violates length
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb1', 'db'),
        ('uid=joe,ou=people', '00J0e1', 'db'),
        ('uid=jack,ou=people', '00J6ck1', 'db'),
        ('uid=deep,ou=others,ou=people', '00De3p1', 'db')
    ])
    # dbyers and fred can change as it does not belong to any pw policy
    change_password(topo, [
        ('uid=dbyers,ou=dirsec', 'dby3rs1', 'db'),
        ('uid=fred', '00fr3d1', 'db')
    ])
    # Change to a password that meets length requirement
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb1', 'This_IS_a_very_very_long_password'),
        ('uid=joe,ou=people', '00J0e1', 'This_IS_a_very_very_long_password'),
        ('uid=jack,ou=people', '00J6ck1', 'This_IS_a_very_very_long_password'),
        ('uid=deep,ou=others,ou=people', '00De3p1', 'This_IS_a_very_very_long_password'),
        ('uid=dbyers,ou=dirsec', 'db', 'This_IS_a_very_very_long_password'),
        ('uid=fred', 'db', 'This_IS_a_very_very_long_password')
    ])


@pytest.fixture(scope="function")
def _fixture_for_password_history(request, topo):
    pwp = PwPolicyManager(topo.standalone)
    orl = pwp.get_pwpolicy_entry(f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    joe = pwp.get_pwpolicy_entry(f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    people = pwp.get_pwpolicy_entry(f'ou=people,{DEFAULT_SUFFIX}')
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1')
    ])
    for instance in [orl, joe, people]:
        instance.replace_many(
            ('passwordhistory', 'on'),
            ('passwordinhistory', '3'),
            ('passwordChange', 'on'))
    for instance in [orl, joe, people]:
        assert instance.get_attr_val_utf8('passwordhistory') == 'on'
        assert instance.get_attr_val_utf8('passwordinhistory') == '3'
        assert instance.get_attr_val_utf8('passwordChange') == 'on'

    def final_step():
        for instance1 in [orl, joe, people]:
            instance1.replace('passwordhistory', 'off')
        change_password_with_admin(topo, [
            ('uid=orla,ou=dirsec', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1')
        ])
    request.addfinalizer(final_step)


def test_password_history_section(topo, _policy_setup, _fixture_for_password_history):
    """Password History Section.

        :id: 51f459a0-a0ba-11ea-ade7-8c16451d917b
        :setup: Standalone
        :steps:
            1. Changing current password for orla,joe,jack and deep
            2. Checking that the passwordhistory attribute has been added !
            3. Try to change the password back which should fail
            4. Change the passwords for all four test users to something new
            5. Try to change passwords back to the first password
            6. Change to a fourth password not in password history
            7. Try to change all the passwords back to the first password
            8. Change the password to one more new password as root dn
            9. Now try to change the password back to the first password
            10. Checking that password history does still containt the previous 3 passwords
            11. Add a password test for long long password (more than 490 bytes).
            12. Changing password : LONGPASSWORD goes in history
            13. Setting policy to NOT keep password histories
            14. Changing current password from ``*2 to ``*2``
            15. Try to change ``*2`` to ``*1``, should succeed
        :expected results:
            1. Success
            2. Success
            3. Fail(ldap.CONSTRAINT_VIOLATION)
            4. Success
            5. Fail(ldap.CONSTRAINT_VIOLATION))
            6. Success
            7. Fail(ldap.CONSTRAINT_VIOLATION))
            8. Success
            9. Success
            10. Success
            11. Success
            12. Success
            13. Success
            14. Success
            15. Success
    """
    # Changing current password for orla,joe,jack and deep
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb2'),
        ('uid=joe,ou=people', '00J0e2'),
        ('uid=jack,ou=people', '00J6ck2'),
        ('uid=deep,ou=others,ou=people', '00De3p2'),
    ])
    time.sleep(1)
    # Checking that the password history attribute has been added !
    for user, password in [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1'),
    ]:
        assert password in UserAccount(topo.standalone,
                                       f'{user},{DEFAULT_SUFFIX}').get_attr_val_utf8("passwordhistory")
    # Try to change the password back which should fail
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rLb2', '000rLb1'),
            ('uid=joe,ou=people', '00J0e2', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck2', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p2', '00De3p1'),
        ])
    # Change the passwords for all four test users to something new
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb3'),
        ('uid=joe,ou=people', '00J0e3'),
        ('uid=jack,ou=people', '00J6ck3'),
        ('uid=deep,ou=others,ou=people', '00De3p3')
    ])
    # Try to change passwords back to the first password
    time.sleep(1)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rLb3', '000rLb1'),
            ('uid=joe,ou=people', '00J0e3', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck3', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p3', '00De3p1'),
        ])
    # Change to a fourth password not in password history
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb4'),
        ('uid=joe,ou=people', '00J0e4'),
        ('uid=jack,ou=people', '00J6ck4'),
        ('uid=deep,ou=others,ou=people', '00De3p4')
    ])
    time.sleep(1)
    # Try to change all the passwords back to the first password
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rLb4', '000rLb1'),
            ('uid=joe,ou=people', '00J0e4', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck4', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p4', '00De3p1')
        ])
    # change the password to one more new password as root dn
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb5'),
        ('uid=joe,ou=people', '00J0e5'),
        ('uid=jack,ou=people', '00J6ck5'),
        ('uid=deep,ou=others,ou=people', '00De3p5')
    ])
    time.sleep(1)
    # Now try to change the password back to the first password
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb5', '000rLb1'),
        ('uid=joe,ou=people', '00J0e5', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck5', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p5', '00De3p1')
    ])
    time.sleep(1)
    # checking that password history does still containt the previous 3 passwords
    for user, password3, password2, password1 in [
        ('uid=orla,ou=dirsec', '000rLb5', '000rLb4', '000rLb3'),
        ('uid=joe,ou=people', '00J0e5', '00J0e4', '00J0e3'),
        ('uid=jack,ou=people', '00J6ck5', '00J6ck4', '00J6ck3'),
        ('uid=deep,ou=others,ou=people', '00De3p5', '00De3p4', '00De3p3')
    ]:
        user1 = UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}')
        pass_list = ''.join(user1.get_attr_vals_utf8("passwordhistory"))
        assert password1 in pass_list
        assert password2 in pass_list
        assert password3 in pass_list
    # Add a password test for long long password (more than 490 bytes).
    long = '01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901' \
           '23456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456' \
           '789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012' \
           '345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678' \
           '901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234' \
           '5678901234567890123456789LENGTH=510'
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb1', long),
        ('uid=joe,ou=people', '00J0e1', long),
        ('uid=jack,ou=people', '00J6ck1', long),
        ('uid=deep,ou=others,ou=people', '00De3p1', long)
    ])
    time.sleep(1)
    # Changing password : LONGPASSWORD goes in history
    change_password(topo, [
        ('uid=orla,ou=dirsec', long, '000rLb2'),
        ('uid=joe,ou=people', long, '00J0e2'),
        ('uid=jack,ou=people', long, '00J6ck2'),
        ('uid=deep,ou=others,ou=people', long, '00De3p2')
    ])
    time.sleep(1)
    for user, password in [
        ('uid=orla,ou=dirsec', '000rLb2'),
        ('uid=joe,ou=people', '00J0e2'),
        ('uid=jack,ou=people', '00J6ck2'),
        ('uid=deep,ou=others,ou=people', '00De3p2')
    ]:
        real_user = UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}')
        conn = real_user.bind(password)
        assert long in ''.join(UserAccount(conn,
                                           f'{user},{DEFAULT_SUFFIX}').get_attr_vals_utf8("passwordhistory"))
    # Setting policy to NOT keep password histories
    _do_transaction_for_pwp(topo, 'passwordhistory', 'off')
    time.sleep(1)
    # Changing current password from *2 to *2
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb2', '000rLb2'),
        ('uid=joe,ou=people', '00J0e2', '00J0e2'),
        ('uid=jack,ou=people', '00J6ck2', '00J6ck2'),
        ('uid=deep,ou=others,ou=people', '00De3p2', '00De3p2')
    ])
    # Try to change *2 to *1, should succeed
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb2', '000rLb1'),
        ('uid=joe,ou=people', '00J0e2', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck2', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p2', '00De3p1')
    ])


@pytest.fixture(scope="function")
def _fixture_for_password_min_age(request, topo):
    pwp = PwPolicyManager(topo.standalone)
    orl = pwp.get_pwpolicy_entry(f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    joe = pwp.get_pwpolicy_entry(f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    people = pwp.get_pwpolicy_entry(f'ou=people,{DEFAULT_SUFFIX}')
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1')
    ])
    for pwp1 in [orl, joe, people]:
        assert pwp1.get_attr_val_utf8('passwordminage') == '0'
        pwp1.replace_many(
            ('passwordminage', '10'),
            ('passwordChange', 'on'))

    def final_step():
        for pwp2 in [orl, joe, people]:
            pwp2.replace('passwordminage', '0')
    request.addfinalizer(final_step)


def test_password_minimum_age_section(topo, _policy_setup, _fixture_for_password_min_age):
    """Password History Section.

        :id: 470f5b2a-a0ba-11ea-ab2d-8c16451d917b
        :setup: Standalone
        :steps:
            1. Searching for password minimum age, should be 0 per defaults set
            2. Change current password from ``*1`` to ``*2``
            3. Wait 5 secs and try to change again.  Should fail.
            4. Wait more time to complete password min age
            5. Now user can change password
        :expected results:
            1. Success
            2. Success
            3. Fail(ldap.CONSTRAINT_VIOLATION)
            4. Success
            5. Success
    """
    # Change current password from *1 to *2
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb1', '000rLb2'),
        ('uid=joe,ou=people', '00J0e1', '00J0e2'),
        ('uid=jack,ou=people', '00J6ck1', '00J6ck2'),
        ('uid=deep,ou=others,ou=people', '00De3p1', '00De3p2')
    ])
    # Wait 5 secs and try to change again.  Should fail.
    count = 0
    while count < 8:
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            change_password(topo, [
                ('uid=orla,ou=dirsec', '000rLb2', '000rLb1'),
                ('uid=joe,ou=people', '00J0e2', '00J0e1'),
                ('uid=jack,ou=people', '00J6ck2', '00J6ck1'),
                ('uid=deep,ou=others,ou=people', '00De3p2', '00De3p1')
            ])
        time.sleep(1)
        count += 1
    # Wait more time to complete password min age
    time.sleep(3)
    # Now user can change password
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb2', '000rLb1'),
        ('uid=joe,ou=people', '00J0e2', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck2', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p2', '00De3p1')
    ])


@pytest.fixture(scope="function")
def _fixture_for_password_lock_out(request, topo):
    pwp = PwPolicyManager(topo.standalone)
    orl = pwp.get_pwpolicy_entry(f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    joe = pwp.get_pwpolicy_entry(f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    people = pwp.get_pwpolicy_entry(f'ou=people,{DEFAULT_SUFFIX}')
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1')
    ])
    for pwp1 in [orl, joe, people]:
        assert pwp1.get_attr_val_utf8('passwordlockout') == 'off'
        pwp1.replace_many(
            ('passwordlockout', 'on'),
            ('passwordlockoutduration', '3'),
            ('passwordresetfailurecount', '3'),
            ('passwordChange', 'on'))

    def final_step():
        for instance in [orl, joe, people]:
            instance.replace('passwordlockout', 'off')
            instance.replace('passwordunlock', 'off')
            assert instance.get_attr_val_utf8('passwordlockout') == 'off'
            assert instance.get_attr_val_utf8('passwordunlock') == 'off'
    request.addfinalizer(final_step)


def test_account_lockout_and_lockout_duration_section(topo, _policy_setup, _fixture_for_password_lock_out):
    """Account Lockout and Lockout Duration Section

        :id: 1ff0b7a4-b560-11ea-9ece-8c16451d917b
        :setup: Standalone
        :steps:
            1. Try to bind with invalid credentials
            2. Try to bind with valid pw, should give lockout error
            3. After 3 seconds Try to bind with valid pw, should work
            4. Try to bind with invalid credentials
            5. Attempt to bind with valid pw after timeout is up
            6. Resetting with root can break lockout
        :expected results:
            1. Fail(ldap.INVALID_CREDENTIALS)
            2. Fail(ldap.CONSTRAINT_VIOLATION)
            3. Success
            4. Fail(ldap.INVALID_CREDENTIALS))
            5. Success
            6. Success
    """
    # Try to bind with invalid credentials
    for count1 in range(3):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            change_password(topo, [
                ('uid=orla,ou=dirsec', 'Invalid', 'Invalid'),
                ('uid=joe,ou=people', 'Invalid', 'Invalid'),
                ('uid=jack,ou=people', 'Invalid', 'Invalid'),
                ('uid=deep,ou=others,ou=people', 'Invalid', 'Invalid')
            ])
    # Try to bind with valid pw, should give lockout error
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rLb1', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1', '00De3p1')
        ])
    # Try to bind with valid pw, should work
    time.sleep(3)
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb1', '000rLb2'),
        ('uid=joe,ou=people', '00J0e1', '00J0e2'),
        ('uid=jack,ou=people', '00J6ck1', '00J6ck2'),
        ('uid=deep,ou=others,ou=people', '00De3p1', '00De3p2')
    ])
    # Try to bind with invalid credentials
    for count1 in range(2):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            change_password(topo, [
                ('uid=orla,ou=dirsec', 'Invalid', 'Invalid'),
                ('uid=joe,ou=people', 'Invalid', 'Invalid'),
                ('uid=jack,ou=people', 'Invalid', 'Invalid'),
                ('uid=deep,ou=others,ou=people', 'Invalid', 'Invalid')
            ])
    # Attempt to bind with valid pw after timeout is up
    time.sleep(3)
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb2', '000rLb1'),
        ('uid=joe,ou=people', '00J0e2', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck2', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p2', '00De3p1')
    ])
    # Resetting with root can break lockout
    for count1 in range(3):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            change_password(topo, [
                ('uid=orla,ou=dirsec', 'Invalid', 'Invalid'),
                ('uid=joe,ou=people', 'Invalid', 'Invalid'),
                ('uid=jack,ou=people', 'Invalid', 'Invalid'),
                ('uid=deep,ou=others,ou=people', 'Invalid', 'Invalid')
            ])
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rLb1', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1', '00De3p1')
        ])
    change_password_with_admin(topo, [
            ('uid=orla,ou=dirsec', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1')
        ])
    change_password(topo, [
            ('uid=orla,ou=dirsec', '000rLb1', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1', '00De3p1')
        ])


@pytest.fixture(scope="function")
def _fixture_for_grace_limit(topo):
    pwp = PwPolicyManager(topo.standalone)
    orl = pwp.get_pwpolicy_entry(f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    joe = pwp.get_pwpolicy_entry(f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    people = pwp.get_pwpolicy_entry(f'ou=people,{DEFAULT_SUFFIX}')
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1'),
        ('uid=fred', '00fr3d1')
    ])
    for instance in [orl, joe, people]:
        instance.replace_many(('passwordMaxAge', '3'),
                              ('passwordGraceLimit', '7'),
                              ('passwordexp', 'on'),
                              ('passwordwarning', '30'),
                              ('passwordChange', 'on'))


def _bind_self(topo, user_password_new_pass_list):
    """
    Will bind password with self.
    """
    for user, password in user_password_new_pass_list:
        real_user = UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}')
        conn = real_user.bind(password)


def test_grace_limit_section(topo, _policy_setup, _fixture_for_grace_limit):
    """Account Lockout and Lockout Duration Section

    :id: 288e3756-b560-11ea-9390-8c16451d917b
    :setup: Standalone
    :steps:
        1. Check users have 7 grace login attempts after their password expires
        2. Wait for password expiration
        3. The the 8th should fail except fred who defaults to global password policy
        4. Now try resetting the password before the grace login attempts run out
        5. Wait for password expiration
        6. Now change the password as the 7th attempt
        7. Wait for password expiration
        8. First 7 good attempts
        9. The the 8th should fail except fred who defaults to global password policy
        10. Changing the paswordMaxAge to 0 so expiration is immediate test
        11. Modify the users passwords to start the clock of zero
        12. PasswordGraceLimit to 0, passwordMaxAge to 3 seconds
        13. Modify the users passwords to start the clock
        14. Users should be blocked
        15. Removing the passwordgracelimit attribute should make it default to 0
    :expected results:
        1. Success
        2. Success
        3. Fail(ldap.INVALID_CREDENTIALS)
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Fail(ldap.INVALID_CREDENTIALS)
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
    """
    # Check users have 7 grace login attempts after their password expires
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb2'),
        ('uid=joe,ou=people', '00J0e2'),
        ('uid=jack,ou=people', '00J6ck2'),
        ('uid=deep,ou=others,ou=people', '00De3p2'),
        ('uid=fred', '00fr3d2')
    ])
    # Wait for password expiration
    time.sleep(3)
    # The the 8th should fail except fred who defaults to global password policy
    for _ in range(7):
        _bind_self(topo, [
            ('uid=orla,ou=dirsec', '000rLb2'),
            ('uid=joe,ou=people', '00J0e2'),
            ('uid=jack,ou=people', '00J6ck2'),
            ('uid=deep,ou=others,ou=people', '00De3p2'),
            ('uid=fred', '00fr3d2')
        ])
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        _bind_self(topo, [
            ('uid=orla,ou=dirsec', '000rLb2'),
            ('uid=joe,ou=people', '00J0e2'),
            ('uid=jack,ou=people', '00J6ck2'),
            ('uid=deep,ou=others,ou=people', '00De3p2')
        ])
    _bind_self(topo, [
        ('uid=fred', '00fr3d2')
    ])
    # Now try resetting the password before the grace login attempts run out
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1'),
        ('uid=fred', '00fr3d1')
    ])
    # Wait for password expiration
    time.sleep(3)
    # first 6 good attempts
    for _ in range(6):
        _bind_self(topo, [
            ('uid=orla,ou=dirsec', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1'),
            ('uid=fred', '00fr3d1')
        ])
    # now change the password as the 7th attempt
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb1', '000rLb2'),
        ('uid=joe,ou=people', '00J0e1', '00J0e2'),
        ('uid=jack,ou=people', '00J6ck1', '00J6ck2'),
        ('uid=deep,ou=others,ou=people', '00De3p1', '00De3p2'),
        ('uid=fred', '00fr3d1', '00fr3d2')
    ])
    # Wait for password expiration
    time.sleep(3)
    # first 7 good attempts
    for _ in range(7):
        _bind_self(topo, [
            ('uid=orla,ou=dirsec', '000rLb2'),
            ('uid=joe,ou=people', '00J0e2'),
            ('uid=jack,ou=people', '00J6ck2'),
            ('uid=deep,ou=others,ou=people', '00De3p2'),
            ('uid=fred', '00fr3d2')
        ])
    # The the 8th should fail except fred who defaults to global password policy
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        _bind_self(topo, [
        ('uid=orla,ou=dirsec', '000rLb2'),
        ('uid=joe,ou=people', '00J0e2'),
        ('uid=jack,ou=people', '00J6ck2'),
        ('uid=deep,ou=others,ou=people', '00De3p2')
    ])
    _bind_self(topo, [
        ('uid=fred', '00fr3d2')
    ])
    # Changing the paswordMaxAge to 0 so expiration is immediate test to see
    # that the user still has 7 grace login attempts before locked out
    for att1 in ['passwordMaxAge', 'passwordwarning']:
        _do_transaction_for_pwp(topo, att1, '0')
    # Modify the users passwords to start the clock of zero
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1'),
        ('uid=fred', '00fr3d1')
    ])
    # first 7 good attempts
    for _ in range(7):
        _bind_self(topo, [
            ('uid=orla,ou=dirsec', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1'),
            ('uid=fred', '00fr3d1')
        ])
    # The the 8th should fail ....
    # except fred who defaults to global password policy
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        _bind_self(topo, [
            ('uid=orla,ou=dirsec', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1')
        ])
    _bind_self(topo, [
        ('uid=fred', '00fr3d1')
    ])
    # setting the passwordMaxAge to 3 seconds once more
    # and the passwordGraceLimit to 0
    for att1, att2 in [('passwordMaxAge', '3'), ('passwordGraceLimit', '0')]:
        _do_transaction_for_pwp(topo, att1, att2)
    # modify the users passwords to start the clock
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1'),
        ('uid=fred', '00fr3d1')
    ])
    # Users should be blocked
    time.sleep(3)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        _bind_self(topo, [
            ('uid=orla,ou=dirsec', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1')
        ])
    _bind_self(topo, [
        ('uid=fred', '00fr3d1')
    ])
    for att1, att2 in [('passwordGraceLimit', '10')]:
        _do_transaction_for_pwp(topo, att1, att2)
    # removing the passwordgracelimit attribute should make it default to 0
    for att1, att2 in [('passwordGraceLimit', ' ')]:
        _do_transaction_for_pwp(topo, att1, att2)
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1'),
        ('uid=fred', '00fr3d1')
    ])
    time.sleep(3)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        _bind_self(topo, [
            ('uid=orla,ou=dirsec', '000rLb1'),
            ('uid=joe,ou=people', '00J0e1'),
            ('uid=jack,ou=people', '00J6ck1'),
            ('uid=deep,ou=others,ou=people', '00De3p1')
        ])
    _bind_self(topo, [
        ('uid=fred', '00fr3d1')
    ])


@pytest.fixture(scope="function")
def _fixture_for_additional_cases(topo):
    pwp = PwPolicyManager(topo.standalone)
    orl = pwp.get_pwpolicy_entry(f'uid=orla,ou=dirsec,{DEFAULT_SUFFIX}')
    joe = pwp.get_pwpolicy_entry(f'uid=joe,ou=people,{DEFAULT_SUFFIX}')
    people = pwp.get_pwpolicy_entry(f'ou=people,{DEFAULT_SUFFIX}')
    change_password_with_admin(topo, [
        ('uid=orla,ou=dirsec', '000rLb1'),
        ('uid=joe,ou=people', '00J0e1'),
        ('uid=jack,ou=people', '00J6ck1'),
        ('uid=deep,ou=others,ou=people', '00De3p1'),
        ('uid=fred', '00fr3d1'),
        ('uid=dbyers,ou=dirsec', 'dby3rs1')
    ])
    for instance in [orl, joe, people]:
        instance.replace_many(('passwordChange', 'on'),
                              ('passwordchecksyntax', 'off'))


def test_additional_corner_cases(topo, _policy_setup, _fixture_for_additional_cases):
    """Additional corner cases

    :id: 2f6cec66-b560-11ea-9d7c-8c16451d917b
    :setup: Standalone
    :steps:
        1. Try to change password to one containing spaces
        2. Setting password policy to Check password syntax
        3. Try to change password to the value of mail, which is trivial. Should get error.
        4. No error for fred and dbyers as they are not included in PW policy.
        5. Revert changes for fred and dbyers
        6. Try to change password to the value of ou, which is trivial. Should get error.
        7. No error for fred and dbyers as they are not included in PW policy.
        8. Revert changes for fred and dbyers
    :expected results:
        1. Success
        2. Success
        3. Fail(CONSTRAINT_VIOLATION)
        4. Success
        5. Success
        6. Fail(CONSTRAINT_VIOLATION)
        7. Success
        8. Success
    """
    # Try to change password to one containing spaces
    change_password(topo, [
        ('uid=orla,ou=dirsec', '000rLb1', 'This Password has spaces.'),
        ('uid=joe,ou=people', '00J0e1', 'This Password has spaces.'),
        ('uid=jack,ou=people', '00J6ck1', 'This Password has spaces.'),
        ('uid=fred', '00fr3d1', 'This Password has spaces.'),
        ('uid=deep,ou=others,ou=people', '00De3p1', 'This Password has spaces.'),
        ('uid=dbyers,ou=dirsec', 'dby3rs1', 'This Password has spaces.')
    ])
    change_password(topo, [
        ('uid=orla,ou=dirsec', 'This Password has spaces.', '000rLb1'),
        ('uid=joe,ou=people', 'This Password has spaces.', '00j0e1'),
        ('uid=jack,ou=people', 'This Password has spaces.', '00j6ck1'),
        ('uid=fred', 'This Password has spaces.', '00fr3d1'),
        ('uid=deep,ou=others,ou=people', 'This Password has spaces.', '00de3p1'),
        ('uid=dbyers,ou=dirsec', 'This Password has spaces.', 'dby3rs1')
    ])
    # Setting password policy to Check password syntax
    for attr, para in [('passwordchecksyntax', 'on'), ('passwordminlength', '5')]:
        _do_transaction_for_pwp(topo, attr, para)
    # Try to change password to the value of mail, which is trivial. Should get error.
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rLb1', 'orla@example.com'),
            ('uid=joe,ou=people', '00j0e1', 'joe@example.com'),
            ('uid=jack,ou=people', '00j6ck1', 'jack@example.com'),
            ('uid=deep,ou=others,ou=people', '00de3p1', 'deep@example.com')
        ])
    # No error for fred and dbyers as they are not included in PW policy.
    change_password(topo, [
        ('uid=fred', '00fr3d1', 'fred@example.com'),
        ('uid=dbyers,ou=dirsec', 'dby3rs1', 'dbyers@example.com')
    ])
    # Revert changes for fred and dbyers
    change_password(topo, [
        ('uid=fred', 'fred@example.com', '00fr3d1'),
        ('uid=dbyers,ou=dirsec', 'dbyers@example.com', 'dby3rs1')
    ])
    # Creating OUs.
    for user, new_ou in [
        ('uid=orla,ou=dirsec', 'dirsec'),
        ('uid=joe,ou=people', 'people'),
        ('uid=jack,ou=people', 'people'),
        ('uid=deep,ou=others,ou=people', 'others'),
        ('uid=dbyers,ou=dirsec', 'dirsec')
    ]:
        UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}').add('ou', new_ou)
    # Try to change password to the value of ou, which is trivial. Should get error.
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        change_password(topo, [
            ('uid=orla,ou=dirsec', '000rLb1', 'dirsec'),
            ('uid=joe,ou=people', '00j0e1', 'people'),
            ('uid=jack,ou=people', '00j6ck1', 'people'),
            ('uid=deep,ou=others,ou=people', '00de3p1', 'others')
        ])
    # No error for byers as it is  not included in PW policy.
    change_password(topo, [('uid=dbyers,ou=dirsec', 'dby3rs1', 'dirsec')])
    # Revert changes for dbyers
    change_password_with_admin(topo, [
        ('uid=fred', '00fr3d1'),
        ('uid=dbyers,ou=dirsec', 'dby3rs1')
    ])


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
