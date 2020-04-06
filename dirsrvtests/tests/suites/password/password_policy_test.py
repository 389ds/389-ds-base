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
    """ Password Change Section.

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
    """ Password Syntax Section.

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
        9. Changing current password from *1 to *2
        10. Changing current password from *2 to *1
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


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)