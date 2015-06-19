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


def test_ticket365(topology):
    '''
    Write your testcase here...

    nsslapd-auditlog-logging-hide-unhashed-pw

    and test

    nsslapd-unhashed-pw-switch ticket 561

    on, off, nolog?
    '''

    USER_DN = 'uid=test_entry,' + DEFAULT_SUFFIX

    #
    # Add the test entry
    #
    try:
        topology.standalone.add_s(Entry((USER_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'test_entry',
                          'userpassword': 'password'
                          })))
    except ldap.LDAPError, e:
        log.error('Failed to add test user: error ' + e.message['desc'])
        assert False

    #
    # Enable the audit log
    #
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-auditlog-logging-enabled', 'on')])
    except ldap.LDAPError, e:
        log.fatal('Failed to enable audit log, error: ' + e.message['desc'])
        assert False
    '''
    try:
        ent = topology.standalone.getEntry(DN_CONFIG, attrlist=[
                    'nsslapd-instancedir',
                    'nsslapd-errorlog',
                    'nsslapd-accesslog',
                    'nsslapd-certdir',
                    'nsslapd-schemadir'])
    '''
    #
    # Allow the unhashed password to be written to audit log
    #
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                     'nsslapd-auditlog-logging-hide-unhashed-pw', 'off')])
    except ldap.LDAPError, e:
        log.fatal('Failed to enable writing unhashed password to audit log, error: ' + e.message['desc'])
        assert False

    #
    # Set new password, and check the audit log
    #
    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'userpassword', 'mypassword')])
    except ldap.LDAPError, e:
        log.fatal('Failed to enable writing unhashed password to audit log, error: ' + e.message['desc'])
        assert False

    # Check audit log
    if not topology.standalone.searchAuditLog('unhashed#user#password: mypassword'):
        log.fatal('failed to find unhashed password in auditlog')
        assert False

    #
    # Hide unhashed password in audit log
    #
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-auditlog-logging-hide-unhashed-pw', 'on')])
    except ldap.LDAPError, e:
        log.fatal('Failed to deny writing unhashed password to audit log, error: ' + e.message['desc'])
        assert False
    log.info('Test complete')

    #
    # Modify password, and check the audit log
    #
    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'userpassword', 'hidepassword')])
    except ldap.LDAPError, e:
        log.fatal('Failed to enable writing unhashed password to audit log, error: ' + e.message['desc'])
        assert False

    # Check audit log
    if topology.standalone.searchAuditLog('unhashed#user#password: hidepassword'):
        log.fatal('Found unhashed password in auditlog')
        assert False


def test_ticket365_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_ticket365(topo)
    test_ticket365_final(topo)


if __name__ == '__main__':
    run_isolated()

