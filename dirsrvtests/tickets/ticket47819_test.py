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
        standalone.start(timeout=60)

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
        standalone.start(timeout=60)

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
        standalone.start(timeout=60)

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    # Here we have standalone instance up and running
    # Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyStandalone(standalone)


def test_ticket47819(topology):
    """
        Testing precise tombstone purging:
            [1]  Make sure "nsTombstoneCSN" is added to new tombstones
            [2]  Make sure an import of a replication ldif adds "nsTombstoneCSN"
                 to old tombstones
            [4]  Test fixup task
            [3]  Make sure tombstone purging works
    """

    log.info('Testing Ticket 47819 - Test precise tombstone purging')

    #
    # Setup Replication
    #
    log.info('Setting up replication...')
    topology.standalone.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=REPLICAROLE_MASTER,
                                                  replicaId=REPLICAID_MASTER_1)

    #
    # Part 1 create a tombstone entry and make sure nsTombstoneCSN is added
    #
    log.info('Part 1:  Add and then delete an entry to create a tombstone...')

    try:
        topology.standalone.add_s(Entry(('cn=entry1,dc=example,dc=com', {
                                  'objectclass': 'top person'.split(),
                                  'sn': 'user',
                                  'cn': 'entry1'})))
    except ldap.LDAPError, e:
        log.error('Failed to add entry: ' + e.message['desc'])
        assert False

    try:
        topology.standalone.delete_s('cn=entry1,dc=example,dc=com')
    except ldap.LDAPError, e:
        log.error('Failed to delete entry: ' + e.message['desc'])
        assert False

    log.info('Search for tombstone entries...')
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                               '(&(nsTombstoneCSN=*)(objectclass=nsTombstone))')
        if not entries:
            log.fatal('Search failed to the new tombstone(nsTombstoneCSN is probably missing).')
            assert False
    except ldap.LDAPError, e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False

    log.info('Part 1 - passed')

    #
    # Part 2 - import ldif with tombstones missing 'nsTombstoneCSN'
    #
    # First, export the replication ldif, edit the file(remove nstombstonecsn),
    # and reimport it.
    #
    log.info('Part 2:  Exporting replication ldif...')

    # Get the the full path and name for our LDIF we will be exporting
    ldif_file = topology.standalone.getDir(__file__, TMP_DIR) + "export.ldif"

    args = {EXPORT_REPL_INFO: True,
            TASK_WAIT: True}
    exportTask = Tasks(topology.standalone)
    try:
        exportTask.exportLDIF(DEFAULT_SUFFIX, None, ldif_file, args)
    except ValueError:
        assert False

    # open the ldif file, get the lines, then rewrite the file
    ldif = open(ldif_file, "r")
    lines = ldif.readlines()
    ldif.close()

    ldif = open(ldif_file, "w")
    for line in lines:
        if not line.lower().startswith('nstombstonecsn'):
            ldif.write(line)
    ldif.close()

    # import the new ldif file
    log.info('Import replication LDIF file...')
    importTask = Tasks(topology.standalone)
    args = {TASK_WAIT: True}
    try:
        importTask.importLDIF(DEFAULT_SUFFIX, None, ldif_file, args)
        os.remove(ldif_file)
    except ValueError:
        os.remove(ldif_file)
        assert False

    # Search for the tombstone again
    log.info('Search for tombstone entries...')
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                               '(&(nsTombstoneCSN=*)(objectclass=nsTombstone))')
        if not entries:
            log.fatal('Search failed to fine the new tombstone(nsTombstoneCSN is probably missing).')
            assert False
    except ldap.LDAPError, e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False

    log.info('Part 2 - passed')

    #
    # Part 3 - test fixup task
    #
    log.info('Part 4:  test the fixup task')

    # Run fixup task using the strip option.  This removes nsTombstoneCSN
    # so we can test if the fixup task works.
    args = {TASK_WAIT: True,
            TASK_TOMB_STRIP: True}
    fixupTombTask = Tasks(topology.standalone)
    try:
        fixupTombTask.fixupTombstones(DEFAULT_BENAME, args)
    except:
        assert False

    # Search for tombstones with nsTombstoneCSN - better not find any
    log.info('Search for tombstone entries...')
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                               '(&(nsTombstoneCSN=*)(objectclass=nsTombstone))')
        if entries:
            log.fatal('Search found tombstones with nsTombstoneCSN')
            assert False
    except ldap.LDAPError, e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False

    # Now run the fixup task
    args = {TASK_WAIT: True}
    fixupTombTask = Tasks(topology.standalone)
    try:
        fixupTombTask.fixupTombstones(DEFAULT_BENAME, args)
    except:
        assert False

    # Search for tombstones with nsTombstoneCSN - better find some
    log.info('Search for tombstone entries...')
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                               '(&(nsTombstoneCSN=*)(objectclass=nsTombstone))')
        if not entries:
            log.fatal('Search did not find any fixed-up tombstones')
            assert False
    except ldap.LDAPError, e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False

    log.info('Part 3 - passed')

    #
    # Part 4 - Test tombstone purging
    #
    log.info('Part 4:  test tombstone purging...')

    args = {REPLICA_PRECISE_PURGING: 'on',
            REPLICA_PURGE_DELAY: '5',
            REPLICA_PURGE_INTERVAL: '5'}
    try:
        topology.standalone.replica.setProperties(DEFAULT_SUFFIX, None, None, args)
    except:
        log.fatal('Failed to configure replica')
        assert False

    # Wait for the interval to pass
    log.info('Wait for tombstone purge interval to pass...')
    time.sleep(6)

    # Add an entry to trigger replication
    log.info('Perform an update to help trigger tombstone purging...')
    try:
        topology.standalone.add_s(Entry(('cn=test_entry,dc=example,dc=com', {
                                  'objectclass': 'top person'.split(),
                                  'sn': 'user',
                                  'cn': 'entry1'})))
    except ldap.LDAPError, e:
        log.error('Failed to add entry: ' + e.message['desc'])
        assert False

    # Wait for the interval to pass again
    log.info('Wait for tombstone purge interval to pass again...')
    time.sleep(10)

    # search for tombstones, there should be none
    log.info('Search for tombstone entries...')
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                               '(&(nsTombstoneCSN=*)(objectclass=nsTombstone))')
        if entries:
            log.fatal('Search unexpectedly found tombstones')
            assert False
    except ldap.LDAPError, e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False

    log.info('Part 4 - passed')

    #
    # If we got here we passed!
    #
    log.info('Ticket47819 Test - Passed')


def test_ticket47819_final(topology):
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
    test_ticket47819(topo)

if __name__ == '__main__':
    run_isolated()