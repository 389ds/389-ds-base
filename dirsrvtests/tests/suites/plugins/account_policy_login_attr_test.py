# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
import logging
import os
import time
import pytest
import ldap
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts
from lib389.idm.directorymanager import DirectoryManager
from lib389.plugins import AccountPolicyPlugin, AccountPolicyConfig
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD, PLUGIN_ACCT_POLICY

pytestmark = pytest.mark.tier2
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

ACCP_CONF = f"cn=config,cn={PLUGIN_ACCT_POLICY},cn=plugins,cn=config"
ACCT_POLICY_DN = f'cn=Account Inactivation Policy,{DEFAULT_SUFFIX}'
INACTIVITY_LIMIT = '3000'

@pytest.fixture(scope="function")
def account_policy_setup(topology_st, request):
    """Setup account policy plugin and test user"""
    inst = topology_st.standalone

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    policy_user = users.create(properties={
        'uid': 'test_policy_user',
        'cn': 'test_policy_user',
        'sn': 'test_policy_user',
        'givenname': 'test_policy_user',
        'userPassword': PASSWORD,
        'uidNumber': '5000',
        'gidNumber': '5000',
        'homeDirectory': '/home/test_policy_user',
        'acctPolicySubentry': ACCT_POLICY_DN
    })

    policy_entry = AccountPolicyConfig(inst, dn=ACCT_POLICY_DN)
    policy_entry.create(properties={
        'cn': 'Account Inactivation Policy',
        'accountInactivityLimit': INACTIVITY_LIMIT
    })

    plugin = AccountPolicyPlugin(inst)
    plugin.enable()

    def fin():
        policy_user.delete()
        policy_entry.delete()
        plugin.disable()

    request.addfinalizer(fin)
    return inst, policy_user


def test_account_policy_without_always_record_login_attr(account_policy_setup):
    """Test account policy functionality without alwaysRecordLoginAttr

    :id: e8f4a3b2-1c9d-4e7f-8a6b-5d2c9e1f0a4b
    :setup: Standalone Instance with Account Policy plugin and test user
    :steps:
        1. Configure account policy without alwaysRecordLoginAttr
        2. Bind as test user
        3. Check lastLoginTime is updated
        4. Bind as test user again
        5. Verify lastLoginTime is updated to a newer value
        6. Set very low inactivity limit
        7. Try to bind again should fail with constraint violation
    :expectedresults:
        1. Configuration should succeed
        2. Bind should succeed
        3. lastLoginTime should be present
        4. Second bind should succeed
        5. lastLoginTime should be newer
        6. Inactivity limit should be set
        7. Bind should fail due to account inactivity
    """
    inst, policy_user = account_policy_setup

    log.info("Configure account policy without alwaysRecordLoginAttr")
    accp = AccountPolicyConfig(inst, dn=ACCP_CONF)
    accp.replace_many(
        ('alwaysrecordlogin', 'yes'),
        ('stateattrname', 'lastLoginTime'),
        ('altstateattrname', 'createTimestamp'),
        ('specattrname', 'acctPolicySubentry'),
        ('limitattrname', 'accountInactivityLimit')
    )

    inst.restart()

    log.info("Bind as test user")
    policy_user.bind(PASSWORD)
    time.sleep(1)

    log.info("Check lastLoginTime was added")
    dm = DirectoryManager(inst)
    dm.bind(PASSWORD)
    first_login_time = policy_user.get_attr_val_utf8('lastLoginTime')
    assert first_login_time

    log.info("Bind as test user again")
    policy_user.bind(PASSWORD)
    time.sleep(1)

    log.info("Verify lastLoginTime was updated")
    dm = DirectoryManager(inst)
    dm.bind(PASSWORD)
    second_login_time = policy_user.get_attr_val_utf8('lastLoginTime')
    assert first_login_time < second_login_time

    log.info("Change inactivity limit to trigger account lockout")
    policy_entry = AccountPolicyConfig(inst, dn=ACCT_POLICY_DN)
    policy_entry.replace('accountInactivityLimit', '1')
    time.sleep(1)

    log.info("Bind should fail due to account inactivity")
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        policy_user.bind(PASSWORD)

    policy_entry.replace('accountInactivityLimit', INACTIVITY_LIMIT)


def test_account_policy_with_always_record_login_attr(account_policy_setup):
    """Test account policy with alwaysRecordLoginAttr functionality

    :id: b7c2f9a1-8d6e-4c3b-9f5a-2e8d1c7a0b4f
    :setup: Standalone Instance with Account Policy plugin and test user
    :steps:
        1. Configure account policy with alwaysRecordLoginAttr set to lastLoginTime
        2. Set stateattrname to bogus value and altstateattrname to modifyTimestamp
        3. Remove any existing lastLoginTime from user
        4. Bind as test user
        5. Check lastLoginTime was added via alwaysRecordLoginAttr
        6. Bind as test user again
        7. Verify lastLoginTime was updated to newer value
    :expectedresults:
        1. Configuration should succeed
        2. Configuration should be updated
        3. lastLoginTime should be removed
        4. Bind should succeed
        5. lastLoginTime should be present due to alwaysRecordLoginAttr
        6. Second bind should succeed
        7. lastLoginTime should be updated to newer value
    """
    inst, policy_user = account_policy_setup

    log.info("Remove any existing lastLoginTime from user")
    try:
        policy_user.remove('lastLoginTime', None)
    except ldap.NO_SUCH_ATTRIBUTE:
        pass

    log.info("Configure account policy with alwaysRecordLoginAttr")
    accp = AccountPolicyConfig(inst, dn=ACCP_CONF)
    accp.replace_many(
        ('alwaysrecordlogin', 'yes'),
        ('stateattrname', 'bogus'),
        ('altstateattrname', 'modifyTimestamp'),
        ('alwaysRecordLoginAttr', 'lastLoginTime'),
        ('specattrname', 'acctPolicySubentry'),
        ('limitattrname', 'accountInactivityLimit')
    )

    inst.restart()

    log.info("Bind as test user")
    policy_user.bind(PASSWORD)
    time.sleep(1)

    log.info("Check lastLoginTime was added via alwaysRecordLoginAttr")
    dm = DirectoryManager(inst)
    dm.bind(PASSWORD)
    first_login_time = policy_user.get_attr_val_utf8('lastLoginTime')
    assert first_login_time

    log.info("Bind as test user again")
    policy_user.bind(PASSWORD)
    time.sleep(1)

    log.info("Verify lastLoginTime was updated")
    dm = DirectoryManager(inst)
    dm.bind(PASSWORD)
    second_login_time = policy_user.get_attr_val_utf8('lastLoginTime')
    assert first_login_time < second_login_time


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
