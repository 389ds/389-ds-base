# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import re
import statistics
import time

import ldap

import pytest
from test389.topologies import topology_m2

from lib389._constants import DEFAULT_SUFFIX, ErrorLog

from lib389.agreement import Agreements
from lib389.dirsrv_log import DirsrvErrorLog
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.replica import ReplicationManager

from .async_result_gate import AsyncResultGate

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

# agmt_set_WaitForAsyncResults() uses 100 ms for absent/nonpositive values.
@pytest.fixture(params=[(None, 0.1), ('2000', 2.0), ('0', 0.1), ('-5', 0.1)])
def waitfor_async_attr(topology_m2, request):
    """Sets attribute on all replicas"""

    attr_value = request.param[0]
    expected_result = request.param[1]

    # Run through all suppliers

    for supplier in topology_m2.ms.values():
        agmt = Agreements(supplier).list()[0]
        original = agmt.get_attr_vals_utf8('nsDS5ReplicaWaitForAsyncResults')
        request.addfinalizer(lambda agmt=agmt, original=original:
                             agmt.ensure_attr_state({'nsDS5ReplicaWaitForAsyncResults': original}))

        if attr_value:
            agmt.set_wait_for_async_results(attr_value)
        else:
            try:
                # Sometimes we can double remove this.
                agmt.remove_wait_for_async_results()
            except ldap.NO_SUCH_ATTRIBUTE:
                pass

    return (attr_value, expected_result)


def _new_log_lines(errlog):
    """Leave an incomplete write at EOF for the next read."""
    while True:
        position = errlog.tell()
        line = errlog.readline()
        if not line.endswith('\n'):
            errlog.seek(position)
            return
        yield line


def test_not_int_value(topology_m2):
    """Tests not integer value

    :id: 67c9994f-9251-425a-8197-8d12ad9beafc
    :setup: Replication with two suppliers
    :steps:
        1. Try to set some string value
           to nsDS5ReplicaWaitForAsyncResults
    :expectedresults:
        1. Invalid syntax error should be raised
    """
    supplier1 = topology_m2.ms["supplier1"]
    agmt = Agreements(supplier1).list()[0]

    with pytest.raises(ldap.INVALID_SYNTAX):
        agmt.set_wait_for_async_results("ws2")

def test_multi_value(topology_m2):
    """Tests multi value

    :id: 1932301a-db29-407e-b27e-4466a876d1d3
    :setup: Replication with two suppliers
    :steps:
        1. Set nsDS5ReplicaWaitForAsyncResults to some int
        2. Try to add one more int value
           to nsDS5ReplicaWaitForAsyncResults
    :expectedresults:
        1. nsDS5ReplicaWaitForAsyncResults should be set
        2. Object class violation error should be raised
    """

    supplier1 = topology_m2.ms["supplier1"]
    agmt = Agreements(supplier1).list()[0]

    agmt.set_wait_for_async_results('100')
    with pytest.raises(ldap.OBJECT_CLASS_VIOLATION):
        agmt.add('nsDS5ReplicaWaitForAsyncResults', '101')

def test_value_check(topology_m2, waitfor_async_attr):
    """Checks that value has been set correctly

    :id: 3e81afe9-5130-410d-a1bb-d798d8ab8519
    :parametrized: yes
    :setup: Replication with two suppliers,
        wait for async set on all suppliers, try:
        None, '2000', '0', '-5'
    :steps:
        1. Search for nsDS5ReplicaWaitForAsyncResults on supplier 1
        2. Search for nsDS5ReplicaWaitForAsyncResults on supplier 2
    :expectedresults:
        1. nsDS5ReplicaWaitForAsyncResults should be set correctly
        2. nsDS5ReplicaWaitForAsyncResults should be set correctly
    """

    attr_value = waitfor_async_attr[0]

    for supplier in topology_m2.ms.values():
        agmt = Agreements(supplier).list()[0]

        server_value = agmt.get_wait_for_async_results_utf8()
        assert server_value == attr_value

