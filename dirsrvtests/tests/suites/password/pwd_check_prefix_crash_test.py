# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
from test389.topologies import topology_m1 as topo
from lib389.agreement import Agreements
from lib389.replica import Replicas

log = logging.getLogger(__name__)


def test_buffer_overflow_in_check_prefix(topo):
    """Test buffer overflow in checkPrefix

    :id: 12345678-1234-1234-1234-123456789012
    :setup: Supplier Instance
    :steps:
        1. Create a replica agreement with a password that has a long prefix
        2. Check server is still running
    :expectedresults:
        1. The agreement is created successfully
        2. Server is still running
    """
    inst = topo.ms["supplier1"]
    prefix = 'AES-' + ('A' * 512)
    pwd_value = ('{' + prefix + '}dGVzdA==')

    log.info("Creating agreement with invalid password prefix ...")

    replica = Replicas(inst).list()[0]
    repl_agmts = Agreements(inst, basedn=replica.dn)
    repl_agmts.create(properties={
        'cn': 'test agmt',
        'nsDS5ReplicaRoot': 'dc=example,dc=com',
        'nsDS5ReplicaPort': '389',
        'nsDS5ReplicaHost': 'localhost',
        'nsDS5ReplicaBindMethod': 'SIMPLE',
        'nsDS5ReplicaBindDN': 'cn=replication manager,cn=config',
        'nsDS5ReplicaTransportInfo': 'LDAP',
        'nsDS5ReplicaCredentials': pwd_value
    })

    log.info("Agreement added, checking server status ...")
    assert inst.status(), "Server crashed"
    log.info("Test passed!")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
