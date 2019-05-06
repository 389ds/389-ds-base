# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st

log = logging.getLogger(__name__)

from lib389.utils import *

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.3'), reason="Not implemented")]
ACCT_POLICY_CONFIG_DN = ('cn=config,cn=%s,cn=plugins,cn=config' %
                         PLUGIN_ACCT_POLICY)
ACCT_POLICY_DN = 'cn=Account Inactivation Policy,%s' % SUFFIX
# Set inactivty high to prevent timing issues with debug options or gdb on test runs.
INACTIVITY_LIMIT = '3000'
SEARCHFILTER = '(objectclass=*)'

TEST_USER = 'ticket47714user'
TEST_USER_DN = 'uid=%s,%s' % (TEST_USER, SUFFIX)
TEST_USER_PW = '%s' % TEST_USER


def _header(topology_st, label):
    topology_st.standalone.log.info("\n\n###############################################")
    topology_st.standalone.log.info("#######")
    topology_st.standalone.log.info("####### %s" % label)
    topology_st.standalone.log.info("#######")
    topology_st.standalone.log.info("###############################################")


def test_ticket47714_init(topology_st):
    """
    1. Add account policy entry to the DB
    2. Add a test user to the DB
    """
    _header(topology_st,
            'Testing Ticket 47714 - [RFE] Update lastLoginTime also in Account Policy plugin if account lockout is based on passwordExpirationTime.')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    log.info("\n######################### Adding Account Policy entry: %s ######################\n" % ACCT_POLICY_DN)
    topology_st.standalone.add_s(
        Entry((ACCT_POLICY_DN, {'objectclass': "top ldapsubentry extensibleObject accountpolicy".split(),
                                'accountInactivityLimit': INACTIVITY_LIMIT})))

    log.info("\n######################### Adding Test User entry: %s ######################\n" % TEST_USER_DN)
    topology_st.standalone.add_s(
        Entry((TEST_USER_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                              'cn': TEST_USER,
                              'sn': TEST_USER,
                              'givenname': TEST_USER,
                              'userPassword': TEST_USER_PW,
                              'acctPolicySubentry': ACCT_POLICY_DN})))


def test_ticket47714_run_0(topology_st):
    """
    Check this change has no inpact to the existing functionality.
    1. Set account policy config without the new attr alwaysRecordLoginAttr
    2. Bind as a test user
    3. Bind as the test user again and check the lastLoginTime is updated
    4. Waint longer than the accountInactivityLimit time and bind as the test user,
       which should fail with CONSTANT_VIOLATION.
    """
    _header(topology_st, 'Account Policy - No new attr alwaysRecordLoginAttr in config')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # Modify Account Policy config entry
    topology_st.standalone.modify_s(ACCT_POLICY_CONFIG_DN, [(ldap.MOD_REPLACE, 'alwaysrecordlogin', b'yes'),
                                                            (ldap.MOD_REPLACE, 'stateattrname', b'lastLoginTime'),
                                                            (ldap.MOD_REPLACE, 'altstateattrname', b'createTimestamp'),
                                                            (ldap.MOD_REPLACE, 'specattrname', b'acctPolicySubentry'),
                                                            (ldap.MOD_REPLACE, 'limitattrname',
                                                             b'accountInactivityLimit')])

    # Enable the plugins
    topology_st.standalone.plugins.enable(name=PLUGIN_ACCT_POLICY)

    topology_st.standalone.restart()

    log.info("\n######################### Bind as %s ######################\n" % TEST_USER_DN)
    try:
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.error('CONSTRAINT VIOLATION {}'.format(e.args[0]['desc']))

    time.sleep(2)

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    entry = topology_st.standalone.search_s(TEST_USER_DN, ldap.SCOPE_BASE, SEARCHFILTER, ['lastLoginTime'])

    lastLoginTime0 = entry[0].lastLoginTime

    log.info("\n######################### Bind as %s again ######################\n" % TEST_USER_DN)
    try:
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.error('CONSTRAINT VIOLATION {}'.format(e.args[0]['desc']))

    time.sleep(2)

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    entry = topology_st.standalone.search_s(TEST_USER_DN, ldap.SCOPE_BASE, SEARCHFILTER, ['lastLoginTime'])

    lastLoginTime1 = entry[0].lastLoginTime

    log.info("First lastLoginTime: %s, Second lastLoginTime: %s" % (lastLoginTime0, lastLoginTime1))
    assert lastLoginTime0 < lastLoginTime1

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # Now, change the inactivity limit, because that should trigger the account to now be locked. This is possible because the check is "delayed" until the usage of the account.

    topology_st.standalone.modify_s(ACCT_POLICY_DN, [(ldap.MOD_REPLACE, 'accountInactivityLimit', b'1'),])
    time.sleep(2)

    entry = topology_st.standalone.search_s(ACCT_POLICY_DN, ldap.SCOPE_BASE, SEARCHFILTER)
    log.info("\n######################### %s ######################\n" % ACCT_POLICY_CONFIG_DN)
    log.info("accountInactivityLimit: %s" % entry[0].accountInactivityLimit)
    log.info("\n######################### %s DONE ######################\n" % ACCT_POLICY_CONFIG_DN)

    log.info("\n######################### Bind as %s again to fail ######################\n" % TEST_USER_DN)
    try:
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.info('CONSTRAINT VIOLATION {}'.format(e.args[0]['desc']))
        log.info("%s was successfully inactivated." % TEST_USER_DN)
        pass

    # Now reset the value high to prevent issues with the next test.
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ACCT_POLICY_DN, [(ldap.MOD_REPLACE, 'accountInactivityLimit', ensure_bytes(INACTIVITY_LIMIT)),])


