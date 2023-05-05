# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import subprocess
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.idm.user import (UserAccount, UserAccounts)
from lib389.plugins import (AccountPolicyPlugin, AccountPolicyConfig, AccountPolicyConfigs)
from lib389.cos import (CosTemplate, CosPointerDefinition)
from lib389._constants import (PLUGIN_ACCT_POLICY, DN_PLUGIN, DN_DM, PASSWORD, DEFAULT_SUFFIX,
                               DN_CONFIG, SERVERID_STANDALONE)

pytestmark = pytest.mark.tier1

LOCL_CONF = 'cn=AccountPolicy1,ou=people,dc=example,dc=com'
TEMPL_COS = 'cn=TempltCoS,ou=people,dc=example,dc=com'
DEFIN_COS = 'cn=DefnCoS,ou=people,dc=example,dc=com'
ACCPOL_DN = "cn={},{}".format(PLUGIN_ACCT_POLICY, DN_PLUGIN)
ACCP_CONF = "{},{}".format(DN_CONFIG, ACCPOL_DN)
USER_PASW = 'Secret1234'
INVL_PASW = 'Invalid234'


@pytest.fixture(scope="module")
def accpol_global(topology_st, request):
    """Configure Global account policy plugin and restart the server"""

    log.info('Configuring Global account policy plugin, pwpolicy attributes and restarting the server')
    plugin = AccountPolicyPlugin(topology_st.standalone)
    try:
        if DEBUGGING:
            topology_st.standalone.config.set('nsslapd-auditlog-logging-enabled', 'on')
        plugin.enable()
        plugin.set('nsslapd-pluginarg0', ACCP_CONF)
        accp = AccountPolicyConfig(topology_st.standalone, dn=ACCP_CONF)
        accp.set('alwaysrecordlogin', 'yes')
        accp.set('stateattrname', 'lastLoginTime')
        accp.set('altstateattrname', 'createTimestamp')
        accp.set('specattrname', 'acctPolicySubentry')
        accp.set('limitattrname', 'accountInactivityLimit')
        accp.set('accountInactivityLimit', '12')
        topology_st.standalone.config.set('passwordexp', 'on')
        topology_st.standalone.config.set('passwordmaxage', '400')
        topology_st.standalone.config.set('passwordwarning', '1')
        topology_st.standalone.config.set('passwordlockout', 'on')
        topology_st.standalone.config.set('passwordlockoutduration', '5')
        topology_st.standalone.config.set('passwordmaxfailure', '3')
        topology_st.standalone.config.set('passwordunlock', 'on')
    except ldap.LDAPError as e:
        log.error('Failed to enable Global Account Policy Plugin and Password policy attributes')
        raise e
    topology_st.standalone.restart(timeout=10)

    def fin():
        log.info('Disabling Global accpolicy plugin and removing pwpolicy attrs')
        try:
            plugin = AccountPolicyPlugin(topology_st.standalone)
            plugin.disable()
            topology_st.standalone.config.set('passwordexp', 'off')
            topology_st.standalone.config.set('passwordlockout', 'off')
        except ldap.LDAPError as e:
            log.error('Failed to disable Global accpolicy plugin, {}'.format(e.message['desc']))
            assert False
        topology_st.standalone.restart(timeout=10)

    request.addfinalizer(fin)


@pytest.fixture(scope="module")
def accpol_local(topology_st, accpol_global, request):
    """Configure Local account policy plugin for ou=people subtree and restart the server"""

    log.info('Adding Local account policy plugin configuration entries')
    try:
        topology_st.standalone.config.set('passwordmaxage', '400')
        accp = AccountPolicyConfig(topology_st.standalone, dn=ACCP_CONF)
        accp.remove_all('accountInactivityLimit')
        locl_conf = AccountPolicyConfig(topology_st.standalone, dn=LOCL_CONF)
        locl_conf.create(properties={'cn': 'AccountPolicy1', 'accountInactivityLimit': '10'})
        cos_template = CosTemplate(topology_st.standalone, dn=TEMPL_COS)
        cos_template.create(properties={'cn': 'TempltCoS', 'acctPolicySubentry': LOCL_CONF})
        cos_def = CosPointerDefinition(topology_st.standalone,  dn=DEFIN_COS)
        cos_def.create(properties={
            'cn': 'DefnCoS',
            'cosTemplateDn': TEMPL_COS,
            'cosAttribute': 'acctPolicySubentry default operational-default'})
    except ldap.LDAPError as e:
        log.error('Failed to configure Local account policy plugin')
        log.error('Failed to add entry {}, {}, {}:'.format(LOCL_CONF, TEMPL_COS, DEFIN_COS))
        raise e
    topology_st.standalone.restart(timeout=10)

    def fin():
        log.info('Disabling Local accpolicy plugin and removing pwpolicy attrs')
        try:
            topology_st.standalone.plugins.disable(name=PLUGIN_ACCT_POLICY)
            for entry_dn in [LOCL_CONF, TEMPL_COS, DEFIN_COS]:
                entry = UserAccount(topology_st.standalone, dn=entry_dn)
                entry.delete()
        except ldap.LDAPError as e:
            log.error('Failed to disable Local accpolicy plugin, {}'.format(e.message['desc']))
            assert False
        topology_st.standalone.restart(timeout=10)

    request.addfinalizer(fin)


