# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
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
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


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

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': 'user', 'cn': 'buser', 'userpassword': 'Secret123'})
    user = users.create(properties=user_props)

    # Enable the audit log
    topology_st.standalone.config.set('nsslapd-auditlog-logging-enabled','on')

    # Allow the unhashed password to be written to audit log
    topology_st.standalone.config.set('nsslapd-auditlog-logging-hide-unhashed-pw', 'off')
    topology_st.standalone.config.set('nsslapd-unhashed-pw-switch', 'on')

    # Set new password, and check the audit log
    user.reset_password('mypassword')

    # Check audit log
    time.sleep(1)
    if not topology_st.standalone.searchAuditLog('unhashed#user#password: mypassword'):
        log.fatal('failed to find unhashed password in auditlog')
        assert False

    # Hide unhashed password in audit log
    topology_st.standalone.config.set('nsslapd-auditlog-logging-hide-unhashed-pw', 'on')

    # Modify password, and check the audit log
    user.reset_password('hidepassword')

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

