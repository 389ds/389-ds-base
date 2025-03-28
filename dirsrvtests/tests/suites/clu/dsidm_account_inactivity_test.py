# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import time
import pytest
import logging
import os
from datetime import datetime, timedelta

from lib389 import DEFAULT_SUFFIX, DN_PLUGIN, DN_CONFIG
from lib389.cli_idm.account import entry_status, unlock
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older
from lib389.plugins import AccountPolicyPlugin, AccountPolicyConfigs
from lib389.idm.role import FilteredRoles
from lib389.idm.user import UserAccounts
from lib389.cos import CosTemplate, CosPointerDefinition
from lib389.idm.domain import Domain
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

# Constants
PLUGIN_ACCT_POLICY = "Account Policy Plugin"
ACCP_DN = f"cn={PLUGIN_ACCT_POLICY},{DN_PLUGIN}"
ACCP_CONF = f"{DN_CONFIG},{ACCP_DN}"
POLICY_NAME = "Account Inactivity Policy"
POLICY_DN = f"cn={POLICY_NAME},{DEFAULT_SUFFIX}"
COS_TEMPLATE_NAME = "TemplateCoS"
COS_TEMPLATE_DN = f"cn={COS_TEMPLATE_NAME},{DEFAULT_SUFFIX}"
COS_DEFINITION_NAME = "DefinitionCoS"
COS_DEFINITION_DN = f"cn={COS_DEFINITION_NAME},{DEFAULT_SUFFIX}"
TEST_USER_NAME = "test_inactive_user"
TEST_USER_DN = f"uid={TEST_USER_NAME},{DEFAULT_SUFFIX}"
TEST_USER_PW = "password"
INACTIVITY_LIMIT = 30


@pytest.fixture(scope="function")
def account_policy_setup(topology_st, request):
    """Set up account policy plugin, configuration, and CoS objects"""
    log.info("Setting up Account Policy Plugin and CoS")

    # Enable Account Policy Plugin
    plugin = AccountPolicyPlugin(topology_st.standalone)
    if not plugin.status():
        plugin.enable()
    plugin.set('nsslapd-pluginarg0', ACCP_CONF)

    # Configure Account Policy
    accp_configs = AccountPolicyConfigs(topology_st.standalone)
    accp_config = accp_configs.ensure_state(
        properties={
            'cn': 'config',
            'alwaysrecordlogin': 'yes',
            'stateattrname': 'lastLoginTime',
            'altstateattrname': '1.1',
            'specattrname': 'acctPolicySubentry',
            'limitattrname': 'accountInactivityLimit'
        }
    )

    # Add ACI for anonymous access if it doesn't exist
    domain = Domain(topology_st.standalone, DEFAULT_SUFFIX)
    anon_aci = '(targetattr="*")(version 3.0; acl "Anonymous read access"; allow (read,search,compare) userdn="ldap:///anyone";)'
    domain.ensure_present('aci', anon_aci)

    # Restart the server to apply plugin configuration
    topology_st.standalone.restart()

    # Create or update account policy entry
    accp_configs = AccountPolicyConfigs(topology_st.standalone, basedn=DEFAULT_SUFFIX)
    policy = accp_configs.ensure_state(
        properties={
            'cn': POLICY_NAME,
            'objectClass': ['top', 'ldapsubentry', 'extensibleObject', 'accountpolicy'],
            'accountInactivityLimit': str(INACTIVITY_LIMIT)
        }
    )

    # Create or update CoS template entry
    cos_template = CosTemplate(topology_st.standalone, dn=COS_TEMPLATE_DN)
    cos_template.ensure_state(
        properties={
            'cn': COS_TEMPLATE_NAME,
            'objectClass': ['top', 'cosTemplate', 'extensibleObject'],
            'acctPolicySubentry': policy.dn
        }
    )

    # Create or update CoS definition entry
    cos_def = CosPointerDefinition(topology_st.standalone, dn=COS_DEFINITION_DN)
    cos_def.ensure_state(
        properties={
            'cn': COS_DEFINITION_NAME,
            'objectClass': ['top', 'ldapsubentry', 'cosSuperDefinition', 'cosPointerDefinition'],
            'cosTemplateDn': COS_TEMPLATE_DN,
            'cosAttribute': 'acctPolicySubentry default operational-default'
        }
    )

    # Restart server to ensure CoS is applied
    topology_st.standalone.restart()

    def fin():
        log.info('Cleaning up Account Policy settings')
        try:
            # Delete CoS and policy entries
            if cos_def.exists():
                cos_def.delete()
            if cos_template.exists():
                cos_template.delete()
            if policy.exists():
                policy.delete()

            # Disable the plugin
            if plugin.status():
                plugin.disable()
                topology_st.standalone.restart()
        except Exception as e:
            log.error(f'Failed to clean up: {e}')

    request.addfinalizer(fin)

    return topology_st.standalone


