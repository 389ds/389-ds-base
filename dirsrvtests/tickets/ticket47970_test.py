import os
import sys
import time
import ldap
import ldap.sasl
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *

log = logging.getLogger(__name__)

installation_prefix = None

USER1_DN = "uid=user1,%s" % DEFAULT_SUFFIX
USER2_DN = "uid=user2,%s" % DEFAULT_SUFFIX


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
    '''
    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix

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

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def test_ticket47970(topology):
    """
        Testing that a failed SASL bind does not trigger account lockout -
        which would attempt to update the passwordRetryCount on the root dse entry
    """

    log.info('Testing Ticket 47970 - Testing that a failed SASL bind does not trigger account lockout')

    #
    # Enable account lockout
    #
    try:
        topology.standalone.modify_s("cn=config", [(ldap.MOD_REPLACE, 'passwordLockout', 'on')])
        log.info('account lockout enabled.')
    except ldap.LDAPError, e:
        log.error('Failed to enable account lockout: ' + e.message['desc'])
        assert False

    try:
        topology.standalone.modify_s("cn=config", [(ldap.MOD_REPLACE, 'passwordMaxFailure', '5')])
        log.info('passwordMaxFailure set.')
    except ldap.LDAPError, e:
        log.error('Failed to to set passwordMaxFailure: ' + e.message['desc'])
        assert False

    #
    # Perform SASL bind that should fail
    #
    failed_as_expected = False
    try:
        user_name = "mark"
        pw = "secret"
        auth_tokens = ldap.sasl.digest_md5(user_name, pw)
        topology.standalone.sasl_interactive_bind_s("", auth_tokens)
    except ldap.INVALID_CREDENTIALS, e:
        log.info("SASL Bind failed as expected")
        failed_as_expected = True

    if not failed_as_expected:
        log.error("SASL bind unexpectedly succeeded!")
        assert False

    #
    # Check that passwordRetryCount was not set on the root dse entry
    #
    try:
        entry = topology.standalone.search_s("", ldap.SCOPE_BASE,
                                             "passwordRetryCount=*",
                                             ['passwordRetryCount'])
    except ldap.LDAPError, e:
        log.error('Failed to search Root DSE entry: ' + e.message['desc'])
        assert False

    if entry:
        log.error('Root DSE was incorrectly updated')
        assert False

    # We passed
    log.info('Root DSE was correctly not updated')


def test_ticket47970_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix = None

    topo = topology(True)
    test_ticket47970(topo)
    test_ticket47970_final(topo)


if __name__ == '__main__':
    run_isolated()