def pwacc_lock(topology_st, suffix, subtree, userid, nousrs):
    """Lockout user account by attempting invalid password binds"""

    log.info('Lockout user account by attempting invalid password binds')
    while (nousrs > 0):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtree, suffix)
        user = UserAccount(topology_st.standalone,  dn=userdn)
        for i in range(3):
            with pytest.raises(ldap.INVALID_CREDENTIALS):
                user.bind(INVL_PASW)
                log.error('No invalid credentials error for User {}'.format(userdn))
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            user.bind(USER_PASW)
            log.error('User {} is not locked, expected error 19'.format(userdn))
        nousrs = nousrs - 1
        time.sleep(1)


def userpw_reset(topology_st, suffix, subtree, userid, nousrs, bindusr, bindpw, newpasw):
    """Reset user password"""

    while (nousrs > 0):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtree, suffix)
        user = UserAccount(topology_st.standalone,  dn=userdn)
        log.info('Reset user password for user-{}'.format(userdn))
        if (bindusr == "DirMgr"):
            try:
                user.replace('userPassword', newpasw)
            except ldap.LDAPError as e:
                log.error('Unable to reset userPassword for user-{}'.format(userdn))
                raise e
        elif (bindusr == "RegUsr"):
            user_conn = user.bind(bindpw)
            try:
                user_conn.replace('userPassword', newpasw)
            except ldap.LDAPError as e:
                log.error('Unable to reset userPassword for user-{}'.format(userdn))
                raise e
        nousrs = nousrs - 1
        time.sleep(1)


def nsact_inact(topology_st, suffix, subtree, userid, nousrs, command, expected):
    """Account activate/in-activate/status using dsidm"""

    log.info('Account activate/in-activate/status using dsidm')
    while (nousrs > 0):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtree, suffix)
        log.info('Running {} for user {}'.format(command, userdn))

        dsidm_cmd = ['%s/dsidm' % topology_st.standalone.get_sbin_dir(),
                     'slapd-standalone1',
                     '-b', DEFAULT_SUFFIX,
                     'account', command,
                     userdn]

        log.info('Running {} for user {}'.format(dsidm_cmd, userdn))
        try:
            output = subprocess.check_output(dsidm_cmd)
        except subprocess.CalledProcessError as err:
            output = err.output

        log.info('output: {}'.format(output))
        assert ensure_bytes(expected) in output
        nousrs = nousrs - 1
        time.sleep(1)


def modify_attr(topology_st, base_dn, attr_name, attr_val):
    """Modify attribute value for a given DN"""

    log.info('Modify attribute value for a given DN')
    try:
        entry = UserAccount(topology_st.standalone, dn=base_dn)
        entry.replace(attr_name, attr_val)
    except ldap.LDAPError as e:
        log.error('Failed to replace lastLoginTime attribute for user-{} {}'.format(userdn, e.message['desc']))
        assert False
    time.sleep(1)


def check_attr(topology_st, suffix, subtree, userid, nousrs, attr_name):
    """Check ModifyTimeStamp attribute present for user"""

    log.info('Check ModifyTimeStamp attribute present for user')
    while (nousrs > 0):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtree, suffix)
        user = UserAccount(topology_st.standalone, dn=userdn)
        try:
            user.get_attr_val(attr_name)
        except ldap.LDAPError as e:
            log.error('ModifyTimeStamp attribute is not present for user-{} {}'.format(userdn, e.message['desc']))
            assert False
        nousrs = nousrs - 1


def add_time_attr(topology_st, suffix, subtree, userid, nousrs, attr_name):
    """Enable account by replacing lastLoginTime/createTimeStamp/ModifyTimeStamp attribute"""

    new_attr_val = time.strftime("%Y%m%d%H%M%S", time.gmtime()) + 'Z'
    log.info('Enable account by replacing lastLoginTime/createTimeStamp/ModifyTimeStamp attribute')
    while (nousrs > 0):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtree, suffix)
        user = UserAccount(topology_st.standalone, dn=userdn)
        try:
            user.replace(attr_name, new_attr_val)
        except ldap.LDAPError as e:
            log.error('Failed to add/replace {} attribute to-{}, for user-{}'.format(attr_name, new_attr_val, userdn))
            raise e
        nousrs = nousrs - 1
        time.sleep(1)
    time.sleep(1)


def modusr_attr(topology_st, suffix, subtree, userid, nousrs, attr_name, attr_value):
    """Enable account by replacing cn attribute value, value of modifyTimeStamp changed"""

    log.info('Enable account by replacing cn attribute value, value of modifyTimeStamp changed')
    while (nousrs > 0):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtree, suffix)
        user = UserAccount(topology_st.standalone, dn=userdn)
        try:
            user.replace(attr_name, attr_value)
        except ldap.LDAPError as e:
            log.error('Failed to add/replace {} attribute to-{}, for user-{}'.format(attr_name, attr_value, userdn))
            raise e
        nousrs = nousrs - 1
        time.sleep(1)


def del_time_attr(topology_st, suffix, subtree, userid, nousrs, attr_name):
    """Delete lastLoginTime/createTimeStamp/ModifyTimeStamp attribute from user account"""

    log.info('Delete lastLoginTime/createTimeStamp/ModifyTimeStamp attribute from user account')
    while (nousrs > 0):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtree, suffix)
        user = UserAccount(topology_st.standalone, dn=userdn)
        try:
            user.remove_all(attr_name)
        except ldap.LDAPError as e:
            log.error('Failed to delete {} attribute for user-{}'.format(attr_name, userdn))
            raise e
        nousrs = nousrs - 1
        time.sleep(1)


