# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
import shutil
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *

log = logging.getLogger(__name__)

ACCT_POLICY_CONFIG_DN = ('cn=config,cn=%s,cn=plugins,cn=config' %
                         PLUGIN_ACCT_POLICY)
ACCT_POLICY_DN = 'cn=Account Inactivation Policy,%s' % SUFFIX
INACTIVITY_LIMIT = '9'
SEARCHFILTER = '(objectclass=*)'

TEST_USER = 'ticket47714user'
TEST_USER_DN = 'uid=%s,%s' % (TEST_USER, SUFFIX)
TEST_USER_PW = '%s' % TEST_USER


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
    '''
    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def _header(topology, label):
    topology.standalone.log.info("\n\n###############################################")
    topology.standalone.log.info("#######")
    topology.standalone.log.info("####### %s" % label)
    topology.standalone.log.info("#######")
    topology.standalone.log.info("###############################################")


def test_ticket47714_init(topology):
    """
    1. Add account policy entry to the DB
    2. Add a test user to the DB
    """
    _header(topology, 'Testing Ticket 47714 - [RFE] Update lastLoginTime also in Account Policy plugin if account lockout is based on passwordExpirationTime.')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    log.info("\n######################### Adding Account Policy entry: %s ######################\n" % ACCT_POLICY_DN)
    topology.standalone.add_s(Entry((ACCT_POLICY_DN, {'objectclass': "top ldapsubentry extensibleObject accountpolicy".split(),
                                                      'accountInactivityLimit': INACTIVITY_LIMIT})))

    log.info("\n######################### Adding Test User entry: %s ######################\n" % TEST_USER_DN)
    topology.standalone.add_s(Entry((TEST_USER_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                    'cn': TEST_USER,
                                                    'sn': TEST_USER,
                                                    'givenname': TEST_USER,
                                                    'userPassword': TEST_USER_PW,
                                                    'acctPolicySubentry': ACCT_POLICY_DN})))


def test_ticket47714_run_0(topology):
    """
    Check this change has no inpact to the existing functionality.
    1. Set account policy config without the new attr alwaysRecordLoginAttr
    2. Bind as a test user
    3. Bind as the test user again and check the lastLoginTime is updated
    4. Waint longer than the accountInactivityLimit time and bind as the test user,
       which should fail with CONSTANT_VIOLATION.
    """
    _header(topology, 'Account Policy - No new attr alwaysRecordLoginAttr in config')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    # Modify Account Policy config entry
    topology.standalone.modify_s(ACCT_POLICY_CONFIG_DN, [(ldap.MOD_REPLACE, 'alwaysrecordlogin', 'yes'),
                                                         (ldap.MOD_REPLACE, 'stateattrname', 'lastLoginTime'),
                                                         (ldap.MOD_REPLACE, 'altstateattrname', 'createTimestamp'),
                                                         (ldap.MOD_REPLACE, 'specattrname', 'acctPolicySubentry'),
                                                         (ldap.MOD_REPLACE, 'limitattrname', 'accountInactivityLimit')])

    # Enable the plugins
    topology.standalone.plugins.enable(name=PLUGIN_ACCT_POLICY)

    topology.standalone.restart(timeout=120)

    log.info("\n######################### Bind as %s ######################\n" % TEST_USER_DN)
    try:
        topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.error('CONSTRAINT VIOLATION ' + e.message['desc'])

    time.sleep(2)

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    entry = topology.standalone.search_s(TEST_USER_DN, ldap.SCOPE_BASE, SEARCHFILTER, ['lastLoginTime'])

    lastLoginTime0 = entry[0].lastLoginTime

    log.info("\n######################### Bind as %s again ######################\n" % TEST_USER_DN)
    try:
        topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.error('CONSTRAINT VIOLATION ' + e.message['desc'])

    time.sleep(2)

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    entry = topology.standalone.search_s(TEST_USER_DN, ldap.SCOPE_BASE, SEARCHFILTER, ['lastLoginTime'])

    lastLoginTime1 = entry[0].lastLoginTime

    log.info("First lastLoginTime: %s, Second lastLoginTime: %s" % (lastLoginTime0, lastLoginTime1))
    assert lastLoginTime0 < lastLoginTime1

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    entry = topology.standalone.search_s(ACCT_POLICY_DN, ldap.SCOPE_BASE, SEARCHFILTER)
    log.info("\n######################### %s ######################\n" % ACCT_POLICY_CONFIG_DN)
    log.info("accountInactivityLimit: %s" % entry[0].accountInactivityLimit)
    log.info("\n######################### %s DONE ######################\n" % ACCT_POLICY_CONFIG_DN)

    log.info("\n######################### Bind as %s again to fail ######################\n" % TEST_USER_DN)
    try:
        topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.info('CONSTRAINT VIOLATION ' + e.message['desc'])
        log.info("%s was successfully inactivated." % TEST_USER_DN)
        pass


def test_ticket47714_run_1(topology):
    """
    Verify a new config attr alwaysRecordLoginAttr
    1. Set account policy config with the new attr alwaysRecordLoginAttr: lastLoginTime
       Note: bogus attr is set to stateattrname.
             altstateattrname type value is used for checking whether the account is idle or not.
    2. Bind as a test user
    3. Bind as the test user again and check the alwaysRecordLoginAttr: lastLoginTime is updated
    """
    _header(topology, 'Account Policy - With new attr alwaysRecordLoginAttr in config')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(TEST_USER_DN, [(ldap.MOD_DELETE, 'lastLoginTime', None)])

    # Modify Account Policy config entry
    topology.standalone.modify_s(ACCT_POLICY_CONFIG_DN, [(ldap.MOD_REPLACE, 'alwaysrecordlogin', 'yes'),
                                                         (ldap.MOD_REPLACE, 'stateattrname', 'bogus'),
                                                         (ldap.MOD_REPLACE, 'altstateattrname', 'modifyTimestamp'),
                                                         (ldap.MOD_REPLACE, 'alwaysRecordLoginAttr', 'lastLoginTime'),
                                                         (ldap.MOD_REPLACE, 'specattrname', 'acctPolicySubentry'),
                                                         (ldap.MOD_REPLACE, 'limitattrname', 'accountInactivityLimit')])

    # Enable the plugins
    topology.standalone.plugins.enable(name=PLUGIN_ACCT_POLICY)

    topology.standalone.restart(timeout=120)

    log.info("\n######################### Bind as %s ######################\n" % TEST_USER_DN)
    try:
        topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.error('CONSTRAINT VIOLATION ' + e.message['desc'])

    time.sleep(1)

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    entry = topology.standalone.search_s(TEST_USER_DN, ldap.SCOPE_BASE, SEARCHFILTER, ['lastLoginTime'])
    lastLoginTime0 = entry[0].lastLoginTime

    log.info("\n######################### Bind as %s again ######################\n" % TEST_USER_DN)
    try:
        topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.error('CONSTRAINT VIOLATION ' + e.message['desc'])

    time.sleep(1)

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    entry = topology.standalone.search_s(TEST_USER_DN, ldap.SCOPE_BASE, SEARCHFILTER, ['lastLoginTime'])
    lastLoginTime1 = entry[0].lastLoginTime

    log.info("First lastLoginTime: %s, Second lastLoginTime: %s" % (lastLoginTime0, lastLoginTime1))
    assert lastLoginTime0 < lastLoginTime1

    topology.standalone.log.info("ticket47714 was successfully verified.")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
