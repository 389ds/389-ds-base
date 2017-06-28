# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, SUFFIX, DEFAULT_BENAME, PLUGIN_USN

log = logging.getLogger(__name__)

# Assuming DEFAULT_SUFFIX is "dc=example,dc=com", otherwise it does not work... :(
USER_NUM = 10
TEST_USER = "test_user"


def test_ticket48252_setup(topology_st):
    """
    Enable USN plug-in for enabling tombstones
    Add test entries
    """

    log.info("Enable the USN plugin...")
    try:
        topology_st.standalone.plugins.enable(name=PLUGIN_USN)
    except e:
        log.error("Failed to enable USN Plugin: error " + e.message['desc'])
        assert False

    log.info("Adding test entries...")
    for id in range(USER_NUM):
        name = "%s%d" % (TEST_USER, id)
        topology_st.standalone.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))


def in_index_file(topology_st, id, index):
    key = "%s%s" % (TEST_USER, id)
    log.info("	dbscan - checking %s is in index file %s..." % (key, index))
    dbscanOut = topology_st.standalone.dbscan(DEFAULT_BENAME, index)

    if key in dbscanOut:
        found = True
        topology_st.standalone.log.info("Found key %s in dbscan output" % key)
    else:
        found = False
        topology_st.standalone.log.info("Did not found key %s in dbscan output" % key)

    return found


def test_ticket48252_run_0(topology_st):
    """
    Delete an entry cn=test_entry0
    Check it is not in the 'cn' index file
    """
    log.info("Case 1 - Check deleted entry is not in the 'cn' index file")
    del_rdn = "cn=%s0" % TEST_USER
    del_entry = "%s,%s" % (del_rdn, SUFFIX)
    log.info("	Deleting a test entry %s..." % del_entry)
    topology_st.standalone.delete_s(del_entry)

    assert in_index_file(topology_st, 0, 'cn') == False

    log.info("	db2index - reindexing %s ..." % 'cn')
    assert topology_st.standalone.db2index(DEFAULT_BENAME, 'cn')

    assert in_index_file(topology_st, 0, 'cn') == False
    log.info("	entry %s is not in the cn index file after reindexed." % del_entry)
    log.info('Case 1 - PASSED')


def test_ticket48252_run_1(topology_st):
    """
    Delete an entry cn=test_entry1
    Check it is in the 'objectclass' index file as a tombstone entry
    """
    log.info("Case 2 - Check deleted entry is in the 'objectclass' index file as a tombstone entry")
    del_rdn = "cn=%s1" % TEST_USER
    del_entry = "%s,%s" % (del_rdn, SUFFIX)
    log.info("	Deleting a test entry %s..." % del_entry)
    topology_st.standalone.delete_s(del_entry)

    entry = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(&(objectclass=nstombstone)(%s))' % del_rdn)
    assert len(entry) == 1
    log.info("	entry %s is in the objectclass index file." % del_entry)

    log.info("	db2index - reindexing %s ..." % 'objectclass')
    assert topology_st.standalone.db2index(DEFAULT_BENAME, 'objectclass')

    entry = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(&(objectclass=nstombstone)(%s))' % del_rdn)
    assert len(entry) == 1
    log.info("	entry %s is in the objectclass index file after reindexed." % del_entry)
    log.info('Case 2 - PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