def add_users(topology_st, suffix, subtree, userid, nousrs, ulimit):
    """Add users to default test instance with given suffix, subtree, userid and nousrs"""

    log.info('add_users: Pass all of these as parameters suffix, subtree, userid and nousrs')
    users = UserAccounts(topology_st.standalone, suffix, rdn=subtree)
    while (nousrs > ulimit):
        usrrdn = '{}{}'.format(userid, nousrs)
        user_properties = {
            'uid': usrrdn,
            'cn': usrrdn,
            'sn': usrrdn,
            'uidNumber': '1001',
            'gidNumber': '2001',
            'userpassword': USER_PASW,
            'homeDirectory': '/home/{}'.format(usrrdn)}
        users.create(properties=user_properties)
        nousrs = nousrs - 1


def del_users(topology_st, suffix, subtree, userid, nousrs):
    """Delete users from default test instance with given suffix, subtree, userid and nousrs"""

    log.info('del_users: Pass all of these as parameters suffix, subtree, userid and nousrs')
    users = UserAccounts(topology_st.standalone, suffix, rdn=subtree)
    while (nousrs > 0):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = users.get(usrrdn)
        userdn.delete()
        nousrs = nousrs - 1


def account_status(topology_st, suffix, subtree, userid, nousrs, ulimit, tochck):
    """Check account status for the given suffix, subtree, userid and nousrs"""

    while (nousrs > ulimit):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtree, suffix)
        user = UserAccount(topology_st.standalone, dn=userdn)
        if (tochck == "Enabled"):
            try:
                user.bind(USER_PASW)
            except ldap.LDAPError as e:
                log.error('User {} failed to login, expected 0'.format(userdn))
                raise e
        elif (tochck == "Expired"):
            with pytest.raises(ldap.INVALID_CREDENTIALS):
                user.bind(USER_PASW)
                log.error('User {} password not expired , expected error 49'.format(userdn))
        elif (tochck == "Disabled"):
            with pytest.raises(ldap.CONSTRAINT_VIOLATION):
                user.bind(USER_PASW)
                log.error('User {} is not inactivated, expected error 19'.format(userdn))
        nousrs = nousrs - 1
        time.sleep(1)


def user_binds(user, user_pw, num_binds):
    """ Bind as user a number of times """
    for i in range(num_binds):
        userconn = user.bind(user_pw)
        time.sleep(1)
        userconn.unbind()


def verify_last_login_entries(inst, dn, expected):
    """ Search for lastLoginHistory attribute and verify the number and order of entries """
    entries = inst.search_s(dn, ldap.SCOPE_SUBTREE, "(objectclass=*)", ['lastLoginHistory'])
    decoded_values = [entry.decode() for entry in entries[0].getValues('lastLoginHistory')]
    ascending_order = all(decoded_values[i] <= decoded_values[i + 1] for i in range(len(decoded_values) - 1))
    assert len(decoded_values) == expected
    assert ascending_order


def test_glact_inact(topology_st, accpol_global):
    """Verify if user account is inactivated when accountInactivityLimit is exceeded.

    :id: 342af084-0ad0-442f-b6f6-5a8b8e5e4c28
    :setup: Standalone instance, Global account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Add few users to ou=people subtree in the default suffix
        2. Check if users are active just before it reaches accountInactivityLimit.
        3. User accounts should not be inactivated, expected 0
        4. Check if users are inactivated when accountInactivityLimit is exceeded.
        5. User accounts should be inactivated, expected error 19.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Should return error code 19
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=people"
    userid = "glinactusr"
    nousrs = 3
    log.info('AccountInactivityLimit set to 12. Account will be inactivated if not accessed in 12 secs')
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)

    log.info('Sleep for 10 secs to check if account is not inactivated, expected value 0')
    time.sleep(10)
    log.info('Account should not be inactivated since AccountInactivityLimit not exceeded')
    account_status(topology_st, suffix, subtree, userid, 3, 2, "Enabled")

    log.info('Sleep for 3 more secs to check if account is inactivated')
    time.sleep(3)
    account_status(topology_st, suffix, subtree, userid, 2, 0, "Disabled")

    log.info('Sleep +10 secs to check if account {}3 is inactivated'.format(userid))
    time.sleep(10)
    account_status(topology_st, suffix, subtree, userid, 3, 2, "Disabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)


def test_glremv_lastlogin(topology_st, accpol_global):
    """Verify if user account is inactivated by createTimeStamp, if lastLoginTime attribute is missing.

    :id: 8ded5d8e-ed93-4c22-9c8e-78c479189f84
    :setup: Standalone instance, Global account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Add few users to ou=people subtree in the default suffix
        2. Wait for few secs and bind as user to create lastLoginTime attribute.
        3. Remove the lastLoginTime attribute from the user.
        4. Wait till accountInactivityLimit exceeded based on createTimeStamp value
        5. Check if users are inactivated, expected error 19.
        6. Replace lastLoginTime attribute and check if account is activated
        7. User should be activated based on lastLoginTime attribute, expected 0
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Should return error code 19
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=people"
    userid = "nologtimeusr"
    nousrs = 1
    log.info('AccountInactivityLimit set to 12. Account will be inactivated if not accessed in 12 secs')
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)
    log.info('Sleep for 6 secs to check if account is not inactivated, expected value 0')
    time.sleep(6)
    log.info('Account should not be inactivated since AccountInactivityLimit not exceeded')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    del_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    log.info('Sleep for 7 more secs to check if account is inactivated')
    time.sleep(7)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    log.info('Check if account is activated, expected 0')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)


