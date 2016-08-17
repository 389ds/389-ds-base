import os
import sys
import time
import ldap
import logging
import pytest
import subprocess
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *


DEBUGGING = False

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


class TopologyStandalone(object):
    """The DS Topology Class"""
    def __init__(self, standalone):
        """Init"""
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    """Create DS Deployment"""

    # Creating standalone instance ...
    if DEBUGGING:
        standalone = DirSrv(verbose=True)
    else:
        standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        """If we are debugging just stop the instances, otherwise remove them
        """
        if DEBUGGING:
            standalone.stop()
        else:
            standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)

def _check_status(topology, user, expected):
    nsaccountstatus = '%s/sbin/ns-accountstatus.pl' % topology.standalone.prefix
    proc = subprocess.Popen([nsaccountstatus, '-Z', 'standalone', '-D', DN_DM, '-w', PASSWORD, '-p', str(topology.standalone.port), '-I', user], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    found = False
    while True:
        l = proc.stdout.readline()
        log.info("output: %s" % l)
        if l == "":
            break
        if expected in l:
            found = True
            break
    return found

def _check_inactivity(topology, mysuffix):
    ACCT_POLICY_DN = 'cn=Account Inactivation Policy,%s' % mysuffix
    log.info("\n######################### Adding Account Policy entry: %s ######################\n" % ACCT_POLICY_DN)
    topology.standalone.add_s(Entry((ACCT_POLICY_DN, {'objectclass': "top ldapsubentry extensibleObject accountpolicy".split(),
                                                      'accountInactivityLimit': INACTIVITY_LIMIT})))
    TEST_USER_DN = 'uid=%s,%s' % (TEST_USER, mysuffix)
    log.info("\n######################### Adding Test User entry: %s ######################\n" % TEST_USER_DN)
    topology.standalone.add_s(Entry((TEST_USER_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                    'cn': TEST_USER,
                                                    'sn': TEST_USER,
                                                    'givenname': TEST_USER,
                                                    'userPassword': TEST_USER_PW,
                                                    'acctPolicySubentry': ACCT_POLICY_DN})))

    # Setting the lastLoginTime
    try:
        topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PW)
    except ldap.CONSTRAINT_VIOLATION as e:
        log.error('CONSTRAINT VIOLATION ' + e.message['desc'])
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)


    assert(_check_status(topology, TEST_USER_DN, '- activated'))

    time.sleep(int(INACTIVITY_LIMIT) + 5)
    assert(_check_status(topology, TEST_USER_DN, '- inactivated (inactivity limit exceeded'))

def test_ticket48956(topology):
    """Write your testcase here...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).

    """

    topology.standalone.modify_s(ACCT_POLICY_PLUGIN_DN, [(ldap.MOD_REPLACE, 'nsslapd-pluginarg0', ACCT_POLICY_CONFIG_DN)])

    topology.standalone.modify_s(ACCT_POLICY_CONFIG_DN, [(ldap.MOD_REPLACE, 'alwaysrecordlogin', 'yes'),
                                                         (ldap.MOD_REPLACE, 'stateattrname', 'lastLoginTime'),
                                                         (ldap.MOD_REPLACE, 'altstateattrname', 'createTimestamp'),
                                                         (ldap.MOD_REPLACE, 'specattrname', 'acctPolicySubentry'),
                                                         (ldap.MOD_REPLACE, 'limitattrname', 'accountInactivityLimit')])

    # Enable the plugins
    topology.standalone.plugins.enable(name=PLUGIN_ACCT_POLICY)

    topology.standalone.restart(timeout=10)

    # Check inactivity on standard suffix (short)
    _check_inactivity(topology, SUFFIX)

    # Check inactivity on a long suffix
    topology.standalone.backend.create(LONG_SUFFIX, {BACKEND_NAME: LONG_SUFFIX_BE})
    topology.standalone.mappingtree.create(LONG_SUFFIX, bename=LONG_SUFFIX_BE)
    topology.standalone.add_s(Entry((LONG_SUFFIX, {
                                            'objectclass': "top domain".split(),
                                            'dc': RDN_LONG_SUFFIX})))
    _check_inactivity(topology, LONG_SUFFIX)


    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

