# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import pytest
from lib389.utils import *
from lib389._constants import *
from lib389.replica import Replicas, ReplicationManager
from lib389.agreement import Agreements
from lib389.dseldif import *
from lib389.topologies import topology_m2c2 as topo_m2c2


pytestmark = pytest.mark.tier1

NEW_SUFFIX_NAME = 'test_repl'
NEW_SUFFIX = 'o={}'.format(NEW_SUFFIX_NAME)
NEW_BACKEND = 'repl_base'
CHANGELOG = 'cn=changelog,{}'.format(DN_USERROOT_LDBM)
MAXAGE_ATTR = 'nsslapd-changelogmaxage'
MAXAGE_STR = '30'
TRIMINTERVAL_STR = '5'
TRIMINTERVAL = 'nsslapd-changelogtrim-interval'

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def get_agreement(agmts, consumer):
    # Get agreement towards consumer among the agremment list
    for agmt in agmts.list():
        if (agmt.get_attr_val_utf8('nsDS5ReplicaPort') == str(consumer.port) and
           agmt.get_attr_val_utf8('nsDS5ReplicaHost') == consumer.host):
            return agmt
    return None


def test_ruv_url_not_added_if_different_uuid(topo_m2c2):
    """Check that RUV url is not updated if RUV generation uuid are different

    :id: 7cc30a4e-0ffd-4758-8f00-e500279af344
    :setup: Two suppliers + two consumers replication setup
    :steps:
        1. Generate ldif without replication data
        2. Init both suppliers from that ldif
             (to clear the ruvs and generates different generation uuid)
        3. Perform on line init from supplier1 to consumer1
               and from supplier2 to consumer2
        4. Perform update on both suppliers
        5. Check that c1 RUV does not contains URL towards m2
        6. Check that c2 RUV does contains URL towards m2
        7. Perform on line init from supplier1 to supplier2
        8. Perform update on supplier2
        9. Check that c1 RUV does contains URL towards m2
    :expectedresults:
        1. No error while generating ldif
        2. No error while importing the ldif file
        3. No error and Initialization done.
        4. No error
        5. supplier2 replicaid should not be in the consumer1 RUV
        6. supplier2 replicaid should be in the consumer2 RUV
        7. No error and Initialization done.
        8. No error
        9. supplier2 replicaid should be in the consumer1 RUV

    """

    # Variables initialization
    repl = ReplicationManager(DEFAULT_SUFFIX)

    m1 = topo_m2c2.ms["supplier1"]
    m2 = topo_m2c2.ms["supplier2"]
    c1 = topo_m2c2.cs["consumer1"]
    c2 = topo_m2c2.cs["consumer2"]

    replica_m1 = Replicas(m1).get(DEFAULT_SUFFIX)
    replica_m2 = Replicas(m2).get(DEFAULT_SUFFIX)
    replica_c1 = Replicas(c1).get(DEFAULT_SUFFIX)
    replica_c2 = Replicas(c2).get(DEFAULT_SUFFIX)

    replicid_m2 = replica_m2.get_rid()

    agmts_m1 = Agreements(m1, replica_m1.dn)
    agmts_m2 = Agreements(m2, replica_m2.dn)

    m1_m2 = get_agreement(agmts_m1, m2)
    m1_c1 = get_agreement(agmts_m1, c1)
    m1_c2 = get_agreement(agmts_m1, c2)
    m2_m1 = get_agreement(agmts_m2, m1)
    m2_c1 = get_agreement(agmts_m2, c1)
    m2_c2 = get_agreement(agmts_m2, c2)

    # Step 1: Generate ldif without replication data
    m1.stop()
    m2.stop()
    ldif_file = '%s/norepl.ldif' % m1.get_ldif_dir()
    m1.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX],
               excludeSuffixes=None, repl_data=False,
               outputfile=ldif_file, encrypt=False)
    # Remove replication metadata that are still in the ldif
    # _remove_replication_data(ldif_file)

    # Step 2: Init both suppliers from that ldif
    m1.ldif2db(DEFAULT_BENAME, None, None, None, ldif_file)
    m2.ldif2db(DEFAULT_BENAME, None, None, None, ldif_file)
    m1.start()
    m2.start()

    # Step 3: Perform on line init from supplier1 to consumer1
    #          and from supplier2 to consumer2
    m1_c1.begin_reinit()
    m2_c2.begin_reinit()
    (done, error) = m1_c1.wait_reinit()
    assert done is True
    assert error is False
    (done, error) = m2_c2.wait_reinit()
    assert done is True
    assert error is False

    # Step 4: Perform update on both suppliers
    repl.test_replication(m1, c1)
    repl.test_replication(m2, c2)

    # Step 5: Check that c1 RUV does not contains URL towards m2
    ruv = replica_c1.get_ruv()
    log.debug(f"c1 RUV: {ruv}")
    url = ruv._rid_url.get(replica_m2.get_rid())
    if url is None:
        log.debug(f"No URL for RID {replica_m2.get_rid()} in RUV")
    else:
        log.debug(f"URL for RID {replica_m2.get_rid()} in RUV is {url}")
        log.error(f"URL for RID {replica_m2.get_rid()} found in RUV")
        # Note: this assertion fails if issue 2054 is not fixed.
        assert False

    # Step 6: Check that c2 RUV does contains URL towards m2
    ruv = replica_c2.get_ruv()
    log.debug(f"c1 RUV: {ruv} {ruv._rids} ")
    url = ruv._rid_url.get(replica_m2.get_rid())
    if url is None:
        log.error(f"No URL for RID {replica_m2.get_rid()} in RUV")
        assert False
    else:
        log.debug(f"URL for RID {replica_m2.get_rid()} in RUV is {url}")

    # Step 7: Perform on line init from supplier1 to supplier2
    m1_m2.begin_reinit()
    (done, error) = m1_m2.wait_reinit()
    assert done is True
    assert error is False

    # Step 8: Perform update on supplier2
    repl.test_replication(m2, c1)

    # Step 9: Check that c1 RUV does contains URL towards m2
    ruv = replica_c1.get_ruv()
    log.debug(f"c1 RUV: {ruv} {ruv._rids} ")
    url = ruv._rid_url.get(replica_m2.get_rid())
    if url is None:
        log.error(f"No URL for RID {replica_m2.get_rid()} in RUV")
        assert False
    else:
        log.debug(f"URL for RID {replica_m2.get_rid()} in RUV is {url}")