def test_lastlogin_history(topology_st, request):
    """Verify a user account with attr alwaysrecordlogin=yes returns no more
    than the last login history size and that the timestamps are in chronological order.

    :id: 34725a73-c2ba-4b18-9329-532c1514327f
    :setup: Standalone instance, Global account policy plugin configuration,
            set alwaysrecordlogin to yes.
    :steps:
        1. Enable account policy plugin and restart instance.
        2. Add a config entry, setting alwaysrecordlogin to yes (lastLoginHistorySize defaults to 5)
        3. Create a test user and reset its password.
        4. Bind as test user more times than lastLoginHistorySize.
        5. Search on the test user DN for lastLoginTimeHistory attribute.
        6. Verify returned entry contains only LOGIN_HIST_SIZE_FIVE timestamps in chronological order.
        7. Modify plugin config entry, setting lastLoginHistorySize to LOGIN_HIST_SIZE_TWO
        8. Bind as test user more times than lastLoginHistorySize.
        9. Search on the test user DN for lastLoginTimeHistory attribute.
        10. Verify returned entry contains only LOGIN_HIST_SIZE_TWO timestamps in chronological order.
        11. Modify plugin config entry, setting lastLoginHistorySize to LOGIN_HIST_SIZE_FIVE
        12. Search on the test user DN for lastLoginTimeHistory attribute.
        13. Verify returned entry contains only LOGIN_HIST_SIZE_FIVE timestamps in chronological order.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
    """

    USER_DN = 'uid=test_user_1000,ou=people,dc=example,dc=com'
    USER_PW = 'password'
    LOGIN_HIST_NUM_BINDS_SEVEN = 7
    LOGIN_HIST_SIZE_FIVE = 5
    LOGIN_HIST_SIZE_TWO = 2

    inst = topology_st[0]

    # Enable plugin and restart
    plugin = AccountPolicyPlugin(inst)
    plugin.disable()
    plugin.enable()
    inst.restart()

    # Add config entry, set alwaysrecordlogin to yes (lastLoginHistorySize defaults to 5)
    ap_configs = AccountPolicyConfigs(inst)
    try:
        ap_config = ap_configs.create(properties={'cn': 'config', 'alwaysrecordlogin': 'yes', })
    except ldap.ALREADY_EXISTS:
        ap_config = ap_configs.get('config')
        ap_config.replace('alwaysrecordlogin', 'yes')

    # Add a test user entry
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create_test_user(uid=1000, gid=2000)
    user.replace('userPassword', USER_PW)

    # Bind as test user more times than lastLoginHistorySize
    user_binds(user, USER_PW, LOGIN_HIST_NUM_BINDS_SEVEN)

    # Verify lastLoginTimeHistory attribute returns the correct number of entries in chronological order
    verify_last_login_entries(inst, USER_DN, LOGIN_HIST_SIZE_FIVE)

    # Reduce the lastLoginHistorySize to LOGIN_HIST_SIZE_TWO
    try:
        ap_config = ap_configs.create(properties={'cn': 'config', 'lastLoginHistorySize': str(LOGIN_HIST_SIZE_TWO)})
    except ldap.ALREADY_EXISTS:
        ap_config = ap_configs.get('config')
        ap_config.replace('lastLoginHistorySize', str(LOGIN_HIST_SIZE_TWO))

    # Bind as test user more times than lastLoginHistorySize
    user_binds(user, USER_PW, LOGIN_HIST_NUM_BINDS_SEVEN)

    # Verify lastLoginTimeHistory attribute returns the correct number of entries in chronological order
    verify_last_login_entries(inst, USER_DN, LOGIN_HIST_SIZE_TWO)

    # Increase the lastLoginHistorySize to LOGIN_HIST_SIZE_FIVE
    try:
        ap_config = ap_configs.create(properties={'cn': 'config', 'lastLoginHistorySize': str(LOGIN_HIST_SIZE_FIVE)})
    except ldap.ALREADY_EXISTS:
        ap_config = ap_configs.get('config')
        ap_config.replace('lastLoginHistorySize', str(LOGIN_HIST_SIZE_FIVE))

    # Bind as test user more times than lastLoginHistorySize
    user_binds(user, USER_PW, LOGIN_HIST_NUM_BINDS_SEVEN)

    # Verify lastLoginTimeHistory attribute returns the correct number of entries in chronological order
    verify_last_login_entries(inst, USER_DN, LOGIN_HIST_SIZE_FIVE)

    def fin():
        log.info('test_lastlogin_history cleanup')
        user.delete()

    request.addfinalizer(fin)


