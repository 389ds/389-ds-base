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
from lib389.pwpolicy import PwPolicyManager
from lib389.config import Config
from lib389.idm.domain import Domain
import time

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
    pwp1 = PwPolicyManager(topo.standalone)
    user = pwp1.get_pwpolicy_entry(f'{pwp},{DEFAULT_SUFFIX}')
    user.replace(operation, to_do)


def change_password_of_user(topo, user_password_new_pass_list, pass_to_change):
    """
    Will change password with self binding.
    """
    for user, password, new_pass in user_password_new_pass_list:
        real_user = UserAccount(topo.standalone, f'{user},{DEFAULT_SUFFIX}')
        conn = real_user.bind(password)
        UserAccount(conn, pass_to_change).replace('userpassword', new_pass)


@pytest.mark.bug1044164
def test_local_password_policy(topo):
    """Regression test for bug1044164 part 1.

    :id: d6f4a7fa-473b-11ea-8766-8c16451d917b
    :setup: Standalone
    :steps:
        1. Add a User as Password Admin
        2. Create a password admin user entry
        3. Add an aci to allow this user all rights
        4. Configure password admin
        5. Create local password policy and enable passwordmustchange
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    user = _create_user(topo, 'pwadm_admin_1', None)
    user.replace('userpassword', 'Secret123')
    Domain(topo.standalone, DEFAULT_SUFFIX).set("aci",
                                                f'(targetattr ="userpassword")(version 3.0;acl '
                                                f'"Allow password admin to write user '
                                                f'passwords";allow (write)(userdn = "ldap:///{user.dn}");)')
    Config(topo.standalone).replace_many(
        ('passwordAdminDN', user.dn),
        ('passwordMustChange', 'off'),
        ('nsslapd-pwpolicy-local', 'on'))


@pytest.mark.bug1044164
def test_admin_user_to_perform_password_update(topo):
    """Regression test for bug1044164 part 2.

    :id: 374fadc0-473c-11ea-9291-8c16451d917b
    :setup: Standalone
    :steps:
        1. Add another generic user but do not include the password (userpassword)
        2. Use admin user to perform a password update on generic user
        3. We don't need this ACI anymore. Delete it
    :expected results:
        1. Success
        2. Success
        3. Success
    """
    for uid, ou_ou in [('pwadm_user_1', None), ('pwadm_user_2', 'ou=People')]:
        _create_user(topo, uid, ou_ou)
    real_user = UserAccount(topo.standalone, f'uid=pwadm_admin_1,{DEFAULT_SUFFIX}')
    conn = real_user.bind('Secret123')
    UserAccount(conn, f'uid=pwadm_user_1,{DEFAULT_SUFFIX}').replace('userpassword', 'hello')
    Domain(topo.standalone, DEFAULT_SUFFIX).remove('aci',
                                                   '(targetattr ="userpassword")(version 3.0;acl '
                                                   '"Allow password admin to write user '
                                                   'passwords";allow (write)'
                                                   '(userdn = "ldap:///uid=pwadm_admin_1,dc=example,dc=com");)')


@pytest.mark.bug1118006
def test_passwordexpirationtime_attribute(topo):
    """Regression test for bug1118006.

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
    UserAccount(topo.standalone, f'uid=pwadm_user_1,{DEFAULT_SUFFIX}').replace('userpassword', 'Secret123')
    time.sleep(1)
    assert UserAccount(topo.standalone, f'uid=pwadm_user_1,{DEFAULT_SUFFIX}').get_attr_val_utf8('passwordExpirationTime') == epoch_date
    Config(topo.standalone).replace('passwordMustChange', 'off')
    time.sleep(1)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)