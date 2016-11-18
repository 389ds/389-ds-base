import os
import sys
import time
import ldap
import logging
import pytest
import ldif
import ldap.modlist as modlist
from ldif import LDIFParser, LDIFWriter
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

LOCAL_CONFIG = 'cn=AccountPolicy1,ou=people,dc=example,dc=com'
TEMPLT_COS = 'cn=TempltCoS,ou=people,dc=example,dc=com'
DEFN_COS = 'cn=DefnCoS,ou=people,dc=example,dc=com'
ACCPOL_DN = "cn={},{}".format(PLUGIN_ACCT_POLICY, DN_PLUGIN)
CONFIG_DN = "cn=config,{}".format(ACCPOL_DN)
SUBTREE = 'ou=people'
SUFFIX = DEFAULT_SUFFIX
USR_NAME = 'testusr'
NOF_USERS = 5
INACT_VAL = 15
USR_RDN = '{}'.format(USR_NAME)
USR_DN = 'uid={},{},{}'.format(USR_RDN, SUBTREE, SUFFIX)
USER_PW = 'Secret1234'


@pytest.fixture(scope="module")
def accpolicy_local(topology_st):
    """Configure account policy plugin based
    on LDIF file and restart the server.
    """

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
        log.error("Failed to modify account policy plugin attrs attrs")
        raise

    log.info("Adding Local account policy plugin configuration entries")
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


@pytest.fixture(scope="module")
def users(topology_st, request):
    """Add users to the given SUFFIX and SUBTREE."""

    log.info('Adding {} {} users to {} SUBTREE {} SUFFIX'.format(NOF_USERS, USR_NAME, SUBTREE, SUFFIX))
    for NUM in range(1, NOF_USERS):
        USR_RDN = '{}{}'.format(USR_NAME, NUM)
        USR_DN = 'uid={},{},{}'.format(USR_RDN, SUBTREE, SUFFIX)
        try:
            topology_st.standalone.add_s(Entry((USR_DN, {
                'objectclass': 'top person'.split(),
                'objectclass': 'inetorgperson',
                'cn': USR_RDN,
                'sn': USR_RDN,
                'userpassword': 'Secret1234',
                'mail': '{}@redhat.com'.format(USR_RDN)})))
        except ldap.LDAPError as e:
            log.error('Failed to add {} user: error {}'.format(USR_DN, e.message['desc']))
            raise

    def fin():
        log.info('Deleting {} {} users from {} {}'.format(NOF_USERS, USR_NAME, SUBTREE, SUFFIX))
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        for NUM in range(1, NOF_USERS):
            USR_RDN = '{}{}'.format(USR_NAME, NUM)
            USR_DN = 'uid={},{},{}'.format(USR_RDN, SUBTREE, SUFFIX)
            try:
                topology_st.standalone.delete_s(USR_DN)
            except ldap.LDAPError as e:
                log.error('Failed to delete {} :error- {}'.format(USR_DN, e.message['desc']))
                raise

    request.addfinalizer(fin)


def test_inact_plugin(topology_st, accpolicy_local, users):
    """Verify if user account is inactivated when accountInactivityLimit is exceeded.
    User is created in the default SUFFIX.

    :Feature: Account Policy Plugin

    :Setup: Standalone instance, Local account policy plugin configuration,
            accountInactivityLimit set to 15, Inactivate account by Account policy plugin

    :Steps: 1. Configure account policy plugin with accpol_local for ou=people SUBTREE
	    2. Set accountInactivityLimit to 15
	    3. Add few users to ou=people SUBTREE in the default SUFFIX
	    4. Wait for 12 secs and run ldapsearch as normal user to check if its not inactivated, expected 0.
	    5. Wait for 3 secs or till accountInactivityLimit is exceeded
	    6. Run ldapsearch as normal user and check if its inactivated, expected error 19.

    :Assert: Should return error code 19
    """

    log.info("AccountInactivityLimit set to 15. Account will be inactivated if not accessed in 15 secs")
    log.info("Sleeping for 12 secs to check if account is not inactivated, expected value 0")
    time.sleep(12)
    for NUM in range(2, NOF_USERS):
        USR_RDN = '{}{}'.format(USR_NAME, NUM)
        USR_DN = 'uid={},{},{}'.format(USR_RDN, SUBTREE, SUFFIX)
        try:
            topology_st.standalone.simple_bind_s(USR_DN, USER_PW)
        except ldap.LDAPError as e:
            log.error('Checking if {} is inactivated: error {}'.format(USR_DN, e.message['desc']))
            raise

    USR_DN = 'uid={}1,{},{}'.format(USR_NAME, SUBTREE, SUFFIX)
    log.info("Sleeping for 4 more secs to check if {} is inactivated, expected error 19".format(USR_DN))
    time.sleep(4)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION) as e:
        topology_st.standalone.simple_bind_s(USR_DN, USER_PW)

    USR_DN = 'uid={}2,{},{}'.format(USR_NAME, SUBTREE, SUFFIX)
    log.info("Checking if {} is not inactivated, expected value 0".format(USR_DN))
    try:
        topology_st.standalone.simple_bind_s(USR_DN, USER_PW)
    except ldap.LDAPError as e:
        log.error('Checking if {} is inactivated : error {}'.format(USR_DN, e.message['desc']))
        raise
    time.sleep(12)
    for NUM in range(3, NOF_USERS):
        USR_RDN = '{}{}'.format(USR_NAME, NUM)
        USR_DN = 'uid={},{},{}'.format(USR_RDN, SUBTREE, SUFFIX)
        log.info("Checking if {} is inactivated, expected error 19".format(USR_DN))
        with pytest.raises(ldap.CONSTRAINT_VIOLATION) as e:
            topology_st.standalone.simple_bind_s(USR_DN, USER_PW)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
