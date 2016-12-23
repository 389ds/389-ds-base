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
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket365(topology_st):
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
        topology_st.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'test_entry',
            'userpassword': 'password'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add test user: error ' + e.message['desc'])
        assert False

    #
    # Enable the audit log
    #
    try:
        topology_st.standalone.modify_s(DN_CONFIG,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-auditlog-logging-enabled',
                                          'on')])
    except ldap.LDAPError as e:
        log.fatal('Failed to enable audit log, error: ' + e.message['desc'])
        assert False
    '''
    try:
        ent = topology_st.standalone.getEntry(DN_CONFIG, attrlist=[
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
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                                     'nsslapd-auditlog-logging-hide-unhashed-pw', 'off')])
    except ldap.LDAPError as e:
        log.fatal('Failed to enable writing unhashed password to audit log, ' +
                  'error: ' + e.message['desc'])
        assert False

    #
    # Set new password, and check the audit log
    #
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword',
                                                   'mypassword')])
    except ldap.LDAPError as e:
        log.fatal('Failed to enable writing unhashed password to audit log, ' +
                  'error: ' + e.message['desc'])
        assert False

    # Check audit log
    time.sleep(1)
    if not topology_st.standalone.searchAuditLog('unhashed#user#password: mypassword'):
        log.fatal('failed to find unhashed password in auditlog')
        assert False

    #
    # Hide unhashed password in audit log
    #
    try:
        topology_st.standalone.modify_s(DN_CONFIG,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-auditlog-logging-hide-unhashed-pw',
                                          'on')])
    except ldap.LDAPError as e:
        log.fatal('Failed to deny writing unhashed password to audit log, ' +
                  'error: ' + e.message['desc'])
        assert False
    log.info('Test complete')

    #
    # Modify password, and check the audit log
    #
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                   'userpassword',
                                                   'hidepassword')])
    except ldap.LDAPError as e:
        log.fatal('Failed to enable writing unhashed password to audit log, ' +
                  'error: ' + e.message['desc'])
        assert False

    # Check audit log
    time.sleep(1)
    if topology_st.standalone.searchAuditLog('unhashed#user#password: hidepassword'):
        log.fatal('Found unhashed password in auditlog')
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