def test_glact_login(topology_st, accpol_global):
    """Verify if user account can be activated by replacing the lastLoginTime attribute.

    :id: f89897cc-c13e-4824-af08-3dd1039bab3c
    :setup: Standalone instance, Global account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Add few users to ou=groups subtree in the default suffix
        2. Wait till accountInactivityLimit exceeded
        3. Run ldapsearch as normal user, expected error 19.
        4. Replace the lastLoginTime attribute and check if account is activated
        5. Run ldapsearch as normal user, expected 0.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=groups"
    userid = "glactusr"
    nousrs = 3
    log.info('AccountInactivityLimit set to 12. Account will be inactivated if not accessed in 12 secs')
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)
    log.info('Sleep for 13 secs to check if account is inactivated, expected error 19')
    time.sleep(13)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    log.info('Check if account is activated, expected 0')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)


def test_glinact_limit(topology_st, accpol_global):
    """Verify if account policy plugin functions well when changing accountInactivityLimit value.

    :id: 7fbc373f-a3d7-4774-8d34-89b057c5e74b
    :setup: Standalone instance, Global account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Add few users to ou=groups subtree in the default suffix
        2. Check if users are active just before reaching accountInactivityLimit
        3. Modify AccountInactivityLimit to a bigger value
        4. Wait for additional few secs, but check users before it reaches accountInactivityLimit
        5. Wait till accountInactivityLimit exceeded and check users, expected error 19
        6. Modify accountInactivityLimit to use the min value.
        7. Add few users to ou=groups subtree in the default suffix
        8. Wait till it reaches accountInactivityLimit and check users, expected error 19
        9. Modify accountInactivityLimit to 10 times(30 secs) bigger than the initial value.
        10. Add few users to ou=groups subtree in the default suffix
        11. Wait for 90 secs and check if account is not inactivated, expected 0
        12. Wait for +27 secs and check if account is not inactivated, expected 0
        13. Wait for +30 secs and check if account is inactivated, error 19
        14. Replace the lastLoginTime attribute and check if account is activated
        15. Modify accountInactivityLimit to 12 secs, which is the default
        16. Run ldapsearch as normal user, expected 0.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=groups"
    userid = "inactestusr"
    nousrs = 3

    log.info('AccountInactivityLimit set to 12. Account will be inactivated if not accessed in 12 secs')
    add_users(topology_st, suffix, subtree, userid, nousrs, 2)
    log.info('Sleep for 9 secs to check if account is not inactivated, expected 0')
    time.sleep(9)
    account_status(topology_st, suffix, subtree, userid, nousrs, 2, "Enabled")

    modify_attr(topology_st, ACCP_CONF, 'accountInactivityLimit', '20')
    time.sleep(17)
    account_status(topology_st, suffix, subtree, userid, nousrs, 2, "Enabled")
    time.sleep(20)
    account_status(topology_st, suffix, subtree, userid, nousrs, 2, "Disabled")

    modify_attr(topology_st, ACCP_CONF, 'accountInactivityLimit', '1')
    add_users(topology_st, suffix, subtree, userid, 2, 1)
    time.sleep(2)
    account_status(topology_st, suffix, subtree, userid, 2, 1, "Disabled")

    modify_attr(topology_st, ACCP_CONF, 'accountInactivityLimit', '30')
    add_users(topology_st, suffix, subtree, userid, 1, 0)
    time.sleep(27)
    account_status(topology_st, suffix, subtree, userid, 1, 0, "Enabled")
    time.sleep(30)
    account_status(topology_st, suffix, subtree, userid, 1, 0, "Disabled")

    log.info('Check if account is activated, expected 0')
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")

    modify_attr(topology_st, ACCP_CONF, 'accountInactivityLimit', '12')
    del_users(topology_st, suffix, subtree, userid, nousrs)

#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_glnologin_attr(topology_st, accpol_global):
    """Verify if user account is inactivated based on createTimeStamp attribute, no lastLoginTime attribute present

    :id: 3032f670-705d-4f69-96f5-d75445cffcfb
    :setup: Standalone instance, Local account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Configure Global account policy plugin with createTimestamp as stateattrname
        2. lastLoginTime attribute will not be effective.
        3. Add few users to ou=groups subtree in the default suffix
        4. Wait for 10 secs and check if account is not inactivated, expected 0
        5. Modify AccountInactivityLimit to 20 secs
        6. Wait for +9 secs and check if account is not inactivated, expected 0
        7. Wait for +3 secs and check if account is inactivated, error 19
        8. Modify accountInactivityLimit to 3 secs
        9. Add few users to ou=groups subtree in the default suffix
        10. Wait for 3 secs and check if account is inactivated, error 19
        11. Modify accountInactivityLimit to 30 secs
        12. Add few users to ou=groups subtree in the default suffix
        13. Wait for 90 secs and check if account is not inactivated, expected 0
        14. Wait for +28 secs and check if account is not inactivated, expected 0
        15. Wait for +2 secs and check if account is inactivated, error 19
        16. Replace the lastLoginTime attribute and check if account is activated
        17. Modify accountInactivityLimit to 12 secs, which is the default
        18. Run ldapsearch as normal user, expected 0.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
        17. Success
        18. Success
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=groups"
    userid = "nologinusr"
    nousrs = 3

    log.info('AccountInactivityLimit set to 12. Account will be inactivated if not accessed in 12 secs')
    log.info('Set attribute StateAttrName to createTimestamp, loginTime attr wont be considered')
    modify_attr(topology_st, ACCP_CONF, 'stateattrname', 'createTimestamp')
    topology_st.standalone.restart(timeout=10)
    add_users(topology_st, suffix, subtree, userid, nousrs, 2)
    log.info('Sleep for 9 secs to check if account is not inactivated, expected 0')
    time.sleep(9)
    account_status(topology_st, suffix, subtree, userid, nousrs, 2, "Enabled")

    modify_attr(topology_st, ACCP_CONF, 'accountInactivityLimit', '20')
    time.sleep(9)
    account_status(topology_st, suffix, subtree, userid, nousrs, 2, "Enabled")
    time.sleep(3)
    account_status(topology_st, suffix, subtree, userid, nousrs, 2, "Disabled")

    modify_attr(topology_st, ACCP_CONF, 'accountInactivityLimit', '3')
    add_users(topology_st, suffix, subtree, userid, 2, 1)
    time.sleep(2)
    account_status(topology_st, suffix, subtree, userid, 2, 1, "Enabled")
    time.sleep(2)
    account_status(topology_st, suffix, subtree, userid, 2, 1, "Disabled")

    modify_attr(topology_st, ACCP_CONF, 'accountInactivityLimit', '30')
    add_users(topology_st, suffix, subtree, userid, 1, 0)
    time.sleep(28)
    account_status(topology_st, suffix, subtree, userid, 1, 0, "Enabled")
    time.sleep(2)
    account_status(topology_st, suffix, subtree, userid, 1, 0, "Disabled")

    modify_attr(topology_st, ACCP_CONF, 'accountInactivityLimit', '12')
    log.info('Set attribute StateAttrName to lastLoginTime, the default')
    modify_attr(topology_st, ACCP_CONF, 'stateattrname', 'lastLoginTime')
    topology_st.standalone.restart(timeout=10)
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    log.info('Check if account is activated, expected 0')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)

#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_glnoalt_stattr(topology_st, accpol_global):
    """Verify if user account can be inactivated based on lastLoginTime attribute, altstateattrname set to 1.1

    :id: 8dcc3540-578f-422a-bb44-28c2cf20dbcd
    :setup: Standalone instance, Global account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Configure Global account policy plugin with altstateattrname to 1.1
        2. Add few users to ou=groups subtree in the default suffix
        3. Wait till it reaches accountInactivityLimit
        4. Remove lastLoginTime attribute from the user entry
        5. Run ldapsearch as normal user, expected 0. no lastLoginTime attribute present
        6. Wait till it reaches accountInactivityLimit and check users, expected error 19
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=groups"
    userid = "nologinusr"
    nousrs = 3
    log.info('Set attribute altStateAttrName to 1.1')
    modify_attr(topology_st, ACCP_CONF, 'altstateattrname', '1.1')
    topology_st.standalone.restart(timeout=10)
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)
    log.info('Sleep for 13 secs to check if account is not inactivated, expected 0')
    time.sleep(13)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    log.info('lastLoginTime attribute is added from the above ldap bind by userdn')
    time.sleep(13)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    del_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    modify_attr(topology_st, ACCP_CONF, 'altstateattrname', 'createTimestamp')
    topology_st.standalone.restart(timeout=10)
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)


