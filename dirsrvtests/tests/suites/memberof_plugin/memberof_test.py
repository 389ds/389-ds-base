# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
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
from lib389.utils import *
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX
GROUP_DN = 'cn=group,' + DEFAULT_SUFFIX


def test_memberof_auto_add_oc(topology_st):
    """Test the auto add objectclass feature.  The plugin should add a predefined
    objectclass that will allow memberOf to be added to an entry.
    """

    # enable dynamic plugins
    try:
        topology_st.standalone.modify_s(DN_CONFIG,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-dynamic-plugins',
                                          'on')])
    except ldap.LDAPError as e:
        ldap.error('Failed to enable dynamic plugins! ' + e.message['desc'])
        assert False

    # Enable the plugin
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)

    # First test invalid value (config validation)
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    try:
        topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                        [(ldap.MOD_REPLACE,
                                          'memberofAutoAddOC',
                                          'invalid123')])
        log.fatal('Incorrectly added invalid objectclass!')
        assert False
    except ldap.UNWILLING_TO_PERFORM:
        log.info('Correctly rejected invalid objectclass')
    except ldap.LDAPError as e:
        ldap.error('Unexpected error adding invalid objectclass - error: ' + e.message['desc'])
        assert False

    # Add valid objectclass
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    try:
        topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                        [(ldap.MOD_REPLACE,
                                          'memberofAutoAddOC',
                                          'inetuser')])
    except ldap.LDAPError as e:
        log.fatal('Failed to configure memberOf plugin: error ' + e.message['desc'])
        assert False

    # Add two users
    try:
        topology_st.standalone.add_s(Entry((USER1_DN,
                                            {'objectclass': 'top',
                                             'objectclass': 'person',
                                             'objectclass': 'organizationalPerson',
                                             'objectclass': 'inetorgperson',
                                             'sn': 'last',
                                             'cn': 'full',
                                             'givenname': 'user1',
                                             'uid': 'user1'
                                             })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER2_DN,
                                            {'objectclass': 'top',
                                             'objectclass': 'person',
                                             'objectclass': 'organizationalPerson',
                                             'objectclass': 'inetorgperson',
                                             'sn': 'last',
                                             'cn': 'full',
                                             'givenname': 'user2',
                                             'uid': 'user2'
                                             })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add user2 entry, error: ' + e.message['desc'])
        assert False

    # Add a group(that already includes one user
    try:
        topology_st.standalone.add_s(Entry((GROUP_DN,
                                            {'objectclass': 'top',
                                             'objectclass': 'groupOfNames',
                                             'cn': 'group',
                                             'member': USER1_DN
                                             })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add group entry, error: ' + e.message['desc'])
        assert False

    # Add a user to the group
    try:
        topology_st.standalone.modify_s(GROUP_DN,
                                        [(ldap.MOD_ADD,
                                          'member',
                                          USER2_DN)])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user2 to group: error ' + e.message['desc'])
        assert False

    log.info('Test complete.')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
