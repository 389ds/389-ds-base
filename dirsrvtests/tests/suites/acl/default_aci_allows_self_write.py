# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


import pytest
from lib389.idm.user import nsUserAccounts, UserAccounts
from lib389.topologies import topology_st as topology
from lib389.paths import Paths
from lib389.utils import ds_is_older
from lib389._constants import *

default_paths = Paths()

pytestmark = pytest.mark.tier1

USER_PASSWORD = "some test password"
NEW_USER_PASSWORD = "some new password"

@pytest.mark.skipif(ds_is_older('1.4.2.0'), reason="Default aci's in older versions do not support this functionality")
def test_acl_default_allow_self_write_nsuser(topology):
    """
    Testing nsusers can self write and self read. This it a sanity test
    so that our default entries have their aci's checked.

    :id: 4f0fb01a-36a6-430c-a2ee-ebeb036bd951

    :setup: Standalone instance

    :steps:
        1. Testing comparison of two different users.

    :expectedresults:
        1. Should fail to compare
    """
    topology.standalone.enable_tls()
    nsusers = nsUserAccounts(topology.standalone, DEFAULT_SUFFIX)
    # Create a user as dm.
    user = nsusers.create(properties={
        'uid': 'test_nsuser',
        'cn': 'test_nsuser',
        'displayName': 'testNsuser',
        'legalName': 'testNsuser',
        'uidNumber': '1001',
        'gidNumber': '1001',
        'homeDirectory': '/home/testnsuser',
        'userPassword': USER_PASSWORD,
    })
    # Create a new con and bind as the user.
    user_conn = user.bind(USER_PASSWORD)

    user_nsusers = nsUserAccounts(user_conn, DEFAULT_SUFFIX)
    self_ent = user_nsusers.get(dn=user.dn)

    # Can we self read x,y,z
    check = self_ent.get_attrs_vals_utf8([
        'uid',
        'cn',
        'displayName',
        'legalName',
        'uidNumber',
        'gidNumber',
        'homeDirectory',
    ])
    for k in check.values():
        # Could we read the values?
        assert(isinstance(k, list))
        assert(len(k) > 0)
    # Can we self change a,b,c
    self_ent.ensure_attr_state({
        'legalName': ['testNsuser_update'],
        'displayName': ['testNsuser_update'],
        'nsSshPublicKey': ['testkey'],
    })
    # self change pw
    self_ent.change_password(USER_PASSWORD, NEW_USER_PASSWORD)


@pytest.mark.skipif(ds_is_older('1.4.2.0'), reason="Default aci's in older versions do not support this functionality")
def test_acl_default_allow_self_write_user(topology):
    """
    Testing users can self write and self read. This it a sanity test
    so that our default entries have their aci's checked.

    :id: 4c52321b-f473-4c32-a1d5-489b138cd199

    :setup: Standalone instance

    :steps:
        1. Testing comparison of two different users.

    :expectedresults:
        1. Should fail to compare
    """
    topology.standalone.enable_tls()
    users = UserAccounts(topology.standalone, DEFAULT_SUFFIX)
    # Create a user as dm.
    user = users.create(properties={
        'uid': 'test_user',
        'cn': 'test_user',
        'sn': 'User',
        'uidNumber': '1002',
        'gidNumber': '1002',
        'homeDirectory': '/home/testuser',
        'userPassword': USER_PASSWORD,
    })
    # Create a new con and bind as the user.
    user_conn = user.bind(USER_PASSWORD)

    user_users = UserAccounts(user_conn, DEFAULT_SUFFIX)
    self_ent = user_users.get(dn=user.dn)
    # Can we self read x,y,z
    check = self_ent.get_attrs_vals_utf8([
        'uid',
        'cn',
        'sn',
        'uidNumber',
        'gidNumber',
        'homeDirectory',
    ])
    for (a, k) in check.items():
        print(a)
        # Could we read the values?
        assert(isinstance(k, list))
        assert(len(k) > 0)
    # Self change pw
    self_ent.change_password(USER_PASSWORD, NEW_USER_PASSWORD)


