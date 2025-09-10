# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import os
import ldap
import logging
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.group import Group, Groups

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope='function')
def group1(topology_st, request):
    groupname = 'group1'
    log.info('Create group1')
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    if groups.exists(groupname):
        groups.get(groupname).delete()

    group1 = groups.create(properties={'cn': groupname})

    def fin():
        if group1.exists():
            log.info('Delete group1')
            group1.delete()

    request.addfinalizer(fin)

    return group1


@pytest.fixture(scope='function')
def group2(topology_st, request):
    groupname = 'group2'
    log.info('Create group2')
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    if groups.exists(groupname):
        groups.get(groupname).delete()

    group2 = groups.create(properties={'cn': groupname})

    def fin():
        if group2.exists():
            log.info('Delete group2')
            group2.delete()

    request.addfinalizer(fin)

    return group2


@pytest.fixture(scope='function')
def group3(topology_st, request):
    groupname = 'group3'
    log.info('Create group3')
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    if groups.exists(groupname):
        groups.get(groupname).delete()

    group3 = groups.create(properties={'cn': groupname})

    def fin():
        if group3.exists():
            log.info('Delete group3')
            group3.delete()

    request.addfinalizer(fin)

    return group3


@pytest.fixture(scope='function')
def supergroup(topology_st, request):
    groupname = 'supergroup'
    log.info('Create supergroup')
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    if groups.exists(groupname):
        groups.get(groupname).delete()

    supergroup = groups.create(properties={'cn': groupname})

    def fin():
        if supergroup.exists():
            log.info('Delete supergroup')
            supergroup.delete()

    request.addfinalizer(fin)

    return supergroup


@pytest.fixture(scope='function')
def user1(topology_st, request):
    username = 'user1'
    log.info('Create user1')
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    if users.exists(username):
        users.get(username).delete()

    user1 = users.create(properties={'cn': username,
                                     'uid': username,
                                     'sn': username,
                                     'uidNumber': '1',
                                     'gidNumber': '11',
                                     'homeDirectory': f'/home/{username}'})

    def fin():
        if user1.exists():
            log.info('Delete user1')
            user1.delete()

    request.addfinalizer(fin)

    return user1
