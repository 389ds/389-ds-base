# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket47966(topology_m2):
    '''
    Testing bulk import when the backend with VLV was recreated.
    If the test passes without the server crash, 47966 is verified.
    '''
    log.info('Testing Ticket 47966 - [VLV] slapd crashes during Dogtag clone reinstallation')
    M1 = topology_m2.ms["master1"]
    M2 = topology_m2.ms["master2"]
    m1_m2_agmt = topology_m2.ms["master1_agmts"]["m1_m2"]

    log.info('0. Create a VLV index on Master 2.')
    # get the backend entry
    be = M2.replica.conn.backend.list(suffix=DEFAULT_SUFFIX)
    if not be:
        log.fatal("ticket47966: enable to retrieve the backend for %s" % DEFAULT_SUFFIX)
        raise ValueError("no backend for suffix %s" % DEFAULT_SUFFIX)
    bent = be[0]
    beName = bent.getValue('cn')
    beDn = "cn=%s,cn=ldbm database,cn=plugins,cn=config" % beName

    # generate vlvSearch entry
    vlvSrchDn = "cn=vlvSrch,%s" % beDn
    log.info('0-1. vlvSearch dn: %s' % vlvSrchDn)
    vlvSrchEntry = Entry(vlvSrchDn)
    vlvSrchEntry.setValues('objectclass', 'top', 'vlvSearch')
    vlvSrchEntry.setValues('cn', 'vlvSrch')
    vlvSrchEntry.setValues('vlvBase', DEFAULT_SUFFIX)
    vlvSrchEntry.setValues('vlvFilter', '(|(objectclass=*)(objectclass=ldapsubentry))')
    vlvSrchEntry.setValues('vlvScope', '2')
    M2.add_s(vlvSrchEntry)

    # generate vlvIndex entry
    vlvIndexDn = "cn=vlvIdx,%s" % vlvSrchDn
    log.info('0-2. vlvIndex dn: %s' % vlvIndexDn)
    vlvIndexEntry = Entry(vlvIndexDn)
    vlvIndexEntry.setValues('objectclass', 'top', 'vlvIndex')
    vlvIndexEntry.setValues('cn', 'vlvIdx')
    vlvIndexEntry.setValues('vlvSort', 'cn ou sn')
    M2.add_s(vlvIndexEntry)

    log.info('1. Initialize Master 2 from Master 1.')
    M1.agreement.init(DEFAULT_SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    M1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if M1.testReplication(DEFAULT_SUFFIX, M2):
        log.info('1-1. Replication is working.')
    else:
        log.fatal('1-1. Replication is not working.')
        assert False

    log.info('2. Delete the backend instance on Master 2.')
    M2.delete_s(vlvIndexDn)
    M2.delete_s(vlvSrchDn)
    # delete the agreement, replica, and mapping tree, too.
    M2.replica.disableReplication(DEFAULT_SUFFIX)
    mappingTree = 'cn="%s",cn=mapping tree,cn=config' % DEFAULT_SUFFIX
    M2.mappingtree.delete(DEFAULT_SUFFIX, beName, mappingTree)
    M2.backend.delete(DEFAULT_SUFFIX, beDn, beName)

    log.info('3. Recreate the backend and the VLV index on Master 2.')
    M2.mappingtree.create(DEFAULT_SUFFIX, beName)
    M2.backend.create(DEFAULT_SUFFIX, {BACKEND_NAME: beName})
    log.info('3-1. Recreating %s and %s on Master 2.' % (vlvSrchDn, vlvIndexDn))
    M2.add_s(vlvSrchEntry)
    M2.add_s(vlvIndexEntry)
    M2.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_2)
    # agreement m2_m1_agmt is not needed... :p

    log.info('4. Initialize Master 2 from Master 1 again.')
    M1.agreement.init(DEFAULT_SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    M1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if M1.testReplication(DEFAULT_SUFFIX, M2):
        log.info('4-1. Replication is working.')
    else:
        log.fatal('4-1. Replication is not working.')
        assert False

    log.info('5. Check Master 2 is up.')
    entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(cn=*)')
    assert len(entries) > 0
    log.info('5-1. %s entries are returned from M2.' % len(entries))

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