def test_behavior_with_value(topology_m2, waitfor_async_attr, request):
    """Check the configured delay while replication replies remain outstanding.

    :id: 117b6be2-cdab-422e-b0c7-3b88bbeec036
    :parametrized: yes
    :setup: Replication with two suppliers,
        wait for async set on all suppliers, try:
        None, '2000', '0', '-5'
    :steps:
        1. Enable replication logging before adding an entry
        2. Hold its replication AddResponse and observe four pending checks
        3. Check the delay between checks in this wait invocation
        4. Release the response and wait for replication to complete
    :expectedresults:
        1. Replication logging is enabled before the workload
        2. The same wait remains pending while the reply is held
        3. The typical delay is at least the effective configured interval
        4. The wait completes and the entry is replicated
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    interval = waitfor_async_attr[1]
    agmt = Agreements(supplier1).list()[0]
    reverse_agmt = Agreements(supplier2).list()[0]
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(supplier1, supplier2)
    repl.wait_for_replication(supplier2, supplier1)

    config_attrs = ('nsslapd-errorlog-level', 'nsslapd-errorlog-logbuffering')
    original_config = {attr: supplier1.config.get_attr_vals_utf8(attr) for attr in config_attrs}
    request.addfinalizer(lambda: supplier1.config.ensure_attr_state(original_config))
    # Absence means enabled at creation, but deleting this attribute through a
    # live modify is rejected. Restore the effective state in that case.
    original_reverse = reverse_agmt.get_attr_val_utf8('nsDS5ReplicaEnabled') or 'on'
    request.addfinalizer(lambda: reverse_agmt.replace('nsDS5ReplicaEnabled', original_reverse))
    agmt_attrs = ('nsDS5ReplicaHost', 'nsDS5ReplicaPort', 'nsDS5ReplicaTimeout')
    original_agmt = {attr: agmt.get_attr_vals_utf8(attr) for attr in agmt_attrs}
    original_enabled = agmt.get_attr_val_utf8('nsDS5ReplicaEnabled') or 'on'

    def restore_agreement():
        agmt.pause()
        agmt.ensure_attr_state(original_agmt)
        agmt.replace('nsDS5ReplicaEnabled', original_enabled)

    request.addfinalizer(restore_agreement)
    # Stop existing protocol threads/connections before changing the endpoint.
    reverse_agmt.pause()
    agmt.pause()
    supplier1.config.loglevel((ErrorLog.REPLICA,))
    supplier1.config.replace('nsslapd-errorlog-logbuffering', 'off')

    # This topology uses SIMPLE/LDAP. Hold an actual operation response rather
    # than assuming that a busy receiver will leave results outstanding.
    assert agmt.get_attr_val_utf8_l('nsDS5ReplicaBindMethod') == 'simple'
    assert agmt.get_attr_val_utf8_l('nsDS5ReplicaTransportInfo') == 'ldap'
    gate = AsyncResultGate(supplier2.host, supplier2.port)
    # Close the relay before restoring the agreement, including on failure.
    request.addfinalizer(gate.close)
    # The topology's usual five-second response timeout is shorter than three
    # two-second sleeps. Keep it beyond the observation deadline.
    agmt.replace_many(('nsDS5ReplicaHost', '127.0.0.1'),
                      ('nsDS5ReplicaPort', str(gate.port)), ('nsDS5ReplicaTimeout', '60'))
    ou = OrganizationalUnits(supplier1, DEFAULT_SUFFIX).create(properties={'ou': 'async_wait'})
    request.addfinalizer(ou.delete)

    poll_re = re.compile(r'repl5_inc_waitfor_async_results - sid="([^"]+)" - (\d+) (\d+)\s*$')
    error_log = DirsrvErrorLog(supplier1)
    polls = []
    with open(supplier1.errlog) as errlog:
        # Keep diagnostics and earlier cases intact, but only observe new lines.
        errlog.seek(0, os.SEEK_END)
        agmt.resume()
        deadline = time.monotonic() + 30
        while (len(polls) < 4 or not gate.held.is_set()) and time.monotonic() < deadline:
            assert gate.error is None, f"Response relay failed: {gate.error}"
            for line in _new_log_lines(errlog):
                match = poll_re.search(line)
                if match:
                    sid, received, sent = match.groups()
                    received, sent = int(received), int(sent)
                    if not polls and received >= sent:
                        continue  # An already-completed wait need not sleep.
                    assert received < sent, f"Wait completed while its response was held: {line}"
                    if polls:
                        assert (sid, sent) == polls[0][:2], f"Wait invocation changed: {line}"
                    polls.append((sid, sent, error_log.parse_timestamp(line)))
            time.sleep(0.02)

        assert gate.held.is_set(), "No AddResponse reached the relay"
        assert len(polls) >= 4, f"Expected four pending checks, observed {polls}"
        gaps = [(after[2] - before[2]).total_seconds() for before, after in zip(polls, polls[1:])]
        log.info("Pending check gaps for %r: %s", waitfor_async_attr[0], gaps)
        # DS_Sleep may be interrupted by a signal. Use several observations and
        # allow timer granularity; scheduling can lengthen any of these gaps.
        # Ignoring 2000 (using 100 ms) or spinning without sleeping must fail.
        assert statistics.median(gaps) >= interval * 0.9, (
            f"Pending check gaps {gaps} are below the configured {interval}s delay")

        gate.release.set()
        deadline = time.monotonic() + 30
        completed = False
        while not completed and time.monotonic() < deadline:
            assert gate.error is None, f"Response relay failed: {gate.error}"
            for line in _new_log_lines(errlog):
                match = poll_re.search(line)
                if match:
                    sid, received, sent = match.groups()
                    if (sid, int(sent)) == polls[0][:2] and int(received) >= int(sent):
                        completed = True
            time.sleep(0.02)
        assert completed, "The pending wait did not complete after releasing its response"

    assert OrganizationalUnits(supplier2, DEFAULT_SUFFIX).get('async_wait').exists()
    repl.wait_for_replication(supplier1, supplier2)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
