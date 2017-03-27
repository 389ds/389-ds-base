# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

LOCAL_CONFIG = 'cn=AccountPolicy1,ou=people,dc=example,dc=com'
TEMPLT_COS = 'cn=TempltCoS,ou=people,dc=example,dc=com'
DEFN_COS = 'cn=DefnCoS,ou=people,dc=example,dc=com'
ACCPOL_DN = "cn={},{}".format(PLUGIN_ACCT_POLICY, DN_PLUGIN)
CONFIG_DN = "cn=config,{}".format(ACCPOL_DN)
INACT_VAL = 15
USER_PW = 'Secret1234'


@pytest.fixture(scope="module")
def accpolicy_local(topology_st):
    """Configure local account policy plugin for ou=people subtree and restart the server"""

    log.info('Enabling account policy plugin and restarting the server')
    try:
        topology_st.standalone.plugins.enable(name=PLUGIN_ACCT_POLICY)
        topology_st.standalone.modify_s(ACCPOL_DN, [(ldap.MOD_REPLACE, 'nsslapd-pluginarg0', CONFIG_DN)])
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'alwaysrecordlogin', 'yes')])
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'stateattrname', 'lastLoginTime')])
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'altstateattrname', 'createTimestamp')])
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'specattrname', 'acctPolicySubentry')])
        topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'limitattrname', 'accountInactivityLimit')])
    except ldap.LDAPError as e:
        log.error('Failed to modify account policy plugin attrs')
        raise

    log.info('Adding Local account policy plugin configuration entries')
    try:
        topology_st.standalone.add_s(Entry((LOCAL_CONFIG, {
            'objectclass': ['top', 'ldapsubentry', 'extensibleObject', 'accountpolicy'],
            'accountInactivityLimit': '15'})))
        topology_st.standalone.add_s(Entry((TEMPLT_COS, {
            'objectclass': ['top', 'ldapsubentry', 'extensibleObject', 'cosTemplate'],
            'acctPolicySubentry': LOCAL_CONFIG})))
        topology_st.standalone.add_s(Entry((DEFN_COS, {
            'objectclass': ['top', 'ldapsubentry', 'cosSuperDefinition', 'cosPointerDefinition'],
            'cosTemplateDn': TEMPLT_COS,
            'cosAttribute': 'acctPolicySubentry default operational-default'})))
    except ldap.LDAPError as e:
        log.error('Failed to add entry ({}, {}, {}):'.format(LOCAL_CONFIG, TEMPLT_COS, DEFN_COS))
        raise
    topology_st.standalone.restart(timeout=10)


def add_users(topology_st, suffix, subtre, userid, nousrs):
    """Add users to default test instance with given suffix, subtre, userid and nousrs"""

    log.info('add_users: Pass all of these as parameteres suffix, subtre, userid and nousrs')
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    while (nousrs > 0):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtre, suffix)
        try:
            topology_st.standalone.add_s(Entry((userdn, {
                'objectclass': 'top person'.split(),
                'objectclass': 'inetorgperson',
                'cn': usrrdn,
                'sn': usrrdn,
                'userpassword': USER_PW,
                'mail': '{}@redhat.com'.format(usrrdn)})))
        except ldap.LDAPError as e:
            log.error('Failed to add {} user: error {}'.format(userdn, e.message['desc']))
            raise
        nousrs = nousrs - 1


def del_users(topology_st, suffix, subtre, userid, nousrs):
    """Delete users from default test instance with given suffix, subtre, userid and nousrs"""

    log.info('del_users: Pass all of these as parameteres suffix, subtre, userid and nousrs')
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    while (nousrs > 0):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtre, suffix)
        try:
            topology_st.standalone.delete_s(userdn)
        except ldap.LDAPError as e:
            log.error('Failed to delete {} user: error {}'.format(userdn, e.message['desc']))
            raise
        nousrs = nousrs - 1


def account_status(topology_st, suffix, subtre, userid, nousrs, ulimit, tochck):
    """Check account status for the given suffix, subtre, userid and nousrs"""

    while (nousrs > ulimit):
        usrrdn = '{}{}'.format(userid, nousrs)
        userdn = 'uid={},{},{}'.format(usrrdn, subtre, suffix)
        if (tochck == "Enabled"):
            try:
                topology_st.standalone.simple_bind_s(userdn, USER_PW)
            except ldap.LDAPError as e:
                log.error('User {} is inactivated, expected 0 : error {}'.format(userdn, e.message['desc']))
                raise
        elif (tochck == "Disabled"):
            with pytest.raises(ldap.CONSTRAINT_VIOLATION):
                topology_st.standalone.simple_bind_s(userdn, USER_PW)
                log.error('User {} is not inactivated, expected error 19'.format(userdn))
        nousrs = nousrs - 1


