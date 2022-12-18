# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import logging
import pytest
import os
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.cli_base import FakeArgs
from lib389.cli_idm.account import bulk_update
from lib389.idm.user import UserAccounts

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def create_test_users(topo):
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    u_range = list(range(5))
    for idx in u_range:
        try:
            users.create(properties={
                'uid': f'testuser{idx}',
                'cn': f'testuser{idx}',
                'sn': f'user{idx}',
                'uidNumber': f'{1000 + idx}',
                'gidNumber': f'{1000 + idx}',
                'homeDirectory': f'/home/testuser{idx}'
            })
        except ldap.ALREADY_EXISTS:
            pass


def test_bulk_operations(topo, create_test_users):
    """Testing adding, replacing, an removing attribute/values to a bulk set
    of users

    :id: c89ff057-2f44-4070-8d42-850257025b2b
    :setup: Standalone Instance
    :steps:
        1. Bulk add attribute
        2. Bulk replace attribute
        3. Bulk delete attribute
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    inst = topo.standalone

    args = FakeArgs()
    args.json = False
    args.basedn = DEFAULT_SUFFIX
    args.scope = ldap.SCOPE_SUBTREE
    args.filter = "(uid=*)"
    args.stop = False
    args.changes = []

    # Test ADD
    args.changes = ['add:objectclass:extensibleObject']
    bulk_update(inst, DEFAULT_SUFFIX, log, args)
    users = UserAccounts(inst, DEFAULT_SUFFIX).list()
    for user in users:
        assert 'extensibleobject' in user.get_attr_vals_utf8_l('objectclass')

    # Test REPLACE
    args.changes = ['replace:cn:hello_new_cn']
    bulk_update(inst, DEFAULT_SUFFIX, log, args)
    users = UserAccounts(inst, DEFAULT_SUFFIX).list()
    for user in users:
        assert user.get_attr_val_utf8_l('cn') == "hello_new_cn"

    # Test DELETE
    args.changes = ['delete:objectclass:extensibleObject']
    bulk_update(inst, DEFAULT_SUFFIX, log, args)
    users = UserAccounts(inst, DEFAULT_SUFFIX).list()
    for user in users:
        assert 'extensibleobject' not in user.get_attr_vals_utf8_l('objectclass')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
