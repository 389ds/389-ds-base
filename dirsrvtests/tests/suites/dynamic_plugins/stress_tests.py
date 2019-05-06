# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
'''
Created on Dec 16, 2014

@author: mreynolds
'''
import logging
import threading

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.properties import *
from lib389.plugins import ReferentialIntegrityPlugin, MemberOfPlugin
from lib389.utils import *
from lib389.idm.directorymanager import *

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

NUM_USERS = 250
GROUP_DN = 'cn=stress-group,' + DEFAULT_SUFFIX


# Configure Referential Integrity Plugin for stress test
def configureRI(inst):
    plugin = ReferentialIntegrityPlugin(inst)
    plugin.enable()
    plugin.replace('referint-membership-attr', 'uniquemember')


# Configure MemberOf Plugin for stress test
def configureMO(inst):
    plugin = MemberOfPlugin(inst)
    plugin.enable()
    plugin.replace('memberofgroupattr', 'uniquemember')


def cleanup(conn):
    try:
        conn.delete_s(GROUP_DN)
    except ldap.LDAPError as e:
        log.fatal('cleanup: failed to delete group (' + GROUP_DN + ') error: ' + e.message['desc'])
        assert False


class DelUsers(threading.Thread):
    def __init__(self, inst, rdnval):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.rdnval = rdnval

    def run(self):
        dm = DirectoryManager(self.inst)
        conn = dm.bind()
        idx = 0
        log.info('DelUsers - Deleting ' + str(NUM_USERS) + ' entries (' + self.rdnval + ')...')
        while idx < NUM_USERS:
            USER_DN = 'uid=' + self.rdnval + str(idx) + ',' + DEFAULT_SUFFIX
            try:
                conn.delete_s(USER_DN)
            except ldap.LDAPError as e:
                if e == ldap.UNAVAILABLE or e == ldap.SERVER_DOWN:
                    log.fatal('DeleteUsers: failed to delete (' + USER_DN + ') error: ' + e.message['desc'])
                    assert False

            idx += 1

        conn.close()
        log.info('DelUsers - Finished deleting ' + str(NUM_USERS) + ' entries (' + self.rdnval + ').')


class AddUsers(threading.Thread):
    def __init__(self, inst, rdnval, addToGroup):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.addToGroup = addToGroup
        self.rdnval = rdnval

    def run(self):
        # Start adding users
        dm = DirectoryManager(self.inst)
        conn = dm.bind()
        idx = 0

        if self.addToGroup:
            try:
                conn.add_s(Entry((GROUP_DN,
                                  {'objectclass': b'top groupOfNames groupOfUniqueNames'.split(),
                                   'cn': 'stress-group'})))
            except ldap.LDAPError as e:
                if e == ldap.UNAVAILABLE or e == ldap.SERVER_DOWN:
                    log.fatal('AddUsers: failed to add group (' + GROUP_DN + ') error: ' + e.message['desc'])
                    assert False

        log.info('AddUsers - Adding ' + str(NUM_USERS) + ' entries (' + self.rdnval + ')...')

        while idx < NUM_USERS:
            USER_DN = 'uid=' + self.rdnval + str(idx) + ',' + DEFAULT_SUFFIX
            try:
                conn.add_s(Entry((USER_DN, {'objectclass': b'top nsOrgPerson'.split(),
                                            'uid': ensure_bytes('user' + str(idx))})))
            except ldap.LDAPError as e:
                if e == ldap.UNAVAILABLE or e == ldap.SERVER_DOWN:
                    log.fatal('AddUsers: failed to add (' + USER_DN + ') error: ' + e.message['desc'])
                    assert False

            if self.addToGroup:
                # Add the user to the group
                try:
                    conn.modify_s(GROUP_DN, [(ldap.MOD_ADD, 'uniquemember', ensure_bytes(USER_DN))])
                except ldap.LDAPError as e:
                    if e == ldap.UNAVAILABLE or e == ldap.SERVER_DOWN:
                        log.fatal('AddUsers: Failed to add user' + USER_DN + ' to group: error ' + e.message['desc'])
                        assert False

            idx += 1

        conn.close()
        log.info('AddUsers - Finished adding ' + str(NUM_USERS) + ' entries (' + self.rdnval + ').')