def test_actNinact_local(topology_st, accpolicy_local):
    """Verify if user account is inactivated when accountInactivityLimit is exceeded. User is created in the default suffix.

    :ID: 470f480c-da55-47ee-9095-5ff50a6d5be1
    :feature: Account Policy Plugin
    :setup: Standalone instance, Local account policy plugin configuration,
            accountInactivityLimit set to 15, Inactivate account by Account policy plugin
    :steps: 1. Configure account policy plugin with accpol_local for ou=people subtree
            2. Set accountInactivityLimit to 15
            3. Add few users to ou=people subtre in the default suffix
            4. Wait for 14 secs and run ldapsearch as normal user to check if its not inactivated, expected 0.
            5. Wait for 2 secs or till accountInactivityLimit is exceeded
            6. Run ldapsearch as normal user and check if its inactivated, expected error 19.
            7. Sleep for +14 secs to check if accounts accessed at step4 are inactivated now
    :expectedresults: Should return error code 19
    """

    suffix = DEFAULT_SUFFIX
    subtre = "ou=people"
    userid = "inactusr"
    userpw = USER_PW
    nousrs = 3
    log.info('AccountInactivityLimit set to 15. Account will be inactivated if not accessed in 15 secs')
    add_users(topology_st, suffix, subtre, userid, nousrs)
    log.info('Sleeping for 14 secs to check account is not inactivated, expected value 0')
    time.sleep(14)
    log.info('Account should not be inactivated since AccountInactivityLimit not exceeded')
    account_status(topology_st, suffix, subtre, userid, 3, 2, "Enabled")
    log.info('Sleeping for 2 more secs to check if account is inactivated')
    time.sleep(2)
    account_status(topology_st, suffix, subtre, userid, 2, 0, "Disabled")
    log.info('Sleeping +14 secs to check if account {}3 is inactivated'.format(userid))
    time.sleep(14)
    account_status(topology_st, suffix, subtre, userid, 3, 2, "Disabled")
    del_users(topology_st, suffix, subtre, userid, nousrs)


def test_noinact_local(topology_st, accpolicy_local):
    """Verify if user account is inactivated when moved from ou=groups to ou=people subtree.

    :ID: 235e3f00-db20-4166-9cfd-77e7c08cabdf
    :feature: Account Policy Plugin
    :setup: Standalone instance, Local account policy plugin configuration,
            accountInactivityLimit set to 15, Inactivate account by Account policy plugin
    :steps: 1. Add few users to ou=groups subtre in the default suffix, plugin configured to ou=people subtree only.
            2. Wait for 16 secs and run ldapsearch as normal user to check account is active, expected 0.
            3. Move users from ou=groups to ou=people subtree
            4. Sleep for 16 secs and check if entries are inactivated
    :expectedresults: Should return error code 0 and 19
    """

    suffix = DEFAULT_SUFFIX
    subtre = "ou=groups"
    userid = "nolockusr"
    userpw = USER_PW
    nousrs = 1
    log.info('Account should not be inactivated since the subtree is not configured')
    add_users(topology_st, suffix, subtre, userid, nousrs)
    log.info('Sleeping for 16 secs to check if account is not inactivated, expected value 0')
    time.sleep(16)
    account_status(topology_st, suffix, subtre, userid, nousrs, 0, "Enabled")
    log.info('Moving users from ou=groups to ou=people subtree')
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.rename_s('uid=nolockusr1,ou=groups,dc=example,dc=com', 'uid=nolockusr1',
                                        'ou=people,dc=example,dc=com')
    except ldap.LDAPError as e:
        log.error('Failed to move user uid=nolockusr1 from ou=groups to ou=people')
        raise
    subtre = "ou=people"
    log.info('Then wait for 16 secs and check if entries are inactivated')
    time.sleep(16)
    account_status(topology_st, suffix, subtre, userid, nousrs, 0, "Disabled")
    del_users(topology_st, suffix, subtre, userid, nousrs)


def test_inact_local(topology_st, accpolicy_local):
    """Verify if user account is inactivated when users moved from ou=people to ou=groups subtree.

    :ID: ffccf0fd-b684-4462-87ef-1ec6d3e40574
    :feature: Account Policy Plugin
    :setup: Standalone instance, Local account policy plugin configuration,
            accountInactivityLimit set to 15, Inactivate account by Account policy plugin
    :steps: 1. Add few users to ou=people subtre in the default suffix
            2. Wait for 14 secs and run ldapsearch as normal user to check if its not inactivated, expected 0.
            3. Move users from ou=people to ou=groups subtree
            4. Sleep for +2 secs and check if users are inactivated in ou=people subtree
            5. Check if users are not inactivated in ou=groups subtree
    :expectedresults: Should return error code 0
    """

    suffix = DEFAULT_SUFFIX
    subtre = "ou=people"
    userid = "lockusr"
    userpw = USER_PW
    nousrs = 1
    log.info('Account should be inactivated since the subtree is configured')
    add_users(topology_st, suffix, subtre, userid, nousrs)
    log.info('Sleeping for 16 secs to check if account is inactivated, expected value 19')
    time.sleep(14)
    account_status(topology_st, suffix, subtre, userid, nousrs, 0, "Enabled")
    log.info('Moving users from ou=people to ou=groups subtree')
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.rename_s('uid=lockusr1,ou=people,dc=example,dc=com', 'uid=lockusr1',
                                        'ou=groups,dc=example,dc=com')
    except ldap.LDAPError as e:
        log.error('Failed to move user uid=lockusr1 from ou=groups to ou=people')
        raise
    log.info('Sleep for +2 secs and check users from both ou=people and ou=groups subtree')
    time.sleep(2)
    subtre = "ou=groups"
    account_status(topology_st, suffix, subtre, userid, 1, 0, "Enabled")
    del_users(topology_st, suffix, subtre, userid, nousrs)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
