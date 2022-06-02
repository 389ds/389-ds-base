# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


"""
This file contains the test for password storage scheme
"""

import os
import subprocess
import shutil
import pytest

from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts, UserAccount
from lib389._constants import DEFAULT_SUFFIX
from lib389.config import Config
from lib389.password_plugins import PBKDF2Plugin, SSHA512Plugin
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1


def user_config(topo, field_value):
    """
    Will set storage schema and create user.
    """
    Config(topo.standalone).replace("passwordStorageScheme", field_value)
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX).create_test_user()
    user.set('userpassword', 'ItsMeAnuj')
    return user


LIST_FOR_PARAMETERIZATION = ["CRYPT", "SHA", "SSHA", "SHA256", "SSHA256",
                             "SHA384", "SSHA384", "SHA512", "SSHA512", "MD5", "PBKDF2_SHA256"]


@pytest.mark.parametrize("value", LIST_FOR_PARAMETERIZATION, ids=LIST_FOR_PARAMETERIZATION)
def test_check_password_scheme(topo, value):
    """Check all password scheme.

    :id: 196bccfc-33a6-11ea-a2a5-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Change password scheme and create user with password.
        2. check password scheme is set .
        3. Delete user
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
    """
    user = user_config(topo, value)
    assert '{' + f'{value.lower()}' + '}' in \
           UserAccount(topo.standalone, user.dn).get_attr_val_utf8('userpassword').lower()
    user.delete()


def test_clear_scheme(topo):
    """Check clear password scheme.

    :id: 2420aadc-33a6-11ea-b59a-8c16451d917b
    :setup: Standalone
    :steps:
        1. Change password scheme and create user with password.
        2. check password scheme is set .
        3. Delete user
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
    """
    user = user_config(topo, "CLEAR")
    assert "ItsMeAnuj" in UserAccount(topo.standalone, user.dn).get_attr_val_utf8('userpassword')
    user.delete()


def test_check_two_scheme(topo):
    """Check password scheme SHA and CRYPT

    :id: 2b677f1e-33a6-11ea-a371-8c16451d917b
    :setup: Standalone
    :steps:
        1. Change password scheme and create user with password.
        2. check password scheme is set .
        3. Delete user
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
    """
    for schema, value in [("nsslapd-rootpwstoragescheme", "SHA"),
                          ("passwordStorageScheme", "CRYPT")]:
        Config(topo.standalone).replace(schema, value)
    topo.standalone.restart()
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX).create_test_user()
    user.set('userpassword', 'ItsMeAnuj')
    assert '{' + f'{"CRYPT".lower()}' + '}' \
           in UserAccount(topo.standalone, user.dn).get_attr_val_utf8('userpassword').lower()
    user.delete()

@pytest.mark.skipif(ds_is_older('1.4'), reason="Not implemented")
def test_check_pbkdf2_sha256(topo):
    """Check password scheme PBKDF2_SHA256.

    :id: 31612e7e-33a6-11ea-a750-8c16451d917b
    :setup: Standalone
    :steps:
        1. Try to delete PBKDF2_SHA256.
        2. Should not deleted PBKDF2_SHA256 and server should up.
    :expectedresults:
        1. Pass
        2. Pass
    """
    value = 'PBKDF2_SHA256'
    user = user_config(topo, value)
    assert '{' + f'{value.lower()}' + '}' in \
           UserAccount(topo.standalone, user.dn).get_attr_val_utf8('userpassword').lower()
    plg = PBKDF2Plugin(topo.standalone)
    plg._protected = False
    plg.delete()
    topo.standalone.restart()
    assert Config(topo.standalone).get_attr_val_utf8('passwordStorageScheme') == 'PBKDF2_SHA256'
    assert topo.standalone.status()
    user.delete()


def test_check_ssha512(topo):
    """Check password scheme SSHA512.

    :id: 9db023d2-33a1-11ea-b68c-8c16451d917b
    :setup: Standalone
    :steps:
        1. Try to delete SSHA512Plugin.
        2. Should deleted SSHA512Plugin and server should not up.
        3. Restore dse file to recover
    :expectedresults:
        1. Pass
        2. Pass
        3. Pass
    """
    value = 'SSHA512'
    config_dir = topo.standalone.get_config_dir()
    user = user_config(topo, value)
    assert '{' + f'{value.lower()}' + '}' in \
           UserAccount(topo.standalone, user.dn).get_attr_val_utf8('userpassword').lower()
    plg = SSHA512Plugin(topo.standalone)
    plg._protected = False
    plg.delete()
    with pytest.raises(subprocess.CalledProcessError):
        topo.standalone.restart()
    shutil.copy(config_dir + '/dse.ldif.startOK', config_dir + '/dse.ldif')
    topo.standalone.restart()
    user.delete()


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
