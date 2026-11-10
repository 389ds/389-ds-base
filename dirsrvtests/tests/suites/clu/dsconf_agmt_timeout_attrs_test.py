# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import logging
import pytest
from test389.topologies import topology_st as topo
from lib389.cli_base import FakeArgs
from lib389.cli_conf.replication import add_agmt
from lib389.replica import Replicas
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_agmt_create_timeout_and_flow_control_attrs(topo):
    """Verify add_agmt passes timeout and flow-control attributes
    through to the agreement entry

    :id: 2c95ce33-7f25-480a-b827-c03ee10da650
    :setup: Standalone instance
    :steps:
        1. Enable replication on the instance as a supplier
        2. Add agreement with all timeout/flow-control arguments set
        3. Read back the agreement and verify each attribute values
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    inst = topo.standalone

    replicas = Replicas(inst)
    replicas.create(properties={
        'cn': 'replica',
        'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
        'nsDS5ReplicaId': '1',
        'nsDS5ReplicaType': '3',
        'nsDS5Flags': '1',
    })

    EXPECTED = {
        'nsds5replicatimeout': '30',
        'nsds5replicaprotocoltimeout': '120',
        'nsds5replicawaitforasyncresults': '500',
        'nsds5replicabusywaittime': '5',
        'nsds5replicaSessionPauseTime': '3',
        'nsds5replicaflowcontrolwindow': '2000',
        'nsds5replicaflowcontrolpause': '4',
    }

    args = FakeArgs()
    # Required args
    args.AGMT_NAME = ['test-timeout-agmt']
    args.suffix = DEFAULT_SUFFIX
    args.host = 'localhost'
    args.port = '33333'
    args.conn_protocol = 'LDAP'
    args.bind_dn = 'cn=replmgr,cn=config'
    args.bind_passwd = 'replmgr'
    args.bind_method = 'SIMPLE'
    args.init = False
    
    # Optional attributes that we're testing
    args.conn_timeout = EXPECTED['nsds5replicatimeout']
    args.protocol_timeout = EXPECTED['nsds5replicaprotocoltimeout']
    args.wait_async_results = EXPECTED['nsds5replicawaitforasyncresults']
    args.busy_wait_time = EXPECTED['nsds5replicabusywaittime']
    args.session_pause_time = EXPECTED['nsds5replicaSessionPauseTime']
    args.flow_control_window = EXPECTED['nsds5replicaflowcontrolwindow']
    args.flow_control_pause = EXPECTED['nsds5replicaflowcontrolpause']

    # The rest is None
    args.__class__.__getattr__ = lambda self, name: None

    # Create new agreement with optional attributes
    add_agmt(inst, None, log, args)

    # Read it back
    replica = replicas.get(DEFAULT_SUFFIX)
    agmt = replica.get_agreements().list()[0]

    for attr, expected_val in EXPECTED.items():
        actual = agmt.get_attr_val_utf8(attr)
        log.info(f"Checking {attr}: expected={expected_val}, actual={actual}")
        assert actual == expected_val, \
            f"Attribute {attr}: expected '{expected_val}', got '{actual}'"

    log.info("All timeout and flow-control attributes verified successfully")


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(f"-s {CURRENT_FILE}")
