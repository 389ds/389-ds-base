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

from lib389._constants import DN_CONFIG, DEFAULT_SUFFIX

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

@pytest.mark.ds365
def test_hide_unhashed_pwd(topology_st):
    """Change userPassword, enable hiding of un-hashed
    password and check the audit logs.

    :id: c4a5d08d-f525-459b-82b9-3f68dae6fc71
    :setup: Standalone instance
    :steps:
        1. Add a test user entry
        2. Set a new password for user and nsslapd-auditlog-logging-enabled to 'on'
        3. Disable nsslapd-auditlog-logging-hide-unhashed-pw
        4. Check the audit logs
        5. Set a new password for user and nsslapd-auditlog-logging-hide-unhashed-pw to 'on'
        6. Check the audit logs
    :expectedresults:
        1. User addition should be successful
        2. New password should be set and audit logs should be enabled
        3. Operation should be successful
        4. Audit logs should show password without hash
        5. Operation should be successful
        6. Audit logs should hide password which is un-hashed
     """

    USER_DN = 'uid=test_entry,' + DEFAULT_SUFFIX

    #
    # Add the test entry
    #
    topology_st.standalone.add_s(Entry((USER_DN, {
        'objectclass': 'top extensibleObject'.split(),
        'uid': 'test_entry',
        'userpassword': 'password'
    })))

    #
    # Enable the audit log
    #
    topology_st.standalone.modify_s(DN_CONFIG,
                                    [(ldap.MOD_REPLACE,
                                      'nsslapd-auditlog-logging-enabled',
                                      b'on')])
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
    topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                                 'nsslapd-auditlog-logging-hide-unhashed-pw', b'off')])

    #
    # Set new password, and check the audit log
    #
    topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                               'userpassword',
                                               b'mypassword')])

    # Check audit log
    time.sleep(1)
    if not topology_st.standalone.searchAuditLog('unhashed#user#password: mypassword'):
        log.fatal('failed to find unhashed password in auditlog')
        assert False

    #
    # Hide unhashed password in audit log
    #
    topology_st.standalone.modify_s(DN_CONFIG,
                                    [(ldap.MOD_REPLACE,
                                      'nsslapd-auditlog-logging-hide-unhashed-pw',
                                      b'on')])
    log.info('Test complete')

    #
    # Modify password, and check the audit log
    #
    topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                               'userpassword',
                                               b'hidepassword')])

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

