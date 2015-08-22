# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
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

installation_prefix = None

# Assuming DEFAULT_SUFFIX is "dc=example,dc=com", otherwise it does not work... :(
USER_NUM = 10
TEST_USER = "test_user"

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

def test_ticket48252_setup(topology):
    """
    Enable USN plug-in for enabling tombstones
    Add test entries
    """

    log.info("Enable the USN plugin...")
    try:
        topology.standalone.plugins.enable(name=PLUGIN_USN)
    except e:
        log.error("Failed to enable USN Plugin: error " + e.message['desc'])
        assert False

    log.info("Adding test entries...")
    for id in range(USER_NUM):
        name = "%s%d" % (TEST_USER, id)
        topology.standalone.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
                                         'objectclass': "top person".split(),
                                         'sn': name,
                                         'cn': name})))
def in_index_file(topology, id, index):
    key = "%s%s" % (TEST_USER, id)
    log.info("	dbscan - checking %s is in index file %s..." % (key, index))
    dbscanOut = topology.standalone.dbscan(DEFAULT_BENAME, index)

    if key in dbscanOut:
        found = True
        topology.standalone.log.info("Found key %s in dbscan output" % key)
    else:
        found = False
        topology.standalone.log.info("Did not found key %s in dbscan output" % key)

    return found

def test_ticket48252_run_0(topology):
    """
    Delete an entry cn=test_entry0
    Check it is not in the 'cn' index file
    """
    log.info("Case 1 - Check deleted entry is not in the 'cn' index file")
    del_rdn = "cn=%s0" % TEST_USER
    del_entry = "%s,%s" % (del_rdn, SUFFIX)
    log.info("	Deleting a test entry %s..." % del_entry)
    topology.standalone.delete_s(del_entry)

    assert in_index_file(topology, 0, 'cn') == False

    log.info("	db2index - reindexing %s ..." % 'cn')
    assert topology.standalone.db2index(DEFAULT_BENAME, 'cn')

    assert in_index_file(topology, 0, 'cn') == False
    log.info("	entry %s is not in the cn index file after reindexed." % del_entry)
    log.info('Case 1 - PASSED')

def test_ticket48252_run_1(topology):
    """
    Delete an entry cn=test_entry1
    Check it is in the 'objectclass' index file as a tombstone entry
    """
    log.info("Case 2 - Check deleted entry is in the 'objectclass' index file as a tombstone entry")
    del_rdn = "cn=%s1" % TEST_USER
    del_entry = "%s,%s" % (del_rdn, SUFFIX)
    log.info("	Deleting a test entry %s..." % del_entry)
    topology.standalone.delete_s(del_entry)

    entry = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(&(objectclass=nstombstone)(%s))' % del_rdn)
    assert len(entry) == 1
    log.info("	entry %s is in the objectclass index file." % del_entry)

    log.info("	db2index - reindexing %s ..." % 'objectclass')
    assert topology.standalone.db2index(DEFAULT_BENAME, 'objectclass')

    entry = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(&(objectclass=nstombstone)(%s))' % del_rdn)
    assert len(entry) == 1
    log.info("	entry %s is in the objectclass index file after reindexed." % del_entry)
    log.info('Case 2 - PASSED')

def test_ticket48252_final(topology):
    topology.standalone.delete()
    log.info('Testing Ticket 48252 - PASSED.')

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
    log.info('Testing Ticket 48252 - db2index creates index entry from deleted records')

    test_ticket48252_setup(topo)

    test_ticket48252_run_0(topo)
    test_ticket48252_run_1(topo)

    test_ticket48252_final(topo)


if __name__ == '__main__':
    run_isolated()

