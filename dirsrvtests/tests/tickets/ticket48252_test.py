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
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts

from lib389._constants import DEFAULT_SUFFIX, SUFFIX, DEFAULT_BENAME, PLUGIN_USN

pytestmark = pytest.mark.tier2

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
    ua = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    for i in range(USER_NUM):
        ua.create(properties={
                'uid': "%s%d" % (TEST_USER, i),
                'cn' : "%s%d" % (TEST_USER, i),
                'sn' : 'user',
                'uidNumber' : '1000',
                'gidNumber' : '2000',
                'homeDirectory' : '/home/testuser'
            })


def in_index_file(topology_st, id, index):
    key = "%s%s" % (TEST_USER, id)
    log.info("  dbscan - checking %s is in index file %s..." % (key, index))
    dbscanOut = topology_st.standalone.dbscan(DEFAULT_BENAME, index)
    if ensure_bytes(key) in ensure_bytes(dbscanOut):
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
    uas = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    del_rdn = "uid=%s0" % TEST_USER
    del_entry = uas.get('%s0' % TEST_USER)
    log.info("  Deleting a test entry %s..." % del_entry)
    del_entry.delete()

    assert in_index_file(topology_st, 0, 'cn') is False
    log.info("  db2index - reindexing %s ..." % 'cn')
    topology_st.standalone.stop()
    assert topology_st.standalone.db2index(suffixes=[DEFAULT_SUFFIX], attrs=['cn'])
    topology_st.standalone.start()
    assert in_index_file(topology_st, 0, 'cn') is False
    log.info("  entry %s is not in the cn index file after reindexed." % del_rdn)
    log.info('Case 1 - PASSED')


def test_ticket48252_run_1(topology_st):
    """
    Delete an entry cn=test_entry1
    Check it is in the 'objectclass' index file as a tombstone entry
    """
    log.info("Case 2 - Check deleted entry is in the 'objectclass' index file as a tombstone entry")
    uas = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    del_rdn = "uid=%s1" % TEST_USER
    del_entry = uas.get('%s1' % TEST_USER)
    log.info("  Deleting a test entry %s..." % del_rdn)
    del_entry.delete()

    entry = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(&(objectclass=nstombstone)(%s))' % del_rdn)
    assert len(entry) == 1
    log.info("	entry %s is in the objectclass index file." % del_rdn)

    log.info("	db2index - reindexing %s ..." % 'objectclass')
    topology_st.standalone.stop()
    assert topology_st.standalone.db2index(suffixes=[DEFAULT_SUFFIX], attrs=['objectclass'])
    topology_st.standalone.start()
    entry = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(&(objectclass=nstombstone)(%s))' % del_rdn)
    assert len(entry) == 1
    log.info("	entry %s is in the objectclass index file after reindexed." % del_rdn)
    log.info('Case 2 - PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
