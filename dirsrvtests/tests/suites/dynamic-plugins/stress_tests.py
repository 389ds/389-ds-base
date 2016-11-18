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
import os
import sys
import time
import ldap
import logging
import pytest
import threading
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *

log = logging.getLogger(__name__)

NUM_USERS = 250
GROUP_DN = 'cn=stress-group,' + DEFAULT_SUFFIX


def openConnection(inst):
    # Open a new connection to our LDAP server
    server = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    server.allocate(args_standalone)
    server.open()

    return server


# Configure Referential Integrity Plugin for stress test
def configureRI(inst):
    inst.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
    PLUGIN_DN = 'cn=' + PLUGIN_REFER_INTEGRITY + ',cn=plugins,cn=config'
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'referint-membership-attr', 'uniquemember')])
    except ldap.LDAPError as e:
        log.fatal('configureRI: Failed to configure RI plugin: error ' + e.message['desc'])
        assert False


# Configure MemberOf Plugin for stress test
def configureMO(inst):
    inst.plugins.enable(name=PLUGIN_MEMBER_OF)
    PLUGIN_DN = 'cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config'
    try:
        inst.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'memberofgroupattr', 'uniquemember')])
    except ldap.LDAPError as e:
        log.fatal('configureMO: Failed to update config(uniquemember): error ' + e.message['desc'])
        assert False


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
        conn = openConnection(self.inst)
        idx = 0
        log.info('DelUsers - Deleting ' + str(NUM_USERS) + ' entries (' + self.rdnval + ')...')
        while idx < NUM_USERS:
            USER_DN = 'uid=' + self.rdnval + str(idx) + ',' + DEFAULT_SUFFIX
            try:
                conn.delete_s(USER_DN)
            except ldap.LDAPError as e:
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
        conn = openConnection(self.inst)
        idx = 0

        if self.addToGroup:
            try:
                conn.add_s(Entry((GROUP_DN,
                    {'objectclass': 'top groupOfNames groupOfUniqueNames extensibleObject'.split(),
                     'uid': 'user' + str(idx)})))
            except ldap.ALREADY_EXISTS:
                pass
            except ldap.LDAPError as e:
                log.fatal('AddUsers: failed to add group (' + USER_DN + ') error: ' + e.message['desc'])
                assert False

        log.info('AddUsers - Adding ' + str(NUM_USERS) + ' entries (' + self.rdnval + ')...')

        while idx < NUM_USERS:
            USER_DN = 'uid=' + self.rdnval + str(idx) + ',' + DEFAULT_SUFFIX
            try:
                conn.add_s(Entry((USER_DN, {'objectclass': 'top extensibleObject'.split(),
                           'uid': 'user' + str(idx)})))
            except ldap.LDAPError as e:
                log.fatal('AddUsers: failed to add (' + USER_DN + ') error: ' + e.message['desc'])
                assert False

            if self.addToGroup:
                # Add the user to the group
                try:
                    conn.modify_s(GROUP_DN, [(ldap.MOD_ADD, 'uniquemember', USER_DN)])
                except ldap.LDAPError as e:
                    log.fatal('AddUsers: Failed to add user' + USER_DN + ' to group: error ' + e.message['desc'])
                    assert False

            idx += 1

        conn.close()
        log.info('AddUsers - Finished adding ' + str(NUM_USERS) + ' entries (' + self.rdnval + ').')