#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_glattr_modtime(topology_st, accpol_global):
    """Verify if user account can be inactivated based on modifyTimeStamp attribute

    :id: 67380839-2966-45dc-848a-167a954153e1
    :setup: Standalone instance, Global account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Configure Global account policy plugin with altstateattrname to modifyTimestamp
        2. Add few users to ou=groups subtree in the default suffix
        3. Wait till the accountInactivityLimit exceeded and check users, expected error 19
        4. Modify cn attribute for user, ModifyTimeStamp is updated.
        5. Check if user is activated based on ModifyTimeStamp attribute, expected 0
        6. Change the plugin to use createTimeStamp and remove lastLoginTime attribute
        7. Check if account is inactivated, expected error 19
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=groups"
    userid = "modtimeusr"
    nousrs = 3
    log.info('Set attribute altStateAttrName to modifyTimestamp')
    modify_attr(topology_st, ACCP_CONF, 'altstateattrname', 'modifyTimestamp')
    topology_st.standalone.restart(timeout=10)
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)
    log.info('Sleep for 13 secs to check if account is inactivated, expected 0')
    time.sleep(13)
    check_attr(topology_st, suffix, subtree, userid, nousrs, "modifyTimeStamp=*")
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    attr_name = "cn"
    attr_value = "cnewusr"
    modusr_attr(topology_st, suffix, subtree, userid, nousrs, attr_name, attr_value)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    modify_attr(topology_st, ACCP_CONF, 'altstateattrname', 'createTimestamp')
    del_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    topology_st.standalone.restart(timeout=10)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)


#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_glnoalt_nologin(topology_st, accpol_global):
    """Verify if account policy plugin works if we set altstateattrname set to 1.1 and alwaysrecordlogin to NO

    :id: 49eda7db-84de-47ba-8f81-ac5e4de3a500
    :setup: Standalone instance, Global account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Configure Global account policy plugin with altstateattrname to 1.1
        2. Set alwaysrecordlogin to NO.
        3. Add few users to ou=groups subtree in the default suffix
        4. Wait till accountInactivityLimit exceeded and check users, expected 0
        5. Check for lastLoginTime attribute, it should not be present
        6. Wait for few more secs and check if account is not inactivated, expected 0
        7. Run ldapsearch as normal user, expected 0. no lastLoginTime attribute present
        8. Set altstateattrname to createTimeStamp
        9. Check if user account is inactivated based on createTimeStamp attribute.
        10. Account should be inactivated, expected error 19
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=groups"
    userid = "norecrodlogusr"
    nousrs = 3
    log.info('Set attribute altStateAttrName to 1.1')
    modify_attr(topology_st, ACCP_CONF, 'altstateattrname', '1.1')
    log.info('Set attribute alwaysrecordlogin to No')
    modify_attr(topology_st, ACCP_CONF, 'alwaysrecordlogin', 'no')
    topology_st.standalone.restart(timeout=10)
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)
    log.info('Sleep for 13 secs to check if account is not inactivated, expected 0')
    time.sleep(13)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    time.sleep(3)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    log.info('Set attribute altStateAttrName to createTimestamp')
    modify_attr(topology_st, ACCP_CONF, 'altstateattrname', 'createTimestamp')
    topology_st.standalone.restart(timeout=10)
    time.sleep(2)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    log.info('Reset the default attribute values')
    modify_attr(topology_st, ACCP_CONF, 'alwaysrecordlogin', 'yes')
    topology_st.standalone.restart(timeout=10)
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)


