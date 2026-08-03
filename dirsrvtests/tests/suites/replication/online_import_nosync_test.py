# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import time
import pytest

from lib389._constants import (
    DEFAULT_SUFFIX,
    DEFAULT_BENAME,
    TASK_WAIT,
    ReplicaRole,
)
from lib389.config import LMDB_LDBMConfig
from lib389.dbgen import dbgen_users
from lib389.replica import Replicas
from lib389.tasks import Tasks
from lib389.utils import get_default_db_lib
from test389.topologies import create_topology

pytestmark = [
    pytest.mark.tier2,
    pytest.mark.skipif(get_default_db_lib() == "bdb", reason="MDB-specific test"),
]

log = logging.getLogger(__name__)

NUM_ENTRIES = 100_000
REINIT_TIMEOUT = 600


@pytest.fixture(scope="function")
def topology_m1c1(request):
    """Function-scoped supplier + consumer topology."""
    return create_topology({
        ReplicaRole.SUPPLIER: 1,
        ReplicaRole.CONSUMER: 1,
    }, request=request)


@pytest.fixture(scope="function")
def loaded_m1c1(topology_m1c1):
    """Supplier + consumer with NUM_ENTRIES loaded and flow control tuned."""
    supplier = topology_m1c1.ms["supplier1"]

    for inst in topology_m1c1:
        inst.config.set('nsslapd-accesslog-logbuffering', 'on')
        inst.config.set('nsslapd-errorlog-level', '0')
        inst.config.set('nsslapd-accesslog-level', '256')

    ldif_file = os.path.join(supplier.get_ldif_dir(), "nosync_test.ldif")
    dbgen_users(supplier, NUM_ENTRIES, ldif_file, DEFAULT_SUFFIX, generic=True)
    Tasks(supplier).importLDIF(
        benamebase=DEFAULT_BENAME,
        input_file=ldif_file,
        args={TASK_WAIT: True},
    )

    agmt = Replicas(supplier).get(DEFAULT_SUFFIX).get_agreements().list()[0]
    agmt.replace('nsds5ReplicaFlowControlWindow', '3000')
    agmt.replace('nsds5ReplicaFlowControlPause', '5')

    return topology_m1c1, agmt


def _do_reinit(agmt, timeout):
    """Start total init and wait for completion."""
    prev_end = agmt.get_attr_val_utf8('nsds5replicaLastInitEnd') or ''
    agmt.begin_reinit()

    elapsed = 0
    while elapsed < timeout:
        status = agmt.get_attr_val_utf8('nsds5replicaLastInitStatus') or ''
        cur_end = agmt.get_attr_val_utf8('nsds5replicaLastInitEnd') or ''

        if 'replica busy' in status:
            return (False, status)
        if 'Replication error' in status or 'LDAP error' in status:
            return (False, status)
        if cur_end != prev_end and 'Total update succeeded' in status:
            return (True, False)

        time.sleep(2)
        elapsed += 2

    return (False, f"Timeout after {timeout}s, status: {status}")


def _create_loaded_topology():
    """Create a fresh supplier+consumer with entries loaded and flow control tuned."""
    topology = create_topology({
        ReplicaRole.SUPPLIER: 1,
        ReplicaRole.CONSUMER: 1,
    })

    supplier = topology.ms["supplier1"]

    for inst in topology:
        inst.config.set('nsslapd-accesslog-logbuffering', 'on')
        inst.config.set('nsslapd-errorlog-level', '0')
        inst.config.set('nsslapd-accesslog-level', '256')

    ldif_file = os.path.join(supplier.get_ldif_dir(), "nosync_test.ldif")
    dbgen_users(supplier, NUM_ENTRIES, ldif_file, DEFAULT_SUFFIX, generic=True)
    Tasks(supplier).importLDIF(
        benamebase=DEFAULT_BENAME,
        input_file=ldif_file,
        args={TASK_WAIT: True},
    )

    agmt = Replicas(supplier).get(DEFAULT_SUFFIX).get_agreements().list()[0]
    agmt.replace('nsds5ReplicaFlowControlWindow', '3000')
    agmt.replace('nsds5ReplicaFlowControlPause', '5')

    return topology, agmt