@pytest.fixture(scope="function")
def create_test_user(topology_st, account_policy_setup, request):
    """Create a test user for the inactivity test"""
    log.info('Creating test user')

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.ensure_state(
        properties={
            'uid': TEST_USER_NAME,
            'cn': TEST_USER_NAME,
            'sn': TEST_USER_NAME,
            'userPassword': TEST_USER_PW,
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': f'/home/{TEST_USER_NAME}'
        }
    )

    def fin():
        log.info('Deleting test user')
        if user.exists():
            user.delete()

    request.addfinalizer(fin)
    return user


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Indirect account locking not implemented")
def test_dsidm_account_inactivity_lock_unlock(topology_st, create_test_user):
    """Test dsidm account unlock functionality with indirectly locked accounts

    :id: d7b57083-6111-4dbf-af84-6fca7fc7fb31
    :setup: Standalone instance with Account Policy Plugin and CoS configured
    :steps:
        1. Create a test user
        2. Bind as the test user to set lastLoginTime
        3. Check account status - should be active
        4. Set user's lastLoginTime to a time in the past that exceeds inactivity limit
        5. Check account status - should be locked due to inactivity
        6. Attempt to bind as the user - should fail with constraint violation
        7. Unlock the account using dsidm account unlock
        8. Verify account status is active again
        9. Verify the user can bind again
    :expectedresults:
        1. Success
        2. Success
        3. Account status shows as activated
        4. Success
        5. Account status shows as inactivity limit exceeded
        6. Bind attempt fails with constraint violation
        7. Account unlocked successfully
        8. Account status shows as activated
        9. User can bind successfully
    """
    standalone = topology_st.standalone
    user = create_test_user

    # Set up FakeArgs for dsidm commands
    args = FakeArgs()
    args.dn = user.dn
    args.json = False
    args.details = False

    # 1. Check initial account status - should be active
    log.info('Step 1: Checking initial account status')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value='Entry State: activated')

    # 2. Bind as test user to set initial lastLoginTime
    log.info('Step 2: Binding as test user to set lastLoginTime')
    try:
        conn = user.bind(TEST_USER_PW)
        conn.unbind()
        log.info("Successfully bound as test user")
    except ldap.LDAPError as e:
        pytest.fail(f"Failed to bind as test user: {e}")

    # 3. Set lastLoginTime to a time in the past that exceeds inactivity limit
    log.info('Step 3: Setting lastLoginTime to the past')
    past_time = datetime.utcnow() - timedelta(seconds=INACTIVITY_LIMIT * 2)
    past_time_str = past_time.strftime('%Y%m%d%H%M%SZ')
    user.replace('lastLoginTime', past_time_str)

    # 4. Check account status - should now be locked due to inactivity
    log.info('Step 4: Checking account status after setting old lastLoginTime')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value='Entry State: inactivity limit exceeded')

    # 5. Attempt to bind as the user - should fail
    log.info('Step 5: Attempting to bind as user (should fail)')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION) as excinfo:
        conn = user.bind(TEST_USER_PW)
    assert "Account inactivity limit exceeded" in str(excinfo.value)

    # 6. Unlock the account using dsidm account unlock
    log.info('Step 6: Unlocking the account with dsidm')
    unlock(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st,
                                 check_value='now unlocked by resetting lastLoginTime')

    # 7. Verify account status is active again
    log.info('Step 7: Checking account status after unlock')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value='Entry State: activated')

    # 8. Verify the user can bind again
    log.info('Step 8: Verifying user can bind again')
    try:
        conn = user.bind(TEST_USER_PW)
        conn.unbind()
        log.info("Successfully bound as test user after unlock")
    except ldap.LDAPError as e:
        pytest.fail(f"Failed to bind as test user after unlock: {e}")


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Indirect account locking not implemented")
def test_dsidm_indirectly_locked_via_role(topology_st, create_test_user):
    """Test dsidm account unlock functionality with accounts indirectly locked via role

    :id: 7bfe69bb-cf99-4214-a763-051ab2b9cf89
    :setup: Standalone instance with Role and user configured
    :steps:
        1. Create a test user
        2. Create a Filtered Role that includes the test user
        3. Lock the role
        4. Check account status - should be indirectly locked through the role
        5. Attempt to unlock the account - should fail with appropriate message
        6. Unlock the role
        7. Verify account status is active again
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Account status shows as indirectly locked
        5. Unlock attempt fails with appropriate error message
        6. Success
        7. Account status shows as activated
    """
    standalone = topology_st.standalone
    user = create_test_user

    # Use FilteredRoles and ensure_state for role creation
    log.info('Step 1: Creating Filtered Role')
    roles = FilteredRoles(standalone, DEFAULT_SUFFIX)
    role = roles.ensure_state(
        properties={
            'cn': 'TestFilterRole',
            'nsRoleFilter': f'(uid={TEST_USER_NAME})'
        }
    )

    # Set up FakeArgs for dsidm commands
    args = FakeArgs()
    args.dn = user.dn
    args.json = False
    args.details = False

    # 2. Check account status before locking role
    log.info('Step 2: Checking account status before locking role')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value='Entry State: activated')

    # 3. Lock the role
    log.info('Step 3: Locking the role')
    role.lock()

    # 4. Check account status - should be indirectly locked
    log.info('Step 4: Checking account status after locking role')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value='Entry State: indirectly locked through a Role')

    # 5. Attempt to unlock the account - should fail
    log.info('Step 5: Attempting to unlock indirectly locked account')
    unlock(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st,
                                 check_value='Account is locked through role')

    # 6. Unlock the role
    log.info('Step 6: Unlocking the role')
    role.unlock()

    # 7. Verify account status is active again
    log.info('Step 7: Checking account status after unlocking role')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value='Entry State: activated')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])