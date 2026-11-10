# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import pytest
from lib389._constants import DEFAULT_SUFFIX
from test389.topologies import topology_st as topo
from lib389.backend import Backends
from lib389.plugins import MemberOfPlugin
from lib389.idm.group import Groups
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

USERS_SUFFIX = 'ou=users,dc=example,dc=com'


def test_memberof_multi_backend(topo):
    """Test memberOf plugin correctly manages attributes across multiple
    backends when memberOfAllBackends is enabled

    :id: 8b2b6b4a-6b1c-4c1d-9c1d-1c1d1c1d1c1d
    :setup: Standalone Instance
    :steps:
        1. Enable memberOf plugin with memberOfAllBackends
        2. Create additional backend and users in it
        3. Create group in default backend with members from the new backend
        4. Verify memberOf attribute is added to users in the other backend
        5. Remove members and verify memberOf is cleaned up
    :expectedresults:
        1. Plugin configured successfully
        2. Backend and users created
        3. Group created with cross-backend members
        4. memberOf attribute correctly set across backends
        5. memberOf attribute removed when members are removed
    """
    inst = topo.standalone

    log.info('Configuring memberOf plugin with memberOfAllBackends')
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.replace('memberOfAllBackends', 'on')
    memberof.replace('memberOfAutoAddOC', 'nsMemberOf')
    inst.restart()

    log.info('Creating additional backend for users')
    backends = Backends(inst)
    backends.create(properties={
        'nsslapd-suffix': USERS_SUFFIX,
        'cn': 'users'
    })

    log.info('Creating suffix entry and People container for users backend')
    OrganizationalUnits(inst, DEFAULT_SUFFIX).create(properties={'ou': 'users'})
    OrganizationalUnits(inst, USERS_SUFFIX).create(properties={'ou': 'People'})

    log.info('Creating test users in users backend')
    users = UserAccounts(inst, USERS_SUFFIX)
    user1 = users.create(properties={
        'uid': 'user1',
        'cn': 'user1',
        'sn': 'User1',
        'uidNumber': '1001',
        'gidNumber': '2001',
        'homeDirectory': '/home/user1'
    })
    user2 = users.create(properties={
        'uid': 'user2',
        'cn': 'user2',
        'sn': 'User2',
        'uidNumber': '1002',
        'gidNumber': '2002',
        'homeDirectory': '/home/user2'
    })

    log.info('Creating group in default backend')
    groups = Groups(inst, DEFAULT_SUFFIX)
    test_group = groups.create(properties={
        'cn': 'crossbackend',
        'member': user1.dn
    })

    log.info('Adding second member from users backend')
    test_group.add('member', user2.dn)

    log.info('Verifying group has correct members')
    members = test_group.get_attr_vals_utf8('member')
    assert user1.dn in members, f'{user1.dn} not in group members'
    assert user2.dn in members, f'{user2.dn} not in group members'

    log.info('Verifying memberOf attribute on users in different backend')
    assert test_group.dn in user1.get_attr_vals_utf8('memberOf'), \
        f'memberOf not set on {user1.dn}'
    assert test_group.dn in user2.get_attr_vals_utf8('memberOf'), \
        f'memberOf not set on {user2.dn}'

    log.info('Removing members from group')
    test_group.remove('member', user1.dn)
    test_group.remove('member', user2.dn)

    log.info('Verifying memberOf attribute removed from users')
    assert not user1.get_attr_vals_utf8('memberOf'), \
        f'memberOf still present on {user1.dn}'
    assert not user2.get_attr_vals_utf8('memberOf'), \
        f'memberOf still present on {user2.dn}'


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