def test_online_import_nosync_config(topology_m1c1):
    """Test that nsslapd-mdb-online-import-nosync can be set and read.

    :id: b4f7c8a1-2d3e-4f5a-9b6c-7d8e9f0a1b2c
    :setup: Supplier + Consumer
    :steps:
        1. Verify default value is "off"
        2. Set to "on" and verify
        3. Set back to "off" and verify
    :expectedresults:
        1. Default is "off"
        2. Value is "on"
        3. Value is "off"
    """
    consumer = topology_m1c1.cs["consumer1"]
    mdb_config = LMDB_LDBMConfig(consumer)

    val = mdb_config.get_attr_val_utf8('nsslapd-mdb-online-import-nosync')
    assert val == 'off', f"Expected default 'off', got '{val}'"

    mdb_config.set('nsslapd-mdb-online-import-nosync', 'on')
    val = mdb_config.get_attr_val_utf8('nsslapd-mdb-online-import-nosync')
    assert val == 'on', f"Expected 'on', got '{val}'"

    mdb_config.set('nsslapd-mdb-online-import-nosync', 'off')
    val = mdb_config.get_attr_val_utf8('nsslapd-mdb-online-import-nosync')
    assert val == 'off', f"Expected 'off', got '{val}'"


def test_online_import_nosync_logging(loaded_m1c1):
    """Test that MDB_NOSYNC engage/clear is logged during online import.

    :id: a3b2c1d0-4e5f-6a7b-8c9d-0e1f2a3b4c5d
    :setup: Supplier + Consumer with entries loaded
    :steps:
        1. Enable nsslapd-mdb-online-import-nosync on consumer
        2. Run total init
        3. Check consumer error log for MDB_NOSYNC enabled message
        4. Check consumer error log for MDB_NOSYNC cleared message
    :expectedresults:
        1. Config set succeeds
        2. Total init completes
        3. Log contains "MDB_NOSYNC enabled for online import"
        4. Log contains "MDB_NOSYNC cleared"
    """
    topology, agmt = loaded_m1c1
    consumer = topology.cs["consumer1"]

    LMDB_LDBMConfig(consumer).set('nsslapd-mdb-online-import-nosync', 'on')

    (done, error) = _do_reinit(agmt, REINIT_TIMEOUT)
    assert done, f"Total init failed: {error}"

    # Check that the engage message was logged
    assert consumer.searchErrorsLog("MDB_NOSYNC enabled for online import"), \
        "Expected 'MDB_NOSYNC enabled for online import' in consumer error log"

    # Check that the clear message was logged
    assert consumer.searchErrorsLog("MDB_NOSYNC cleared"), \
        "Expected 'MDB_NOSYNC cleared' in consumer error log"


def test_online_import_nosync_throughput():
    """Test that total init with nosync=on is faster than nosync=off.

    :id: c5e8d9b2-3f4a-5b6c-0d7e-8f9a0b1c2d3e
    :setup: Two fresh Supplier + Consumer topologies with 100K entries
    :steps:
        1. Run total init with nosync=off, measure time
        2. Run total init with nosync=on, measure time
        3. Verify nosync=on is faster than nosync=off
    :expectedresults:
        1. Init completes
        2. Init completes
        3. nosync=on is faster
    """
    results = {}

    for nosync in ["off", "on"]:
        topology, agmt = _create_loaded_topology()
        consumer = topology.cs["consumer1"]

        LMDB_LDBMConfig(consumer).set('nsslapd-mdb-online-import-nosync', nosync)

        log.info(f"Starting total init with nosync={nosync} ...")
        start = time.time()
        (done, error) = _do_reinit(agmt, REINIT_TIMEOUT)
        duration = time.time() - start
        rate = NUM_ENTRIES / duration if duration > 0 else 0
        log.info(f"nosync={nosync}: {duration:.1f}s, {rate:.0f} entries/sec")

        for inst in topology:
            inst.delete()

        assert done, f"Total init with nosync={nosync} failed: {error}"
        results[nosync] = {"duration": duration, "rate": rate}

    log.info(
        f"\n{'='*60}\n"
        f"RESULT:\n"
        f"  nosync=off: {results['off']['duration']:.1f}s "
        f"({results['off']['rate']:.0f}/sec)\n"
        f"  nosync=on:  {results['on']['duration']:.1f}s "
        f"({results['on']['rate']:.0f}/sec)\n"
        f"  Speedup:    "
        f"{results['off']['duration'] / results['on']['duration']:.1f}x\n"
        f"{'='*60}"
    )

    assert results['on']['duration'] < results['off']['duration'], (
        f"nosync=on ({results['on']['duration']:.1f}s) should be faster "
        f"than nosync=off ({results['off']['duration']:.1f}s)"
    )


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