def test_ticket47714_run_1(topology_st):
    """
    Verify a new config attr alwaysRecordLoginAttr
    1. Set account policy config with the new attr alwaysRecordLoginAttr: lastLoginTime
       Note: bogus attr is set to stateattrname.
             altstateattrname type value is used for checking whether the account is idle or not.
    2. Bind as a test user
    3. Bind as the test user again and check the alwaysRecordLoginAttr: lastLoginTime is updated
    """
    _header(topology_st, 'Account Policy - With new attr alwaysRecordLoginAttr in config')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(TEST_USER_DN, [(ldap.MOD_DELETE, 'lastLoginTime', None)])

    # Modify Account Policy config entry
    topology_st.standalone.modify_s(ACCT_POLICY_CONFIG_DN, [(ldap.MOD_REPLACE, 'alwaysrecordlogin', b'yes'),
                                                            (ldap.MOD_REPLACE, 'stateattrname', b'bogus'),
                                                            (ldap.MOD_REPLACE, 'altstateattrname', b'modifyTimestamp'),
                                                            (
                                                            ldap.MOD_REPLACE, 'alwaysRecordLoginAttr', b'lastLoginTime'),
                                                            (ldap.MOD_REPLACE, 'specattrname', b'acctPolicySubentry'),
                                                            (ldap.MOD_REPLACE, 'limitattrname',
                                                             b'accountInactivityLimit')])

    # Enable the plugins
    topology_st.standalone.plugins.enable(name=PLUGIN_ACCT_POLICY)

    topology_st.standalone.restart()

    log.info("\n######################### Bind as %s ######################\n" % TEST_USER_DN)
    try:
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.error('CONSTRAINT VIOLATION {}'.format(e.args[0]['desc']))

    time.sleep(1)

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    entry = topology_st.standalone.search_s(TEST_USER_DN, ldap.SCOPE_BASE, SEARCHFILTER, ['lastLoginTime'])
    lastLoginTime0 = entry[0].lastLoginTime

    log.info("\n######################### Bind as %s again ######################\n" % TEST_USER_DN)
    try:
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.error('CONSTRAINT VIOLATION {}'.format(e.args[0]['desc']))

    time.sleep(1)

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    entry = topology_st.standalone.search_s(TEST_USER_DN, ldap.SCOPE_BASE, SEARCHFILTER, ['lastLoginTime'])
    lastLoginTime1 = entry[0].lastLoginTime

    log.info("First lastLoginTime: %s, Second lastLoginTime: %s" % (lastLoginTime0, lastLoginTime1))
    assert lastLoginTime0 < lastLoginTime1

    topology_st.standalone.log.info("ticket47714 was successfully verified.")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
