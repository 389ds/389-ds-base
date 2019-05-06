# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, PLUGIN_MEMBER_OF

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket47963(topology_st):
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
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'memberofskipnested', b'on')])
    except ldap.LDAPError as e:
        log.error('test_automember: Failed to modify config entry: error ' + e.args[0]['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)

    #
    # Add our groups, users, memberships, etc
    #
    try:
        topology_st.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'test_user'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add teset user: error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((GROUP_DN1, {
            'objectclass': 'top groupOfNames groupOfUniqueNames extensibleObject'.split(),
            'cn': 'group1',
            'member': USER_DN
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add group1: error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((GROUP_DN2, {
            'objectclass': 'top groupOfNames groupOfUniqueNames extensibleObject'.split(),
            'cn': 'group2',
            'member': USER_DN
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add group2: error ' + e.args[0]['desc'])
        assert False

    # Add group with no member(yet)
    try:
        topology_st.standalone.add_s(Entry((GROUP_DN3, {
            'objectclass': 'top groupOfNames groupOfUniqueNames extensibleObject'.split(),
            'cn': 'group'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add group3: error ' + e.args[0]['desc'])
        assert False
    time.sleep(1)

    #
    # Test we have the correct memberOf values in the user entry
    #
    try:
        member_filter = ('(&(memberOf=' + GROUP_DN1 + ')(memberOf=' + GROUP_DN2 + '))')
        entries = topology_st.standalone.search_s(USER_DN, ldap.SCOPE_BASE, member_filter)
        if not entries:
            log.fatal('User is missing expected memberOf attrs')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Search for user1 failed: ' + e.args[0]['desc'])
        assert False

    # Add the user to the group
    try:
        topology_st.standalone.modify_s(GROUP_DN3, [(ldap.MOD_ADD, 'member', ensure_bytes(USER_DN))])
    except ldap.LDAPError as e:
        log.error('Failed to member to group: error ' + e.args[0]['desc'])
        assert False
    time.sleep(1)

    # Check that the test user is a "memberOf" all three groups
    try:
        member_filter = ('(&(memberOf=' + GROUP_DN1 + ')(memberOf=' + GROUP_DN2 +
                         ')(memberOf=' + GROUP_DN3 + '))')
        entries = topology_st.standalone.search_s(USER_DN, ldap.SCOPE_BASE, member_filter)
        if not entries:
            log.fatal('User is missing expected memberOf attrs')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Search for user1 failed: ' + e.args[0]['desc'])
        assert False

    #
    # Delete group2, and check memberOf values in the user entry
    #
    try:
        topology_st.standalone.delete_s(GROUP_DN2)
    except ldap.LDAPError as e:
        log.error('Failed to delete test group2: ' + e.args[0]['desc'])
        assert False
    time.sleep(1)

    try:
        member_filter = ('(&(memberOf=' + GROUP_DN1 + ')(memberOf=' + GROUP_DN3 + '))')
        entries = topology_st.standalone.search_s(USER_DN, ldap.SCOPE_BASE, member_filter)
        if not entries:
            log.fatal('User incorrect memberOf attrs')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Search for user1 failed: ' + e.args[0]['desc'])
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
