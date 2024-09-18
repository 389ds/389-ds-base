# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import logging
import pytest
import os
import time
from lib389.topologies import topology_st as topo
from lib389._constants import (
    DEFAULT_SUFFIX,
    DN_CONFIG,
    DN_PLUGIN,
    LOG_DEFAULT,
    LOG_PLUGIN,
    PASSWORD,
    PLUGIN_ACCT_POLICY,
)
from lib389.idm.user import (UserAccount, UserAccounts)
from lib389.plugins import (AccountPolicyPlugin, AccountPolicyConfig)
from lib389.idm.domain import Domain
from datetime import datetime, timedelta

log = logging.getLogger(__name__)

ACCPOL_DN = "cn={},{}".format(PLUGIN_ACCT_POLICY, DN_PLUGIN)
ACCP_CONF = "{},{}".format(DN_CONFIG, ACCPOL_DN)
TEST_ENTRY_NAME = 'actpol_test'
TEST_ENTRY_DN = 'uid={},{}'.format(TEST_ENTRY_NAME, DEFAULT_SUFFIX)
NEW_PASSWORD = 'password123'
USER_SELF_MOD_ACI = '(targetattr="userpassword")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'
ANON_ACI = "(targetattr=\"*\")(version 3.0; acl \"Anonymous Read access\"; allow (read,search,compare) userdn = \"ldap:///anyone\";)"

def test_inactivty_and_expiration(topo):
    """Test account expiration works when we are checking all state attributes

    :id: 704310de-a2eb-4ee7-baf3-9770c0fbf07c
    :setup: Standalone Instance
    :steps:
        1. Configure instance for password expiration
        2. Add ACI to allow users to update themselves
        3. Create test user
        4. Reset users password to set passwordExpirationtime
        5. Configure account policy plugin and restart
        6. Bind as test user to reset lastLoginTime
        7. Sleep, then bind as user which triggers error

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    # passwordMaxAge should be less than accountInactivityLimit divided by 2
    INACTIVITY_LIMIT = 12
    MAX_AGE = 2

    # Configure instance
    inst = topo.standalone
    inst.config.set('passwordexp', 'on')
    inst.config.set('passwordmaxage', str(MAX_AGE))
    inst.config.set('passwordGraceLimit', '5')
    inst.config.set('nsslapd-errorlog-level', str(LOG_PLUGIN + LOG_DEFAULT))

    # Add aci so user and update password
    suffix = Domain(inst, DEFAULT_SUFFIX)
    suffix.add('aci', USER_SELF_MOD_ACI)
    suffix.add('aci', ANON_ACI)

    # Create the test user
    test_user = UserAccount(inst, TEST_ENTRY_DN)
    test_user.create(properties={
        'uid': TEST_ENTRY_NAME,
        'cn': TEST_ENTRY_NAME,
        'sn': TEST_ENTRY_NAME,
        'userPassword': PASSWORD,
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/test',
    })

    # Configure account policy plugin
    plugin = AccountPolicyPlugin(inst)
    plugin.enable()
    plugin.set('nsslapd-pluginarg0', ACCP_CONF)
    accp = AccountPolicyConfig(inst, dn=ACCP_CONF)
    accp.set('alwaysrecordlogin', 'yes')
    accp.set('stateattrname', 'lastLoginTime')
    accp.set('altstateattrname', 'passwordexpirationtime')
    accp.set('specattrname', 'acctPolicySubentry')
    accp.set('limitattrname', 'accountInactivityLimit')
    accp.set('accountInactivityLimit', str(INACTIVITY_LIMIT))
    accp.set('checkAllStateAttrs', 'on')
    inst.restart()

    # Reset test user password to reset passwordExpirationtime
    conn = test_user.bind(PASSWORD)
    test_user = UserAccount(conn, TEST_ENTRY_DN)
    test_user.reset_password(NEW_PASSWORD)

    # Sleep a little bit, we'll sleep the remaining time later
    time.sleep(INACTIVITY_LIMIT / 2)

    # Bind as test user to reset lastLoginTime
    test_user.bind(NEW_PASSWORD)

    # Sleep the remaining time plus extra double passwordMaxAge seconds
    time.sleep(INACTIVITY_LIMIT / 2 + MAX_AGE * 2)

    # Try to bind, but password expiration should reject this as lastLogintTime
    # has not exceeded the inactivity limit
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        test_user.bind(NEW_PASSWORD)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