def test_csngen_state_not_updated_if_different_uuid(topo_m2c2):
    """Check that csngen remote offset is not updated if RUV generation uuid are different

    :id: 77694b8e-22ae-11eb-89b2-482ae39447e5
    :setup: Two suppliers + two consumers replication setup
    :steps:
        1. Disable m1<->m2 agreement to avoid propagate timeSkew
        2. Generate ldif without replication data
        3. Increase time skew on supplier2
        4. Init both suppliers from that ldif
             (to clear the ruvs and generates different generation uuid)
        5. Perform on line init from supplier1 to consumer1 and supplier2 to consumer2
        6. Perform update on both suppliers
        7. Check that c1 has no time skew
        8. Check that c2 has time skew
        9. Init supplier2 from supplier1
        10. Perform update on supplier2
        11. Check that c1 has time skew
    :expectedresults:
        1. No error
        2. No error while generating ldif
        3. No error
        4. No error while importing the ldif file
        5. No error and Initialization done.
        6. No error
        7. c1 time skew should be lesser than threshold
        8. c2 time skew should be higher than threshold
        9. No error and Initialization done.
        10. No error
        11. c1 time skew should be higher than threshold

    """

    # Variables initialization
    repl = ReplicationManager(DEFAULT_SUFFIX)

    m1 = topo_m2c2.ms["supplier1"]
    m2 = topo_m2c2.ms["supplier2"]
    c1 = topo_m2c2.cs["consumer1"]
    c2 = topo_m2c2.cs["consumer2"]

    replica_m1 = Replicas(m1).get(DEFAULT_SUFFIX)
    replica_m2 = Replicas(m2).get(DEFAULT_SUFFIX)
    replica_c1 = Replicas(c1).get(DEFAULT_SUFFIX)
    replica_c2 = Replicas(c2).get(DEFAULT_SUFFIX)

    replicid_m2 = replica_m2.get_rid()

    agmts_m1 = Agreements(m1, replica_m1.dn)
    agmts_m2 = Agreements(m2, replica_m2.dn)

    m1_m2 = get_agreement(agmts_m1, m2)
    m1_c1 = get_agreement(agmts_m1, c1)
    m1_c2 = get_agreement(agmts_m1, c2)
    m2_m1 = get_agreement(agmts_m2, m1)
    m2_c1 = get_agreement(agmts_m2, c1)
    m2_c2 = get_agreement(agmts_m2, c2)

    # Step 1: Disable m1<->m2 agreement to avoid propagate timeSkew
    m1_m2.pause()
    m2_m1.pause()

    # Step 2: Generate ldif without replication data
    m1.stop()
    m2.stop()
    ldif_file = '%s/norepl.ldif' % m1.get_ldif_dir()
    m1.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX],
               excludeSuffixes=None, repl_data=False,
               outputfile=ldif_file, encrypt=False)
    # Remove replication metadata that are still in the ldif
    # _remove_replication_data(ldif_file)

    # Step 3: Increase time skew on supplier2
    timeSkew = 6*3600
    # We can modify supplier2 time skew
    # But the time skew on the consumer may be smaller
    # depending on when the cnsgen generation time is updated
    # and when first csn get replicated.
    # Since we use timeSkew has threshold value to detect
    # whether there are time skew or not,
    # lets add a significative margin (longer than the test duration)
    # to avoid any risk of erroneous failure
    timeSkewMargin = 300
    DSEldif(m2)._increaseTimeSkew(DEFAULT_SUFFIX, timeSkew+timeSkewMargin)

    # Step 4: Init both suppliers from that ldif
    m1.ldif2db(DEFAULT_BENAME, None, None, None, ldif_file)
    m2.ldif2db(DEFAULT_BENAME, None, None, None, ldif_file)
    m1.start()
    m2.start()

    # Step 5: Perform on line init from supplier1 to consumer1
    #          and from supplier2 to consumer2
    m1_c1.begin_reinit()
    m2_c2.begin_reinit()
    (done, error) = m1_c1.wait_reinit()
    assert done is True
    assert error is False
    (done, error) = m2_c2.wait_reinit()
    assert done is True
    assert error is False

    # Step 6: Perform update on both suppliers
    repl.test_replication(m1, c1)
    repl.test_replication(m2, c2)

    # Step 7: Check that c1 has no time skew
    # Stop server to insure that dse.ldif is uptodate
    c1.stop()
    c1_nsState = DSEldif(c1).readNsState(DEFAULT_SUFFIX)[0]
    c1_timeSkew = int(c1_nsState['time_skew'])
    log.debug(f"c1 time skew: {c1_timeSkew}")
    if (c1_timeSkew >= timeSkew):
        log.error(f"c1 csngen state has unexpectedly been synchronized with m2: time skew {c1_timeSkew}")
        assert False
    c1.start()

    # Step 8: Check that c2 has time skew
    # Stop server to insure that dse.ldif is uptodate
    c2.stop()
    c2_nsState = DSEldif(c2).readNsState(DEFAULT_SUFFIX)[0]
    c2_timeSkew = int(c2_nsState['time_skew'])
    log.debug(f"c2 time skew: {c2_timeSkew}")
    if (c2_timeSkew < timeSkew):
        log.error(f"c2 csngen state has not been synchronized with m2: time skew {c2_timeSkew}")
        assert False
    c2.start()

    # Step 9: Perform on line init from supplier1 to supplier2
    m1_c1.pause()
    m1_m2.resume()
    m1_m2.begin_reinit()
    (done, error) = m1_m2.wait_reinit()
    assert done is True
    assert error is False

    # Step 10: Perform update on supplier2
    repl.test_replication(m2, c1)

    # Step 11: Check that c1 has time skew
    # Stop server to insure that dse.ldif is uptodate
    c1.stop()
    c1_nsState = DSEldif(c1).readNsState(DEFAULT_SUFFIX)[0]
    c1_timeSkew = int(c1_nsState['time_skew'])
    log.debug(f"c1 time skew: {c1_timeSkew}")
    if (c1_timeSkew < timeSkew):
        log.error(f"c1 csngen state has not been synchronized with m2: time skew {c1_timeSkew}")
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
