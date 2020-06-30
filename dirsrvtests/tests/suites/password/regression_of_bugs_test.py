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
from lib389._constants import DEFAULT_SUFFIX, DN_DM
from lib389.config import Config
from lib389.idm.domain import Domain
from lib389.idm.group import UniqueGroups, UniqueGroup
from lib389.idm.organizationalunit import OrganizationalUnits, OrganizationalUnit
from lib389.pwpolicy import PwPolicyManager
import time
import ldap

pytestmark = pytest.mark.tier1


def _create_user(topo, uid, ou):
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=ou).create(properties={
        'uid': uid,
        'cn': uid,
        'sn': uid,
        'mail': f'{uid}@example.com',
        'homeDirectory': f'/home/{uid}',
        'uidNumber': '1000',
        'gidNumber': '1000'
    })
    return user


def change_pwp_parameter(topo, pwp, operation, to_do):
    """
    Will change password policy parameter
    """
    pwp1 = PwPolicyManager(topo.standalone)
    user = pwp1.get_pwpolicy_entry(f'{pwp},{DEFAULT_SUFFIX}')
    user.replace(operation, to_do)


def _create_pwp(topo, instance):
    """
    Will  create pwp
    """
    policy_props = {}
    pwp = PwPolicyManager(topo.standalone)
    pwadm_locpol = pwp.create_subtree_policy(instance, policy_props)
    for attribute, value in [
        ('passwordexp', 'off'),
        ('passwordchange', 'off'),
        ('passwordmustchange', 'off'),
        ('passwordchecksyntax', 'off'),
        ('passwordinhistory', '6'),
        ('passwordhistory', 'off'),
        ('passwordlockout', 'off'),
        ('passwordlockoutduration', '3600'),
        ('passwordmaxage', '8640000'),
        ('passwordmaxfailure', '3'),
        ('passwordminage', '0'),
        ('passwordminlength', '6'),
        ('passwordresetfailurecount', '600'),
        ('passwordunlock', 'on'),
        ('passwordStorageScheme', 'CLEAR'),
        ('passwordwarning', '86400')
    ]:
        pwadm_locpol.add(attribute, value)
    return pwadm_locpol


def change_password_of_user(topo, user_password_new_pass_list, pass_to_change):
    """
    Will change password with self binding.
    """
    for user, password, new_pass in user_password_new_pass_list:
        real_user = UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}')
        conn = real_user.bind(password)
        UserAccount(conn, pass_to_change).replace('userpassword', new_pass)


@pytest.fixture(scope="function")
def _add_user(request, topo):
    for uid, ou_ou in [('pwadm_user_1', None), ('pwadm_user_2', 'ou=People')]:
        _create_user(topo, uid, ou_ou)
    for uid, ou_ou in [('pwadm_admin_2', 'ou=People'),
                       ('pwadm_admin_3', 'ou=People'),
                       ('pwadm_admin_4', 'ou=People')]:
        user = _create_user(topo, uid, ou_ou)
        user.replace('userpassword', 'Secret123')

    def fin():
        for user1 in UserAccounts(topo.standalone, DEFAULT_SUFFIX).list():
            user1.delete()
        for user1 in UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None).list():
            user1.delete()
    request.addfinalizer(fin)


@pytest.mark.bz1044164
def test_local_password_policy(topo, _add_user):
    """Regression test for bz1044164 part 1.

    :id: d6f4a7fa-473b-11ea-8766-8c16451d917b
    :setup: Standalone
    :steps:
        1. Add a User as Password Admin
        2. Create a password admin user entry
        3. Add an aci to allow this user all rights
        4. Configure password admin
        5. Create local password policy and enable passwordmustchange
        6. Add another generic user but do not include the password (userpassword)
        7. Use admin user to perform a password update on generic user
        8. We don't need this ACI anymore. Delete it
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """
    # Add a User as Password Admin
    # Create a password admin user entry
    user = _create_user(topo, 'pwadm_admin_1', None)
    user.replace('userpassword', 'Secret123')
    domian = Domain(topo.standalone, DEFAULT_SUFFIX)
    # Add an aci to allow this user all rights
    domian.set("aci", f'(targetattr ="userpassword")'
                      f'(version 3.0;acl "Allow password admin to write user '
                      f'passwords";allow (write)(userdn = "ldap:///{user.dn}");)')
    # Configure password admin
    # Create local password policy and enable passwordmustchange
    Config(topo.standalone).replace_many(
        ('passwordAdminDN', user.dn),
        ('passwordMustChange', 'off'),
        ('nsslapd-pwpolicy-local', 'on'))
    # Add another generic user but do not include the password (userpassword)
    # Use admin user to perform a password update on generic user
    real_user = UserAccount(topo.standalone, f'uid=pwadm_admin_1,{DEFAULT_SUFFIX}')
    conn = real_user.bind('Secret123')
    UserAccount(conn, f'uid=pwadm_user_1,{DEFAULT_SUFFIX}').replace('userpassword', 'hello')
    # We don't need this ACI anymore. Delete it
    domian.remove("aci", f'(targetattr ="userpassword")'
                         f'(version 3.0;acl "Allow password admin to write user '
                         f'passwords";allow (write)(userdn = "ldap:///{user.dn}");)')


