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
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *

log = logging.getLogger(__name__)


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
    except ldap.LDAPError as e:
        log.error('Failed to add entry: ' + e.message['desc'])
        assert False

    try:
        topology.standalone.delete_s('cn=entry1,dc=example,dc=com')
    except ldap.LDAPError as e:
        log.error('Failed to delete entry: ' + e.message['desc'])
        assert False

    log.info('Search for tombstone entries...')
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                               '(&(nsTombstoneCSN=*)(objectclass=nsTombstone))')
        if not entries:
            log.fatal('Search failed to the new tombstone(nsTombstoneCSN is probably missing).')
            assert False
    except ldap.LDAPError as e:
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
    ldif_file = "/tmp/export.ldif"

    args = {EXPORT_REPL_INFO: True,
            TASK_WAIT: True}
    exportTask = Tasks(topology.standalone)
    try:
        exportTask.exportLDIF(DEFAULT_SUFFIX, None, ldif_file, args)
    except ValueError:
        assert False
    time.sleep(1)

    # open the ldif file, get the lines, then rewrite the file
    ldif = open(ldif_file, "r")
    lines = ldif.readlines()
    ldif.close()
    time.sleep(1)

    ldif = open(ldif_file, "w")
    for line in lines:
        if not line.lower().startswith('nstombstonecsn'):
            ldif.write(line)
    ldif.close()
    time.sleep(1)

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
    time.sleep(1)

    # Search for the tombstone again
    log.info('Search for tombstone entries...')
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                               '(&(nsTombstoneCSN=*)(objectclass=nsTombstone))')
        if not entries:
            log.fatal('Search failed to fine the new tombstone(nsTombstoneCSN is probably missing).')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False

    log.info('Part 2 - passed')

    #
    # Part 3 - test fixup task
    #
    log.info('Part 3:  test the fixup task')

    # Run fixup task using the strip option.  This removes nsTombstoneCSN
    # so we can test if the fixup task works.
    args = {TASK_WAIT: True,
            TASK_TOMB_STRIP: True}
    fixupTombTask = Tasks(topology.standalone)
    try:
        fixupTombTask.fixupTombstones(DEFAULT_BENAME, args)
    except:
        assert False
    time.sleep(1)

    # Search for tombstones with nsTombstoneCSN - better not find any
    log.info('Search for tombstone entries...')
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                               '(&(nsTombstoneCSN=*)(objectclass=nsTombstone))')
        if entries:
            log.fatal('Search found tombstones with nsTombstoneCSN')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False


    # Now run the fixup task
    args = {TASK_WAIT: True}
    fixupTombTask = Tasks(topology.standalone)
    try:
        fixupTombTask.fixupTombstones(DEFAULT_BENAME, args)
    except:
        assert False
    time.sleep(1)

    # Search for tombstones with nsTombstoneCSN - better find some
    log.info('Search for tombstone entries...')
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                               '(&(nsTombstoneCSN=*)(objectclass=nsTombstone))')
        if not entries:
            log.fatal('Search did not find any fixed-up tombstones')
            assert False
    except ldap.LDAPError as e:
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
    time.sleep(10)

    # Add an entry to trigger replication
    log.info('Perform an update to help trigger tombstone purging...')
    try:
        topology.standalone.add_s(Entry(('cn=test_entry,dc=example,dc=com', {
                                  'objectclass': 'top person'.split(),
                                  'sn': 'user',
                                  'cn': 'entry1'})))
    except ldap.LDAPError as e:
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
    except ldap.LDAPError as e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False

    log.info('Part 4 - passed')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
