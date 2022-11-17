# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
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

from lib389._constants import (PLUGIN_ACCT_POLICY, DEFAULT_SUFFIX, DN_DM, PASSWORD, SUFFIX,
                              BACKEND_NAME)

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv('DEBUGGING', False)

RDN_LONG_SUFFIX = 'this'
LONG_SUFFIX = "dc=%s,dc=is,dc=a,dc=very,dc=long,dc=suffix,dc=so,dc=long,dc=suffix,dc=extremely,dc=long,dc=suffix" % RDN_LONG_SUFFIX
LONG_SUFFIX_BE = 'ticket48956'

ACCT_POLICY_PLUGIN_DN = 'cn=%s,cn=plugins,cn=config' % PLUGIN_ACCT_POLICY
ACCT_POLICY_CONFIG_DN = 'cn=config,%s' % ACCT_POLICY_PLUGIN_DN

INACTIVITY_LIMIT = '9'
SEARCHFILTER = '(objectclass=*)'

TEST_USER = 'ticket48956user'
TEST_USER_PW = '%s' % TEST_USER

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


def _check_status(topology_st, user, expected):
    nsaccountstatus = os.path.join(topology_st.standalone.ds_paths.sbin_dir, "ns-accountstatus.pl")

    try:
        output = subprocess.check_output([nsaccountstatus, '-Z', topology_st.standalone.serverid,
                                          '-D', DN_DM, '-w', PASSWORD,
                                          '-p', str(topology_st.standalone.port), '-I', user])
    except subprocess.CalledProcessError as err:
        output = err.output

    log.info("output: %s" % output)

    if expected in output:
        return True
    return False


def _check_inactivity(topology_st, mysuffix):
    ACCT_POLICY_DN = 'cn=Account Inactivation Policy,%s' % mysuffix
    log.info("\n######################### Adding Account Policy entry: %s ######################\n" % ACCT_POLICY_DN)
    topology_st.standalone.add_s(
        Entry((ACCT_POLICY_DN, {'objectclass': "top ldapsubentry extensibleObject accountpolicy".split(),
                                'accountInactivityLimit': INACTIVITY_LIMIT})))
    time.sleep(1)

    TEST_USER_DN = 'uid=%s,%s' % (TEST_USER, mysuffix)
    log.info("\n######################### Adding Test User entry: %s ######################\n" % TEST_USER_DN)
    topology_st.standalone.add_s(
        Entry((TEST_USER_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                              'cn': TEST_USER,
                              'sn': TEST_USER,
                              'givenname': TEST_USER,
                              'userPassword': TEST_USER_PW,
                              'acctPolicySubentry': ACCT_POLICY_DN})))
    time.sleep(1)

    # Setting the lastLoginTime
    try:
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.error('CONSTRAINT VIOLATION ' + e.message['desc'])
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    assert (_check_status(topology_st, TEST_USER_DN, b'- activated'))

    time.sleep(int(INACTIVITY_LIMIT) + 5)
    assert (_check_status(topology_st, TEST_USER_DN, b'- inactivated (inactivity limit exceeded'))


def test_ticket48956(topology_st):
    """Write your testcase here...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).

    """

    topology_st.standalone.modify_s(ACCT_POLICY_PLUGIN_DN,
                                    [(ldap.MOD_REPLACE, 'nsslapd-pluginarg0', ensure_bytes(ACCT_POLICY_CONFIG_DN))])

    topology_st.standalone.modify_s(ACCT_POLICY_CONFIG_DN, [(ldap.MOD_REPLACE, 'alwaysrecordlogin', b'yes'),
                                                            (ldap.MOD_REPLACE, 'stateattrname', b'lastLoginTime'),
                                                            (ldap.MOD_REPLACE, 'altstateattrname', b'createTimestamp'),
                                                            (ldap.MOD_REPLACE, 'specattrname', b'acctPolicySubentry'),
                                                            (ldap.MOD_REPLACE, 'limitattrname',
                                                             b'accountInactivityLimit')])

    # Enable the plugins
    topology_st.standalone.plugins.enable(name=PLUGIN_ACCT_POLICY)
    topology_st.standalone.restart(timeout=10)

    # Check inactivity on standard suffix (short)
    _check_inactivity(topology_st, SUFFIX)

    # Check inactivity on a long suffix
    topology_st.standalone.backend.create(LONG_SUFFIX, {BACKEND_NAME: LONG_SUFFIX_BE})
    topology_st.standalone.mappingtree.create(LONG_SUFFIX, bename=LONG_SUFFIX_BE)
    topology_st.standalone.add_s(Entry((LONG_SUFFIX, {
        'objectclass': "top domain".split(),
        'dc': RDN_LONG_SUFFIX})))
    _check_inactivity(topology_st, LONG_SUFFIX)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
