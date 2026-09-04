# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import json
import logging
import os
import time
from datetime import datetime, timedelta

import pytest

from lib389 import DEFAULT_SUFFIX
from lib389._constants import DN_CONFIG, DN_PLUGIN, PASSWORD
from lib389.cli_base import FakeArgs
from lib389.cli_idm.account import (
    list as account_list,
    PLUGIN_DISABLED_MSG,
    PLUGIN_INCOMPLETE_MSG,
)
from lib389.idm.role import FilteredRoles
from lib389.idm.user import UserAccounts
from lib389.plugins import AccountPolicyPlugin, AccountPolicyConfig, AccountPolicyConfigs
from test389.topologies import topology_st
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

PLUGIN_ACCT_POLICY = "Account Policy Plugin"
ACCP_DN = "cn={},{}".format(PLUGIN_ACCT_POLICY, DN_PLUGIN)
ACCP_CONF = "{},{}".format(DN_CONFIG, ACCP_DN)
INACTIVITY_LIMIT = 60
USER_PW = PASSWORD

USER_LOCKED = "list_user_locked"
USER_ROLE = "list_user_role"
USER_INACTIVE = "list_user_inactive"
USER_EXPIRED = "list_user_expired"
USER_EXPIRING = "list_user_expiring"
USER_MUST_RESET = "list_user_must_reset"
USER_NEVER_LOGIN = "list_user_never_login"
USER_NOPW = "list_user_nopw"

ROLE_NAME = "ListLockedRole"


def _make_list_args(**kwargs):
    """Build FakeArgs for dsidm account list, with every filter defaulted off."""
    args = FakeArgs()
    args.json = False
    args.locked = False
    args.expired_password = False
    args.expiring_password = None
    args.inactive = False
    args.never_logged_in = False
    args.must_reset_password = False
    for key, value in kwargs.items():
        setattr(args, key, value)
    return args


def _create_user(inst, uid, name, password=None):
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    if users.exists(name):
        users.get(name).delete()
    properties = {
        'uid': name,
        'cn': name,
        'sn': name,
        'uidNumber': str(uid),
        'gidNumber': str(uid),
        'homeDirectory': '/home/{}'.format(name),
    }
    if password is not None:
        properties['userPassword'] = password
    return users.create(properties=properties)


def _dn_in(entries, dn):
    dn_l = dn.lower()
    return any(entry.lower() == dn_l for entry in entries)