@pytest.mark.bz1118006
def test_passwordexpirationtime_attribute(topo, _add_user):
    """Regression test for bz1118006.

    :id: 867472d2-473c-11ea-b583-8c16451d917b
    :setup: Standalone
    :steps:
        1. Check that the passwordExpirationTime attribute is set to the epoch date
    :expected results:
        1. Success
    """
    Config(topo.standalone).replace('passwordMustChange', 'on')
    epoch_date = "19700101000000Z"
    time.sleep(1)
    user = UserAccount(topo.standalone, f'uid=pwadm_user_1,{DEFAULT_SUFFIX}')
    user.replace('userpassword', 'Secret123')
    time.sleep(1)
    # Check that the passwordExpirationTime attribute is set to the epoch date
    assert user.get_attr_val_utf8('passwordExpirationTime') == epoch_date
    Config(topo.standalone).replace('passwordMustChange', 'off')
    time.sleep(1)


@pytest.mark.bz1118007
@pytest.mark.bz1044164
def test_admin_group_to_modify_password(topo, _add_user):
    """Regression test for bz1044164 part 2.

    :id: 12e09446-52da-11ea-aa11-8c16451d917b
    :setup: Standalone
    :steps:
        1. Create unique members of admin group
        2. Create admin group with unique members
        3. Edit ACIs for admin group
        4. Add group as password admin
        5. Test password admin group to modify password of another admin user
        6. Use admin user to perform a password update on Directory Manager user
        7. Test password admin group for local password policy
        8. Add top level container
        9. Add user
        10. Create local policy configuration entry
        11. Adding admin group for local policy
        12. Change user's password by admin user. Break the local policy rule
        13. Test password admin group for global password policy
        14. Add top level container
        15. Change user's password by admin user. Break the global policy rule
        16. Add new user in password admin group
        17. Modify ordinary user's password
        18. Modify user DN using modrdn of a user in password admin group
        19. Test assigning invalid value to password admin attribute
        20. Try to add more than one Password Admin attribute to config file
        21. Use admin group setup from previous testcases, but delete ACI from that
        22. Try to change user's password by admin user
        23. Restore ACI
        24. Edit ACIs for admin group
        25. Delete a user from password admin group
        26. Change users password by ex-admin user
        27. Remove group from password admin configuration
        28. Change admins
        29. Change user's password by ex-admin user
        30. Change admin user's password by ex-admin user
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Fail(ldap.INSUFFICIENT_ACCESS)
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
        17. Success
        18. Success
        19. Fail
        20. Fail
        21. Success
        22. Success
        23. Success
        24. Success
        25. Success
        26. Success
        27. Success
        28. Success
        29. Fail
        30. Fail
    """
    # create unique members of admin group
    admin_grp = UniqueGroups(topo.standalone, DEFAULT_SUFFIX).create(properties={
        'cn': 'pwadm_group_adm',
        'description': 'pwadm_group_adm',
        'uniqueMember': [f'uid=pwadm_admin_2,ou=People,{DEFAULT_SUFFIX}',
                         f'uid=pwadm_admin_3,ou=People,{DEFAULT_SUFFIX}']
    })
    # Edit ACIs for admin group
    Domain(topo.standalone,
           f"ou=People,{DEFAULT_SUFFIX}").set('aci', f'(targetattr ="userpassword")'
                                                     f'(version 3.0;acl "Allow passwords admin to write user '
                                                     f'passwords";allow (write)(groupdn = "ldap:///{admin_grp.dn}");)')
    # Add group as password admin
    Config(topo.standalone).replace('passwordAdminDN', admin_grp.dn)
    # Test password admin group to modify password of another admin user
    change_password_of_user(topo, [
        ('uid=pwadm_admin_2,ou=People', 'Secret123', 'hello')],
                            f'uid=pwadm_admin_3,ou=people,{DEFAULT_SUFFIX}')
    # Use admin user to perform a password update on Directory Manager user
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        change_password_of_user(topo, [('uid=pwadm_admin_2,ou=People', 'Secret123', 'hello')],
                                f'{DN_DM},{DEFAULT_SUFFIX}')
    # Add top level container
    ou = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX).create(properties={'ou': 'pwadm_locpol'})
    # Change user's password by admin user. Break the global policy rule
    # Add new user in password admin group
    user = _create_user(topo, 'pwadm_locpol_user', 'ou=pwadm_locpol')
    user.replace('userpassword', 'Secret123')
    # Create local policy configuration entry
    _create_pwp(topo, ou.dn)
    # Set parameter for pwp
    for para_meter, op_op in [
        ('passwordLockout', 'on'),
        ('passwordMaxFailure', '4'),
        ('passwordLockoutDuration', '10'),
        ('passwordResetFailureCount', '100'),
        ('passwordMinLength', '8'),
        ('passwordAdminDN', f'cn=pwadm_group_adm,ou=Groups,{DEFAULT_SUFFIX}')]:
        change_pwp_parameter(topo, 'ou=pwadm_locpol', para_meter, op_op)
    # Set ACI
    OrganizationalUnit(topo.standalone,
                       ou.dn).set('aci',
                                  f'(targetattr ="userpassword")'
                                  f'(version 3.0;acl "Allow passwords admin to write user '
                                  f'passwords";allow (write)'
                                  f'(groupdn = "ldap:///cn=pwadm_group_adm,ou=Groups,{DEFAULT_SUFFIX}");)')
    # Change password with new admin
    change_password_of_user(topo, [('uid=pwadm_admin_2,ou=People', 'Secret123', 'Sec')], user.dn)
    # Set global parameter
    Config(topo.standalone).replace_many(
        ('passwordTrackUpdateTime', 'on'),
        ('passwordGraceLimit', '4'),
        ('passwordHistory', 'on'),
        ('passwordInHistory', '4'))
    # Test password admin group for global password policy
    change_password_of_user(topo, [('uid=pwadm_admin_2,ou=People', 'Secret123', 'Sec')],
                            f'uid=pwadm_user_2,ou=People,{DEFAULT_SUFFIX}')
    # Adding admin group for local policy
    grp = UniqueGroup(topo.standalone, f'cn=pwadm_group_adm,ou=Groups,{DEFAULT_SUFFIX}')
    grp.add('uniqueMember', f'uid=pwadm_admin_4,ou=People,{DEFAULT_SUFFIX}')
    # Modify ordinary user's password
    change_password_of_user(topo, [('uid=pwadm_admin_4,ou=People', 'Secret123', 'Secret')],
                            f'uid=pwadm_user_2,ou=People,{DEFAULT_SUFFIX}')
    # Modify user DN using modrdn of a user in password admin group
    UserAccount(topo.standalone, f'uid=pwadm_admin_4,ou=People,{DEFAULT_SUFFIX}').rename('uid=pwadm_admin_4_new')
    # Remove admin
    grp.remove('uniqueMember', f'uid=pwadm_admin_4,ou=People,{DEFAULT_SUFFIX}')
    # Add Admin
    grp.add('uniqueMember', f'uid=pwadm_admin_4_new,ou=People,{DEFAULT_SUFFIX}')
    # Test the group pwp again
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        change_password_of_user(topo, [(f'uid=pwadm_admin_4,ou=People', 'Secret123', 'Secret1')],
                                f'uid=pwadm_user_2,ou=People,{DEFAULT_SUFFIX}')
    change_password_of_user(topo, [(f'uid=pwadm_admin_4_new,ou=People', 'Secret123', 'Secret1')],
                            f'uid=pwadm_user_2,ou=People,{DEFAULT_SUFFIX}')
    with pytest.raises(ldap.INVALID_SYNTAX):
        Config(topo.standalone).replace('passwordAdminDN', "Invalid")
    # Test assigning invalid value to password admin attribute
    # Try to add more than one Password Admin attribute to config file
    with pytest.raises(ldap.OBJECT_CLASS_VIOLATION):
        Config(topo.standalone).replace('passwordAdminDN',
                                        [f'uid=pwadm_admin_2,ou=people,{DEFAULT_SUFFIX}',
                                         f'uid=pwadm_admin_3,ou=people,{DEFAULT_SUFFIX}'])
    # Use admin group setup from previous, but delete ACI from that
    people = Domain(topo.standalone, f"ou=People,{DEFAULT_SUFFIX}")
    people.remove('aci',
                  f'(targetattr ="userpassword")(version 3.0;acl '
                  f'"Allow passwords admin to write user '
                  f'passwords";allow (write)'
                  f'(groupdn = "ldap:///cn=pwadm_group_adm,ou=Groups,{DEFAULT_SUFFIX}");)')
    # Try to change user's password by admin user
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        change_password_of_user(topo, [('uid=pwadm_admin_2,ou=People', 'Secret123', 'Sec')],
                                f'uid=pwadm_user_2,ou=People,{DEFAULT_SUFFIX}')
    # Restore ACI
    people.set('aci',
               f'(targetattr ="userpassword")(version 3.0;acl '
               f'"Allow passwords admin to write user '
               f'passwords";allow (write)(groupdn = "ldap:///cn=pwadm_group_adm,ou=Groups,{DEFAULT_SUFFIX}");)')
    # Edit ACIs for admin group
    people.add('aci',
               f'(targetattr ="userpassword")(version 3.0;acl '
               f'"Allow passwords admin to add user '
               f'passwords";allow (add)(groupdn = "ldap:///cn=pwadm_group_adm,ou=Groups,{DEFAULT_SUFFIX}");)')
    UserAccount(topo.standalone, f'uid=pwadm_user_2,ou=people,{DEFAULT_SUFFIX}').replace('userpassword', 'Secret')
    real_user = UserAccount(topo.standalone, f'uid=pwadm_user_2,ou=people,{DEFAULT_SUFFIX}')
    conn = real_user.bind('Secret')
    # Test new aci
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        UserAccounts(conn, DEFAULT_SUFFIX, rdn='ou=People').create(properties={
            'uid': 'ok',
            'cn': 'ok',
            'sn': 'ok',
            'uidNumber': '1000',
            'gidNumber': 'ok',
            'homeDirectory': '/home/ok'})
    UserAccounts(topo.standalone, DEFAULT_SUFFIX).list()
    real_user = UserAccount(topo.standalone, f'uid=pwadm_admin_2,ou=People,{DEFAULT_SUFFIX}')
    conn = real_user.bind('Secret123')
    # Test new aci which has new rights
    for uid, cn, password in [
        ('pwadm_user_3', 'pwadm_user_1', 'U2VjcmV0MTIzCg=='),
        ('pwadm_user_4', 'pwadm_user_2', 'U2VjcmV0MTIzCg==')]:
        UserAccounts(conn, DEFAULT_SUFFIX, rdn='ou=People').create(properties={
            'uid': uid,
            'cn': cn,
            'sn': cn,
            'uidNumber': '1000',
            'gidNumber': '1001',
            'homeDirectory': f'/home/{uid}',
            'userpassword': password})
    # Remove ACI
    Domain(topo.standalone,
           f"ou=People,{DEFAULT_SUFFIX}").remove('aci',
                                                 f'(targetattr ="userpassword")'
                                                 f'(version 3.0;acl '
                                                 f'"Allow passwords admin to add user '
                                                 f'passwords";allow '
                                                 f'(add)(groupdn = '
                                                 f'"ldap:///cn=pwadm_group_adm,ou=Groups,{DEFAULT_SUFFIX}");)')
    # Delete a user from password admin group
    grp = UniqueGroup(topo.standalone, f'cn=pwadm_group_adm,ou=Groups,{DEFAULT_SUFFIX}')
    grp.remove('uniqueMember', f'uid=pwadm_admin_2,ou=People,{DEFAULT_SUFFIX}')
    # Change users password by ex-admin user
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        change_password_of_user(topo, [('uid=pwadm_admin_2,ou=People', 'Secret123', 'Secret')],
                                f'uid=pwadm_user_2,ou=People,{DEFAULT_SUFFIX}')
    # Set aci for only user
    people = Domain(topo.standalone, f"ou=People,{DEFAULT_SUFFIX}")
    people.remove('aci',
                  f'(targetattr ="userpassword")(version 3.0;acl '
                  f'"Allow passwords admin to write user '
                  f'passwords";allow (write)(groupdn = "ldap:///cn=pwadm_group_adm,ou=Groups,{DEFAULT_SUFFIX}");)')
    people.set('aci',
               f'(targetattr ="userpassword")(version 3.0;acl "Allow passwords admin '
               f'to write user passwords";allow (write)(groupdn = "ldap:///uid=pwadm_admin_1,{DEFAULT_SUFFIX}");)')
    # Remove group from password admin configuration
    Config(topo.standalone).replace('passwordAdminDN', f"uid=pwadm_admin_1,{DEFAULT_SUFFIX}")
    # Change user's password by ex-admin user
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        change_password_of_user(topo, [('uid=pwadm_admin_2,ou=People', 'Secret123', 'hellso')],
                                f'uid=pwadm_user_2,ou=People,{DEFAULT_SUFFIX}')
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        change_password_of_user(topo, [('uid=pwadm_admin_2,ou=People', 'Secret123', 'hellso')],
                                f'uid=pwadm_admin_1,{DEFAULT_SUFFIX}')


