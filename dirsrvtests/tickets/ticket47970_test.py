import os
import sys
import time
import ldap
import ldap.sasl
import logging
import socket
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from constants import *

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
        At the beginning, It may exists a standalone instance.
        It may also exists a backup for the standalone instance.

        Principle:
            If standalone instance exists:
                restart it
            If backup of standalone exists:
                create/rebind to standalone

                restore standalone instance from backup
            else:
                Cleanup everything
                    remove instance
                    remove backup
                Create instance
                Create backup
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

    # Get the status of the backups
    backup_standalone = standalone.checkBackupFS()

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()
    if instance_standalone:
        # assuming the instance is already stopped, just wait 5 sec max
        standalone.stop(timeout=5)
        standalone.start(timeout=10)

    if backup_standalone:
        # The backup exist, assuming it is correct
        # we just re-init the instance with it
        if not instance_standalone:
            standalone.create()
            # Used to retrieve configuration information (dbdir, confdir...)
            standalone.open()

        # restore standalone instance from backup
        standalone.stop(timeout=10)
        standalone.restoreFS(backup_standalone)
        standalone.start(timeout=10)

    else:
        # We should be here only in two conditions
        #      - This is the first time a test involve standalone instance
        #      - Something weird happened (instance/backup destroyed)
        #        so we discard everything and recreate all

        # Remove the backup. So even if we have a specific backup file
        # (e.g backup_standalone) we clear backup that an instance may have created
        if backup_standalone:
            standalone.clearBackupFS()

        # Remove the instance
        if instance_standalone:
            standalone.delete()

        # Create the instance
        standalone.create()

        # Used to retrieve configuration information (dbdir, confdir...)
        standalone.open()

        # Time to create the backups
        standalone.stop(timeout=10)
        standalone.backupfile = standalone.backupFS()
        standalone.start(timeout=10)

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    #
    # Here we have standalone instance up and running
    # Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
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
    log.info("Test Passed.")


def test_ticket47970_final(topology):
    topology.standalone.stop(timeout=10)


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

if __name__ == '__main__':
    run_isolated()