def _run_list_json(topology_st, **flags):
    args = _make_list_args(json=True, **flags)
    topology_st.logcap.flush()
    account_list(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    outputs = topology_st.logcap.get_raw_outputs()
    assert outputs, "dsidm account list produced no output"
    return json.loads(outputs[0])


def _run_list_text(topology_st, **flags):
    args = _make_list_args(json=False, **flags)
    topology_st.logcap.flush()
    account_list(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)


@pytest.fixture(scope="function")
def account_list_users(topology_st, request):
    """Create users covering each account-list filter and enable required plugins/policies."""
    inst = topology_st.standalone
    config = inst.config

    saved_pwp = {
        'passwordMustChange': config.get_attr_val_utf8('passwordMustChange'),
        'passwordExp': config.get_attr_val_utf8('passwordExp'),
        'passwordMaxAge': config.get_attr_val_utf8('passwordMaxAge'),
    }

    plugin = AccountPolicyPlugin(inst)
    if plugin.status():
        plugin.disable()
        inst.restart()

    # passwordExp + passwordMaxAge > 1 day, passwordMustChange still off so an
    # admin password set records a real expiration time (not 19700101000000Z).
    # Create anyone who must bind later while the password is still valid.
    config.replace('passwordExp', 'on')
    config.replace('passwordMaxAge', '90000')
    config.replace('passwordMustChange', 'off')

    user_expiring = _create_user(inst, 4100, USER_EXPIRING, USER_PW)
    user_expiring.replace('userPassword', USER_PW)
    user_locked = _create_user(inst, 4103, USER_LOCKED, USER_PW)
    user_locked.replace('userPassword', USER_PW)
    user_role = _create_user(inst, 4104, USER_ROLE, USER_PW)
    user_role.replace('userPassword', USER_PW)
    user_inactive = _create_user(inst, 4105, USER_INACTIVE, USER_PW)
    user_inactive.replace('userPassword', USER_PW)
    user_never_login = _create_user(inst, 4106, USER_NEVER_LOGIN, USER_PW)
    user_never_login.replace('userPassword', USER_PW)
    user_nopw = _create_user(inst, 4107, USER_NOPW)

    # Short max age so this password is expired after a brief wait.
    config.replace('passwordMaxAge', '2')
    user_expired = _create_user(inst, 4101, USER_EXPIRED, USER_PW)
    user_expired.replace('userPassword', USER_PW)

    # Admin reset with passwordMustChange=on sets passwordExpirationTime to epoch.
    config.replace('passwordMustChange', 'on')
    user_must_reset = _create_user(inst, 4102, USER_MUST_RESET, USER_PW)
    user_must_reset.replace('userPassword', USER_PW)

    user_locked.replace('nsAccountLock', 'true')

    roles = FilteredRoles(inst, DEFAULT_SUFFIX)
    if roles.exists(ROLE_NAME):
        role = roles.get(ROLE_NAME)
        try:
            role.unlock()
        except ValueError:
            pass
        role.delete()
    role = roles.create(properties={
        'cn': ROLE_NAME,
        'nsRoleFilter': '(uid={})'.format(USER_ROLE),
    })
    role.lock()

    plugin.enable()
    plugin.set('nsslapd-pluginarg0', ACCP_CONF)
    accp_configs = AccountPolicyConfigs(inst)
    accp_configs.ensure_state(
        properties={
            'cn': 'config',
            'alwaysrecordlogin': 'yes',
            'stateattrname': 'lastLoginTime',
            'altstateattrname': '1.1',
            'specattrname': 'acctPolicySubentry',
            'limitattrname': 'accountInactivityLimit',
            'accountInactivityLimit': str(INACTIVITY_LIMIT),
        }
    )
    inst.restart()

    users_c = UserAccounts(inst, DEFAULT_SUFFIX)
    user_locked = users_c.get(USER_LOCKED)
    user_role = users_c.get(USER_ROLE)
    user_inactive = users_c.get(USER_INACTIVE)
    user_expired = users_c.get(USER_EXPIRED)
    user_expiring = users_c.get(USER_EXPIRING)
    user_must_reset = users_c.get(USER_MUST_RESET)
    user_never_login = users_c.get(USER_NEVER_LOGIN)
    user_nopw = users_c.get(USER_NOPW)

    # Bind with a valid password so lastLoginTime is recorded, then age it.
    conn = user_inactive.bind(USER_PW)
    conn.unbind()
    past_time = datetime.utcnow() - timedelta(seconds=INACTIVITY_LIMIT * 2)
    user_inactive.replace('lastLoginTime', past_time.strftime('%Y%m%d%H%M%SZ'))

    # lastLoginTime present but no password → the "no password" bucket.
    user_nopw.replace('lastLoginTime', datetime.utcnow().strftime('%Y%m%d%H%M%SZ'))

    # passwordMaxAge=2: wait until expiration time is in the past.
    time.sleep(3)

    users = {
        'locked': user_locked,
        'role': user_role,
        'inactive': user_inactive,
        'expired': user_expired,
        'expiring': user_expiring,
        'must_reset': user_must_reset,
        'never_login': user_never_login,
        'nopw': user_nopw,
        'role_obj': role,
    }

    def fin():
        if DEBUGGING:
            return
        log.info('Cleaning up account list test users and plugin/policy settings')
        try:
            if role.exists():
                try:
                    role.unlock()
                except ValueError:
                    pass
                role.delete()
        except Exception as e:
            log.error('Failed to remove role: {}'.format(e))
        for user in (user_locked, user_role, user_inactive, user_expired,
                     user_expiring, user_must_reset, user_never_login, user_nopw):
            try:
                if user.exists():
                    user.delete()
            except Exception as e:
                log.error('Failed to remove user {}: {}'.format(user.dn, e))
        try:
            if plugin.status():
                plugin.disable()
            for attr, value in saved_pwp.items():
                if value is not None:
                    inst.config.replace(attr, value)
            inst.restart()
        except Exception as e:
            log.error('Failed to restore plugin/password policy: {}'.format(e))

    request.addfinalizer(fin)
    return users


def test_dsidm_account_list_requires_account_policy_plugin(topology_st):
    """--inactive and --never-logged-in fail when Account Policy plugin is off

    :id: 22dd8513-3386-4b85-9f16-711d9a8cc340
    :setup: Standalone instance with Account Policy plugin disabled
    :steps:
        1. Confirm the Account Policy plugin is not enabled
        2. Run dsidm account list --inactive
        3. Run dsidm account list --never-logged-in
        4. Run dsidm account list --locked
    :expectedresults:
        1. Plugin is disabled
        2. The command fails because the plugin is not enabled
        3. The command fails because the plugin is not enabled
        4. --locked succeeds because it does not require the plugin
    """
    inst = topology_st.standalone
    plugin = AccountPolicyPlugin(inst)
    if plugin.status():
        plugin.disable()
        inst.restart()
    assert plugin.status() is False

    topology_st.logcap.flush()
    with pytest.raises(ValueError) as excinfo:
        account_list(inst, DEFAULT_SUFFIX, topology_st.logcap.log,
                     _make_list_args(inactive=True))
    assert PLUGIN_DISABLED_MSG in str(excinfo.value)

    topology_st.logcap.flush()
    with pytest.raises(ValueError) as excinfo:
        account_list(inst, DEFAULT_SUFFIX, topology_st.logcap.log,
                     _make_list_args(never_logged_in=True))
    assert PLUGIN_DISABLED_MSG in str(excinfo.value)

    locked = _run_list_json(topology_st, locked=True)
    assert 'directly_locked' in locked


def test_dsidm_account_list_locked_with_incomplete_account_policy(topology_st, request):
    """--locked still lists nsAccountLock users when Account Policy config is incomplete

    :id: bcb8141d-8fab-4647-8439-5e72a1080add
    :setup: Standalone instance with Account Policy plugin enabled but missing
            inactivity-policy attributes
    :steps:
        1. Enable the Account Policy plugin with an incomplete config entry
        2. Create a directly locked user
        3. Run dsidm account list --locked
        4. Run dsidm account list --inactive
    :expectedresults:
        1. Plugin is enabled
        2. The locked user exists
        3. The locked user is listed
        4. --inactive fails because the plugin is not fully configured
    """
    inst = topology_st.standalone
    plugin = AccountPolicyPlugin(inst)
    accp_configs = AccountPolicyConfigs(inst)
    created = []

    def fin():
        if DEBUGGING:
            return
        log.info('Cleaning up incomplete account-policy locked-list test')
        for user_obj in created:
            try:
                if user_obj.exists():
                    user_obj.delete()
            except Exception as e:
                log.error('Failed to remove user: {}'.format(e))
        try:
            if plugin.status():
                plugin.disable()
            inst.restart()
        except Exception as e:
            log.error('Failed to disable Account Policy plugin: {}'.format(e))

    request.addfinalizer(fin)

    plugin.enable()
    plugin.set('nsslapd-pluginarg0', ACCP_CONF)
    accp_configs.ensure_state(properties={'cn': 'config'})
    config = AccountPolicyConfig(inst, ACCP_CONF)
    for attr in ('stateattrname', 'altstateattrname', 'specattrname',
                 'limitattrname', 'accountInactivityLimit', 'alwaysrecordlogin'):
        config.remove_all(attr)
    inst.restart()
    assert plugin.status() is True

    user = _create_user(inst, 4108, 'list_user_locked_incomplete', USER_PW)
    user.replace('nsAccountLock', 'true')
    created.append(user)

    locked = _run_list_json(topology_st, locked=True)
    assert _dn_in(locked['directly_locked']['accounts'], user.dn)

    topology_st.logcap.flush()
    with pytest.raises(ValueError) as excinfo:
        account_list(inst, DEFAULT_SUFFIX, topology_st.logcap.log,
                     _make_list_args(inactive=True))
    assert PLUGIN_INCOMPLETE_MSG in str(excinfo.value)


def test_dsidm_account_list_options(topology_st, account_list_users):
    """dsidm account list filters return the matching accounts when configured

    :id: 7856c78f-56b3-481f-a841-43c05f898980
    :setup: Standalone instance with Account Policy plugin, password policy
            (passwordMustChange=on, passwordExp=on, passwordMaxAge 90000 then 2),
            and users covering locked, role-locked, inactive, expired, expiring,
            must-reset, never-logged-in, and no-password states
    :steps:
        1. List locked accounts (json and text)
        2. List expired-password accounts
        3. List passwords expiring within 2 days and within 1 day
        4. List inactive accounts
        5. List must-reset-password accounts
        6. List never-logged-in accounts
    :expectedresults:
        1. Direct lock, role lock, and inactivity lock are each reported
        2. The expired-password user is listed, and the must-reset user is not
        3. The 90000s-max-age user is listed for 2 days and not for 1 day;
           already-expired and must-reset users are not listed as expiring
        4. The inactive user is listed
        5. The must-reset user is listed
        6. The never-logged-in user and the no-password user are listed
    """
    users = account_list_users

    log.info('List locked accounts')
    locked = _run_list_json(topology_st, locked=True)
    assert _dn_in(locked['directly_locked']['accounts'], users['locked'].dn), (
        "expected {} in directly_locked, got {}".format(
            users['locked'].dn, locked))
    assert _dn_in(locked['indirectly_locked']['accounts'], users['role'].dn), (
        "expected {} in indirectly_locked, got {}".format(
            users['role'].dn, locked))
    assert _dn_in(locked['inactivity_locked']['accounts'], users['inactive'].dn), (
        "expected {} in inactivity_locked, got {}".format(
            users['inactive'].dn, locked))
    _run_list_text(topology_st, locked=True)
    check_value_in_log_and_reset(topology_st, content_list=[
        users['locked'].dn,
        users['role'].dn,
        users['inactive'].dn,
        'Directly locked accounts',
        'Indirectly locked accounts',
        'Inactivity locked accounts',
    ])

    log.info('List expired password accounts')
    expired = _run_list_json(topology_st, expired_password=True)
    assert _dn_in(expired['expired_password']['accounts'], users['expired'].dn)
    assert not _dn_in(expired['expired_password']['accounts'], users['must_reset'].dn)
    _run_list_text(topology_st, expired_password=True)
    check_value_in_log_and_reset(topology_st, check_value=users['expired'].dn)

    log.info('List expiring password accounts (2 days includes 90000s max age)')
    expiring = _run_list_json(topology_st, expiring_password='2')
    assert _dn_in(expiring['expiring_password']['accounts'], users['expiring'].dn)
    assert not _dn_in(expiring['expiring_password']['accounts'], users['expired'].dn)
    assert not _dn_in(expiring['expiring_password']['accounts'], users['must_reset'].dn)
    _run_list_text(topology_st, expiring_password='2')
    check_value_in_log_and_reset(topology_st, check_value=users['expiring'].dn)

    log.info('List expiring password accounts (1 day excludes 90000s max age)')
    expiring_one_day = _run_list_json(topology_st, expiring_password='1')
    assert not _dn_in(expiring_one_day['expiring_password']['accounts'], users['expiring'].dn)

    log.info('List inactive accounts')
    inactive = _run_list_json(topology_st, inactive=True)
    assert _dn_in(inactive['inactive_accounts']['accounts'], users['inactive'].dn)
    _run_list_text(topology_st, inactive=True)
    check_value_in_log_and_reset(topology_st, check_value=users['inactive'].dn)

    log.info('List must-reset password accounts')
    must_reset = _run_list_json(topology_st, must_reset_password=True)
    assert _dn_in(must_reset['must_reset_password']['accounts'], users['must_reset'].dn)
    _run_list_text(topology_st, must_reset_password=True)
    check_value_in_log_and_reset(topology_st, check_value=users['must_reset'].dn)

    log.info('List never-logged-in and no-password accounts')
    never_logged = _run_list_json(topology_st, never_logged_in=True)
    assert _dn_in(never_logged['never_logged_in']['accounts'], users['never_login'].dn)
    assert _dn_in(never_logged['no_password']['accounts'], users['nopw'].dn)
    _run_list_text(topology_st, never_logged_in=True)
    check_value_in_log_and_reset(topology_st, content_list=[
        users['never_login'].dn,
        users['nopw'].dn,
        'Never logged in accounts',
        'No password accounts',
    ])


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