#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_glinact_nsact(topology_st, accpol_global):
    """Verify if user account can be activated using dsidm.

    :id: 876a7a7c-0b3f-4cd2-9b45-1dc80846e334
    :setup: Standalone instance, Global account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Configure Global account policy plugin
        2. Add few users to ou=groups subtree in the default suffix
        3. Wait for few secs and inactivate user using dsidm
        4. Wait till accountInactivityLimit exceeded.
        5. Run ldapsearch as normal user, expected error 19.
        6. Activate user using ns-activate.pl script
        7. Check if account is activated, expected error 19
        8. Replace the lastLoginTime attribute and check if account is activated
        9. Run ldapsearch as normal user, expected 0.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=groups"
    userid = "nsactusr"
    nousrs = 1

    log.info('AccountInactivityLimit set to 12. Account will be inactivated if not accessed in 12 secs')
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)
    log.info('Sleep for 3 secs to check if account is not inactivated, expected value 0')
    time.sleep(3)
    nsact_inact(topology_st, suffix, subtree, userid, nousrs, "unlock", "")
    log.info('Sleep for 10 secs to check if account is inactivated, expected value 19')
    time.sleep(10)
    nsact_inact(topology_st, suffix, subtree, userid, nousrs, "unlock", "")
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    nsact_inact(topology_st, suffix, subtree, userid, nousrs, "entry-status",
                "inactivity limit exceeded")
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    nsact_inact(topology_st, suffix, subtree, userid, nousrs, "entry-status", "activated")
    del_users(topology_st, suffix, subtree, userid, nousrs)


#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_glinact_acclock(topology_st, accpol_global):
    """Verify if user account is activated when account is unlocked by passwordlockoutduration.

    :id: 43601a61-065c-4c80-a7c2-e4f6ae17beb8
    :setup: Standalone instance, Global account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Add few users to ou=groups subtree in the default suffix
        2. Wait for few secs and attempt invalid binds for user
        3. User account should be locked based on Account Lockout policy.
        4. Wait till accountInactivityLimit exceeded and check users, expected error 19
        5. Wait for passwordlockoutduration and check if account is active
        6. Check if account is unlocked, expected error 19, since account is inactivated
        7. Replace the lastLoginTime attribute and check users, expected 0
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=groups"
    userid = "pwlockusr"
    nousrs = 1
    log.info('AccountInactivityLimit set to 12. Account will be inactivated if not accessed in 12 secs')
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)
    log.info('Sleep for 3 secs and try invalid binds to lockout the user')
    time.sleep(3)

    pwacc_lock(topology_st, suffix, subtree, userid, nousrs)
    log.info('Sleep for 10 secs to check if account is inactivated, expected value 19')
    time.sleep(10)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")

    log.info('Add lastLoginTime to activate the user account')
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")

    log.info('Checking if account is unlocked after passwordlockoutduration, but inactivated after accountInactivityLimit')
    pwacc_lock(topology_st, suffix, subtree, userid, nousrs)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")

    log.info('Account is expected to be unlocked after 5 secs of passwordlockoutduration')
    time.sleep(5)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")

    log.info('Sleep 13s and check if account inactivated based on accountInactivityLimit, expected 19')
    time.sleep(13)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)


#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_glnact_pwexp(topology_st, accpol_global):
    """Verify if user account is activated when password is reset after password is expired

    :id:  3bb97992-101a-4e5a-b60a-4cc21adcc76e
    :setup: Standalone instance, Global account policy plugin configuration,
            set accountInactivityLimit to few secs.
    :steps:
        1. Add few users to ou=groups subtree in the default suffix
        2. Set passwordmaxage to few secs
        3. Wait for passwordmaxage to reach and check if password expired
        4. Run ldapsearch as normal user, expected error 19.
        5. Reset the password for user account
        6. Wait till accountInactivityLimit exceeded and check users
        7. Run ldapsearch as normal user, expected error 19.
        8. Replace the lastLoginTime attribute and check if account is activated
        9. Run ldapsearch as normal user, expected 0.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=groups"
    userid = "pwexpusr"
    nousrs = 1
    try:
        topology_st.standalone.config.set('passwordmaxage', '9')
    except ldap.LDAPError as e:
        log.error('Failed to change the value of passwordmaxage to 9')
        raise e
    log.info('AccountInactivityLimit set to 12. Account will be inactivated if not accessed in 12 secs')
    log.info('Passwordmaxage is set to 9. Password will expire in 9 secs')
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)

    log.info('Sleep for 9 secs and check if password expired')
    time.sleep(9)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Expired")
    time.sleep(4)  # Passed inactivity
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")

    log.info('Add lastLoginTime to activate the user account')
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Expired")
    userpw_reset(topology_st, suffix, subtree, userid, nousrs, "DirMgr", PASSWORD, USER_PASW)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")

    # Allow password to expire again, but inactivity continues
    time.sleep(7)

    # reset password to counter expiration, we will test expiration again later
    userpw_reset(topology_st, suffix, subtree, userid, nousrs, "DirMgr", PASSWORD, USER_PASW)
    log.info('Sleep for 4 secs and check if account is now inactivated, expected error 19')
    time.sleep(4)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    userpw_reset(topology_st, suffix, subtree, userid, nousrs, "DirMgr", PASSWORD, USER_PASW)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")

    # Reset inactivity and check for expiration
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    time.sleep(8)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Expired")

    # Reset account
    userpw_reset(topology_st, suffix, subtree, userid, nousrs, "DirMgr", PASSWORD, USER_PASW)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")

    # Reset maxage
    try:
        topology_st.standalone.config.set('passwordmaxage', '400')
    except ldap.LDAPError as e:
        log.error('Failed to change the value of passwordmaxage to 400')
        raise e
    del_users(topology_st, suffix, subtree, userid, nousrs)


#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_locact_inact(topology_st, accpol_local):
    """Verify if user account is inactivated when accountInactivityLimit is exceeded.

    :id: 02140e36-79eb-4d88-ba28-66478689289b
    :setup: Standalone instance, ou=people subtree configured for Local account
            policy plugin configuration, set accountInactivityLimit to few secs.
    :steps:
        1. Add few users to ou=people subtree in the default suffix
        2. Wait for few secs before it reaches accountInactivityLimit and check users.
        3. Run ldapsearch as normal user, expected 0
        4. Wait till accountInactivityLimit is exceeded
        5. Run ldapsearch as normal user and check if its inactivated, expected error 19.
        6. Replace user's lastLoginTime attribute and check if its activated, expected 0
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Should return error code 19
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=people"
    userid = "inactusr"
    nousrs = 3
    log.info('AccountInactivityLimit set to 10. Account will be inactivated if not accessed in 10 secs')
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)
    log.info('Sleep for 9 secs to check if account is not inactivated, expected value 0')
    time.sleep(9)
    log.info('Account should not be inactivated since AccountInactivityLimit not exceeded')
    account_status(topology_st, suffix, subtree, userid, 3, 2, "Enabled")
    log.info('Sleep for 2 more secs to check if account is inactivated')
    time.sleep(2)
    account_status(topology_st, suffix, subtree, userid, 2, 0, "Disabled")
    log.info('Sleep +9 secs to check if account {}3 is inactivated'.format(userid))
    time.sleep(9)
    account_status(topology_st, suffix, subtree, userid, 3, 2, "Disabled")
    log.info('Add lastLoginTime attribute to all users and check if its activated')
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)


