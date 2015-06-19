# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_ticket47963(topology):
    '''
    Test that the memberOf plugin works correctly after setting:

        memberofskipnested: on

    '''
    PLUGIN_DN = 'cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config'
    USER_DN = 'uid=test_user,' + DEFAULT_SUFFIX
    GROUP_DN1 = 'cn=group1,' + DEFAULT_SUFFIX
    GROUP_DN2 = 'cn=group2,' + DEFAULT_SUFFIX
    GROUP_DN3 = 'cn=group3,' + DEFAULT_SUFFIX

    #
    # Enable the plugin and configure the skiop nest attribute, then restart the server
    #
    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    try:
        topology.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'memberofskipnested', 'on')])
    except ldap.LDAPError, e:
        log.error('test_automember: Failed to modify config entry: error ' + e.message['desc'])
        assert False

    topology.standalone.restart(timeout=10)

    #
    # Add our groups, users, memberships, etc
    #
    try:
        topology.standalone.add_s(Entry((USER_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'test_user'
                          })))
    except ldap.LDAPError, e:
        log.error('Failed to add teset user: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((GROUP_DN1, {
                          'objectclass': 'top groupOfNames groupOfUniqueNames extensibleObject'.split(),
                          'cn': 'group1',
                          'member': USER_DN
                          })))
    except ldap.LDAPError, e:
        log.error('Failed to add group1: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((GROUP_DN2, {
                          'objectclass': 'top groupOfNames groupOfUniqueNames extensibleObject'.split(),
                          'cn': 'group2',
                          'member': USER_DN
                          })))
    except ldap.LDAPError, e:
        log.error('Failed to add group2: error ' + e.message['desc'])
        assert False

    # Add group with no member(yet)
    try:
        topology.standalone.add_s(Entry((GROUP_DN3, {
                          'objectclass': 'top groupOfNames groupOfUniqueNames extensibleObject'.split(),
                          'cn': 'group'
                          })))
    except ldap.LDAPError, e:
        log.error('Failed to add group3: error ' + e.message['desc'])
        assert False
    time.sleep(1)

    #
    # Test we have the correct memberOf values in the user entry
    #
    try:
        member_filter = ('(&(memberOf=' + GROUP_DN1 + ')(memberOf=' + GROUP_DN2 + '))')
        entries = topology.standalone.search_s(USER_DN, ldap.SCOPE_BASE, member_filter)
        if not entries:
            log.fatal('User is missing expected memberOf attrs')
            assert False
    except ldap.LDAPError, e:
        log.fatal('Search for user1 failed: ' + e.message['desc'])
        assert False

    # Add the user to the group
    try:
        topology.standalone.modify_s(GROUP_DN3, [(ldap.MOD_ADD, 'member', USER_DN)])
    except ldap.LDAPError, e:
        log.error('Failed to member to group: error ' + e.message['desc'])
        assert False
    time.sleep(1)

    # Check that the test user is a "memberOf" all three groups
    try:
        member_filter = ('(&(memberOf=' + GROUP_DN1 + ')(memberOf=' + GROUP_DN2 +
                        ')(memberOf=' + GROUP_DN3 + '))')
        entries = topology.standalone.search_s(USER_DN, ldap.SCOPE_BASE, member_filter)
        if not entries:
            log.fatal('User is missing expected memberOf attrs')
            assert False
    except ldap.LDAPError, e:
        log.fatal('Search for user1 failed: ' + e.message['desc'])
        assert False

    #
    # Delete group2, and check memberOf values in the user entry
    #
    try:
        topology.standalone.delete_s(GROUP_DN2)
    except ldap.LDAPError, e:
        log.error('Failed to delete test group2: ' + e.message['desc'])
        assert False
    time.sleep(1)

    try:
        member_filter = ('(&(memberOf=' + GROUP_DN1 + ')(memberOf=' + GROUP_DN3 + '))')
        entries = topology.standalone.search_s(USER_DN, ldap.SCOPE_BASE, member_filter)
        if not entries:
            log.fatal('User incorrect memberOf attrs')
            assert False
    except ldap.LDAPError, e:
        log.fatal('Search for user1 failed: ' + e.message['desc'])
        assert False

    log.info('Test complete')


def test_ticket47963_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_ticket47963(topo)
    test_ticket47963_final(topo)


if __name__ == '__main__':
    run_isolated()