@pytest.mark.bz834060
def test_password_max_failure_should_lockout_password(topo):
    """Regression test for bz834060.

    :id: f2064efa-52d9-11ea-8037-8c16451d917b
    :setup: Standalone
    :steps:
        1. passwordMaxFailure should lockout password one sooner
        2. Setting passwordLockout to \"on\"
        3. Set maximum number of login tries to 3
        4. Turn off passwordLegacyPolicy
        5. Turn off local password policy, so that global is applied
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    config = Config(topo.standalone)
    config.replace_many(
        ('passwordLockout', 'on'),
        ('passwordMaxFailure', '3'),
        ('passwordLegacyPolicy', 'off'),
        ('nsslapd-pwpolicy-local', 'off'))
    user = _create_user(topo, 'tuser', 'ou=people')
    user.replace('userpassword', 'password')
    for _ in range(2):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            user.bind('Invalid')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user.bind("Invalid")
    config.replace('nsslapd-pwpolicy-local', 'on')


@pytest.mark.bz834063
def test_pwd_update_time_attribute(topo):
    """Regression test for bz834063

    :id: ec2b1d4e-52d9-11ea-b13e-8c16451d917b
    :setup: Standalone
    :steps:
        1. Add the attribute passwordTrackUpdateTime to cn=config
        2. Add a test entry while passwordTrackUpdateTime is on
        3. Check if new attribute pwdUpdateTime added automatically after changing the pwd
        4. Modify User pwd
        5. check for the pwdupdatetime attribute added to the test entry as passwordTrackUpdateTime is on
        6. Set passwordTrackUpdateTime to OFF and modify test entry's pwd
        7. Check passwordUpdateTime should not be changed
        8. Record last pwdUpdateTime before changing the password
        9. Modify Pwd
        10. Set passwordTrackUpdateTime to ON and modify test entry's pwd,
            check passwordUpdateTime should be changed
        11. Try setting Invalid value for passwordTrackUpdateTime
        12. Try setting Invalid value for pwdupdatetime
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
        11. Fail
        12. Fail
    """
    config = Config(topo.standalone)
    # Add the attribute passwordTrackUpdateTime to cn=config
    config.replace('passwordTrackUpdateTime', 'on')
    # Add a test entry while passwordTrackUpdateTime is on
    user = _create_user(topo, 'test_bz834063', None)
    user.set('userpassword', 'Unknown')
    # Modify User pwd
    user.replace('userpassword', 'Unknown1')
    # Check if new attribute pwdUpdateTime added automatically after changing the pwd
    assert user.get_attr_val_utf8('pwdUpdateTime')
    # Set passwordTrackUpdateTime to OFF and modify test entry's pwd
    config.replace('passwordTrackUpdateTime', 'off')
    # Record last pwdUpdateTime before changing the password
    update_time = user.get_attr_val_utf8('pwdUpdateTime')
    time.sleep(1)
    user.replace('userpassword', 'Unknown')
    # Check passwordUpdateTime should not be changed
    update_time_again = user.get_attr_val_utf8('pwdUpdateTime')
    assert update_time == update_time_again
    # Set passwordTrackUpdateTime to ON and modify test entry's pwd,
    # check passwordUpdateTime should be changed
    time.sleep(1)
    config.replace('passwordTrackUpdateTime', 'on')
    user.replace('userpassword', 'Unknown')
    time.sleep(1)
    update_time_1 = user.get_attr_val_utf8('pwdUpdateTime')
    assert update_time_again != update_time_1
    with pytest.raises(ldap.OPERATIONS_ERROR):
        config.replace('passwordTrackUpdateTime', "invalid")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        config.replace('pwdupdatetime', 'Invalid')


def test_password_track_update_time(topo):
    """passwordTrackUpdateTime stops working with subtree password policies

    :id: e5d3e4c6-52d9-11ea-a65e-8c16451d917b
    :setup: Standalone
    :steps:
        1. Add users
        2. Create local policy configuration entry for subsuffix
        3. Enable passwordTrackUpdateTime to local policy configuration entry
        4. Check that attribute passwordUpdate was added to entries
        5. check for the pwdupdatetime attribute added to the test entry as passwordTrackUpdateTime is on
        6. Set passwordTrackUpdateTime to OFF and modify test entry's pwd,
           check passwordUpdateTime should not be changed
        7. Record last pwdUpdateTime before changing the password
        8. Modify Pwd
        9. Check current pwdUpdateTime
        10. Set passwordTrackUpdateTime to ON and modify test entry's pwd,
            check passwordUpdateTime should be changed
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
    """
    # Add users
    user1 = _create_user(topo, 'trac478_user1', None)
    user2 = _create_user(topo, 'trac478_user2', None)
    # Create local policy configuration entry for subsuffix
    pwp_for_sufix = _create_pwp(topo, DEFAULT_SUFFIX)
    pwp_for_user2 = _create_pwp(topo, user2.dn)
    # Enable passwordTrackUpdateTime to local policy configuration entry
    for instance in [pwp_for_user2, pwp_for_sufix]:
        instance.replace('passwordTrackUpdateTime', 'on')
    # Check that attribute passwordUpdate was added to entries
    # check for the pwdupdatetime attribute added to the test entry as passwordTrackUpdateTime is on
    for user in [user1, user2]:
        user.replace('userpassword', 'pwd')
        time.sleep(1)
        assert user.get_attr_val_utf8('pwdUpdateTime')
    # Set passwordTrackUpdateTime to OFF and modify test entry's pwd,
    # check passwordUpdateTime should not be changed
    pwp_for_sufix.replace('passwordTrackUpdateTime', 'off')
    # Record last pwdUpdateTime before changing the password
    last_login_time_user1 = user1.get_attr_val_utf8('pwdUpdateTime')
    last_login_time_user2 = user2.get_attr_val_utf8('pwdUpdateTime')
    time.sleep(1)
    # Modify Pwd
    user1.replace('userpassword', 'pwd1')
    # Check current pwdUpdateTime
    last_login_time_user1_last = user1.get_attr_val_utf8('pwdUpdateTime')
    assert last_login_time_user1 == last_login_time_user1_last
    # Set passwordTrackUpdateTime to ON and modify test entry's pwd,
    #  check passwordUpdateTime should be changed
    pwp_for_user2.replace('passwordTrackUpdateTime', 'off')
    time.sleep(1)
    user2.replace('userpassword', 'pwd1')
    last_login_time_user2_last = user2.get_attr_val_utf8('pwdUpdateTime')
    assert last_login_time_user1 == last_login_time_user1_last
    assert last_login_time_user2 == last_login_time_user2_last
    pwp_for_sufix.replace('passwordTrackUpdateTime', 'on')
    user1.replace('userpassword', 'pwd1')
    time.sleep(1)
    last_login_time_user1_last = user1.get_attr_val_utf8('pwdUpdateTime')
    assert last_login_time_user1 != last_login_time_user1_last
    pwp_for_user2.replace('passwordTrackUpdateTime', 'on')
    time.sleep(1)
    user2.replace('userpassword', 'pwd1')
    time.sleep(1)
    last_login_time_user2_last = user2.get_attr_val_utf8('pwdUpdateTime')
    assert last_login_time_user2 != last_login_time_user2_last


@pytest.mark.bz834063
def test_signal_11(topo):
    """ns-slapd instance crashed with signal 11 SIGSEGV

    :id: d757b9ae-52d9-11ea-802f-8c16451d917b
    :setup: Standalone
    :steps:
        1. Adding new user
        2. Modifying user passwod of uid=bz973583
    :expected results:
        1. Success
        2. Success
    """
    user = _create_user(topo, 'bz973583', None)
    user.set('userpassword', 'Secret123')
    user.remove('userpassword', 'Secret123')
    user.set('userpassword', 'new')
    assert topo.standalone.status()


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