#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_locinact_modrdn(topology_st, accpol_local):
    """Verify if user account is inactivated when moved from ou=groups to ou=people subtree.

    :id: 5f25bea3-fab0-4db4-b43d-2d47cc6e5ad1
    :setup: Standalone instance, ou=people subtree configured for Local account
            policy plugin configuration, set accountInactivityLimit to few secs.
    :steps:
        1. Add few users to ou=groups subtree in the default suffix
        2. Plugin configured to ou=people subtree only.
        3. Wait for few secs before it reaches accountInactivityLimit and check users.
        4. Run ldapsearch as normal user, expected 0
        5. Wait till accountInactivityLimit exceeded
        6. Move users from ou=groups subtree to ou=people subtree
        7. Check if users are inactivated, expected error 19
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Should return error code 0 and 19
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=groups"
    userid = "nolockusr"
    nousrs = 1
    log.info('Account should not be inactivated since the subtree is not configured')
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)
    log.info('Sleep for 11 secs to check if account is not inactivated, expected value 0')
    time.sleep(11)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    log.info('Moving users from ou=groups to ou=people subtree')
    user = UserAccount(topology_st.standalone, dn='uid=nolockusr1,ou=groups,dc=example,dc=com')
    try:
        user.rename('uid=nolockusr1', newsuperior='ou=people,dc=example,dc=com')
    except ldap.LDAPError as e:
        log.error('Failed to move user uid=nolockusr1 from ou=groups to ou=people')
        raise e
    subtree = "ou=people"
    log.info('Then wait for 11 secs and check if entries are inactivated')
    time.sleep(11)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    add_time_attr(topology_st, suffix, subtree, userid, nousrs, 'lastLoginTime')
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Enabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)


def test_locact_modrdn(topology_st, accpol_local):
    """Verify if user account is inactivated when users moved from ou=people to ou=groups subtree.

    :id: e821cbae-bfc3-40d3-947d-b228c809987f
    :setup: Standalone instance, ou=people subtree configured for Local account
            policy plugin configuration, set accountInactivityLimit to few secs.
    :steps:
        1. Add few users to ou=people subtree in the default suffix
        2. Wait for few secs and check if users not inactivated, expected 0.
        3. Move users from ou=people to ou=groups subtree
        4. Wait till accountInactivityLimit is exceeded
        5. Check if users are active in ou=groups subtree, expected 0
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    suffix = DEFAULT_SUFFIX
    subtree = "ou=people"
    userid = "lockusr"
    nousrs = 1
    log.info('Account should be inactivated since the subtree is configured')
    add_users(topology_st, suffix, subtree, userid, nousrs, 0)
    log.info('Sleep for 11 secs to check if account is inactivated, expected value 19')
    time.sleep(11)
    account_status(topology_st, suffix, subtree, userid, nousrs, 0, "Disabled")
    log.info('Moving users from ou=people to ou=groups subtree')
    user = UserAccount(topology_st.standalone, dn='uid=lockusr1,ou=people,dc=example,dc=com')
    try:
        user.rename('uid=lockusr1', newsuperior='ou=groups,dc=example,dc=com')
    except ldap.LDAPError as e:
        log.error('Failed to move user uid=lockusr1 from ou=people to ou=groups')
        raise e
    log.info('Sleep for +2 secs and check users from both ou=people and ou=groups subtree')
    time.sleep(2)
    subtree = "ou=groups"
    account_status(topology_st, suffix, subtree, userid, 1, 0, "Enabled")
    del_users(topology_st, suffix, subtree, userid, nousrs)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
