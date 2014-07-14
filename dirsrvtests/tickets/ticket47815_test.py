import os
import sys
import time
import ldap
import logging
import socket
import pytest
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from constants import *

log = logging.getLogger(__name__)

installation_prefix = None


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


def test_ticket47815(topology):
    """
        Test betxn plugins reject an invalid option, and make sure that the rejected entry
        is not in the entry cache.

        Enable memberOf, automember, and retrocl plugins
        Add the automember config entry
        Add the automember group
        Add a user that will be rejected by a betxn plugin - result error 53
        Attempt the same add again, and it should result in another error 53 (not error 68)
    """
    result = 0
    result2 = 0

    log.info('Testing Ticket 47815 - Add entries that should be rejected by the betxn plugins, and are not left in the entry cache')

    # Enabled the plugins
    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology.standalone.plugins.enable(name=PLUGIN_AUTOMEMBER)
    topology.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)

    # configure automember config entry
    log.info('Adding automember config')
    try:
        topology.standalone.add_s(Entry(('cn=group cfg,cn=Auto Membership Plugin,cn=plugins,cn=config', {
                                     'objectclass': 'top autoMemberDefinition'.split(),
                                     'autoMemberScope': 'dc=example,dc=com',
                                     'autoMemberFilter': 'cn=user',
                                     'autoMemberDefaultGroup': 'cn=group,dc=example,dc=com',
                                     'autoMemberGroupingAttr': 'member:dn',
                                     'cn': 'group cfg'})))
    except:
        log.error('Failed to add automember config')
        exit(1)

    topology.standalone.stop(timeout=120)
    time.sleep(1)
    topology.standalone.start(timeout=120)
    time.sleep(3)

    # need to reopen a connection toward the instance
    topology.standalone.open()

    # add automember group
    log.info('Adding automember group')
    try:
        topology.standalone.add_s(Entry(('cn=group,dc=example,dc=com', {
                                  'objectclass': 'top groupOfNames'.split(),
                                  'cn': 'group'})))
    except:
        log.error('Failed to add automember group')
        exit(1)

    # add user that should result in an error 53
    log.info('Adding invalid entry')

    try:
        topology.standalone.add_s(Entry(('cn=user,dc=example,dc=com', {
                                  'objectclass': 'top person'.split(),
                                  'sn': 'user',
                                  'cn': 'user'})))
    except ldap.UNWILLING_TO_PERFORM:
        log.debug('Adding invalid entry failed as expected')
        result = 53
    except ldap.LDAPError, e:
        log.error('Unexpected result ' + e.message['desc'])
        assert False
    if result == 0:
        log.error('Add operation unexpectedly succeeded')
        assert False

    # Attempt to add user again, should result in error 53 again
    try:
        topology.standalone.add_s(Entry(('cn=user,dc=example,dc=com', {
                                  'objectclass': 'top person'.split(),
                                  'sn': 'user',
                                  'cn': 'user'})))
    except ldap.UNWILLING_TO_PERFORM:
        log.debug('2nd add of invalid entry failed as expected')
        result2 = 53
    except ldap.LDAPError, e:
        log.error('Unexpected result ' + e.message['desc'])
        assert False
    if result2 == 0:
        log.error('2nd Add operation unexpectedly succeeded')
        assert False

    # If we got here we passed!
    log.info('Ticket47815 Test - Passed')


def test_ticket47815_final(topology):
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
    test_ticket47815(topo)

if __name__ == '__main__':
    run_isolated()
