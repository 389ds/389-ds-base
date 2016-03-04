import os
import sys
import time
import ldap
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


def test_ticket47781(topology):
    """
        Testing for a deadlock after doing an online import of an LDIF with 
        replication data.  The replication agreement should be invalid.
    """

    log.info('Testing Ticket 47781 - Testing for deadlock after importing LDIF with replication data')

    #
    # Setup Replication
    #
    log.info('Setting up replication...')
    topology.standalone.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=REPLICAROLE_MASTER, 
                                                  replicaId=REPLICAID_MASTER_1)

    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    # The agreement should point to a server that does NOT exist (invalid port)
    repl_agreement = topology.standalone.agreement.create(suffix=DEFAULT_SUFFIX, 
                                                          host=topology.standalone.host, 
                                                          port=5555, 
                                                          properties=properties)

    #
    # add two entries
    #
    log.info('Adding two entries...')
    try:
        topology.standalone.add_s(Entry(('cn=entry1,dc=example,dc=com', {
                                  'objectclass': 'top person'.split(),
                                  'sn': 'user',
                                  'cn': 'entry1'})))
    except ldap.LDAPError, e:
        log.error('Failed to add entry 1: ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry(('cn=entry2,dc=example,dc=com', {
                                  'objectclass': 'top person'.split(),
                                  'sn': 'user',
                                  'cn': 'entry2'})))
    except ldap.LDAPError, e:
        log.error('Failed to add entry 2: ' + e.message['desc'])
        assert False

    #
    # export the replication ldif
    #
    log.info('Exporting replication ldif...')
    args = {EXPORT_REPL_INFO: True}
    exportTask = Tasks(topology.standalone)
    try:
        exportTask.exportLDIF(DEFAULT_SUFFIX, None, "/tmp/export.ldif", args)
    except ValueError:
        assert False

    #
    # Restart the server
    #
    log.info('Restarting server...')
    topology.standalone.stop(timeout=5)
    topology.standalone.start(timeout=5)

    #
    # Import the ldif
    #
    log.info('Import replication LDIF file...')
    importTask = Tasks(topology.standalone)
    args = {TASK_WAIT: True}
    try:
        importTask.importLDIF(DEFAULT_SUFFIX, None, "/tmp/export.ldif", args)
        os.remove("/tmp/export.ldif")
    except ValueError:
        os.remove("/tmp/export.ldif")
        assert False
    
    #
    # Search for tombstones - we should not hang/timeout
    #
    log.info('Search for tombstone entries(should find one and not hang)...')
    topology.standalone.set_option(ldap.OPT_NETWORK_TIMEOUT, 5);
    topology.standalone.set_option(ldap.OPT_TIMEOUT, 5);
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=nsTombstone')
        if not entries:
            log.fatal('Search failed to find any entries.')
            assert PR_False
    except ldap.LDAPError, e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert PR_False

    # If we got here we passed!
    log.info('Ticket47781 Test - Passed')


def test_ticket47781_final(topology):
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
    test_ticket47781(topo)

if __name__ == '__main__':
    run_isolated()
