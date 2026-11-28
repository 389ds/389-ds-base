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

from lib389._constants import DEFAULT_SUFFIX
from lib389.config import Encryption
from lib389.idm.user import UserAccounts
from lib389.replica import ReplicationManager, Replicas
from test389.topologies import topology_m2 as topo_m2

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_tls_version_mismatch_breaks_replication(topo_m2):
    """Test that replication over TLS fails when the suppliers
    are configured with mismatched TLS versions

    :id: 5c1f8a34-9d27-4e6b-8f15-2a9c3d70b1e4
    :setup: Two supplier replication topology
    :steps:
        1. Enable TLS on both suppliers
        2. Configure the replication agreements to use TLS
        3. Add users on both suppliers and check they are replicated
        4. Set TLS1.2 as the only version on supplier1 and TLS1.3 on supplier2
        5. Restart both suppliers
        6. Add a user on supplier1 and a user on supplier2
        7. Check that the user added on supplier1 is replicated to supplier2
        8. Check that the user added on supplier2 is NOT replicated to supplier1
    :expectedresults:
        1. Success
        2. Success
        3. Replication works over TLS
        4. Success
        5. Success
        6. Success
        7. The user is present on supplier2
        8. The user is NOT present on supplier1 - the TLS handshake
           between supplier2 and supplier1 must fail
    """

    m1 = topo_m2.ms['supplier1']
    m2 = topo_m2.ms['supplier2']
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info('Enabling TLS on both suppliers')
    [i.enable_tls() for i in topo_m2]

    log.info('Configuring the replication agreements to use TLS')
    for inst, other in ((m1, m2), (m2, m1)):
        agmt = Replicas(inst).get(DEFAULT_SUFFIX).get_agreements().list()[0]
        agmt.replace_many(('nsDS5ReplicaTransportInfo', 'TLS'),
                          ('nsDS5ReplicaPort', str(other.port)))

    log.info('Restarting both suppliers so the agreements take effect')
    m1.restart()
    m2.restart()

    log.info('Adding users on both suppliers and checking replication works')
    users_m1 = UserAccounts(m1, DEFAULT_SUFFIX)
    users_m2 = UserAccounts(m2, DEFAULT_SUFFIX)
    users_m1.create_test_user(uid=101)
    users_m2.create_test_user(uid=201)
    repl.wait_for_replication(m1, m2)
    repl.wait_for_replication(m2, m1)
    assert users_m1.exists('test_user_201')
    assert users_m2.exists('test_user_101')

    log.info('Restricting supplier1 to TLS1.2 only')
    enc_m1 = Encryption(m1)
    enc_m1.replace_many(('sslVersionMin', 'TLS1.2'),
                        ('sslVersionMax', 'TLS1.2'))

    log.info('Restricting supplier2 to TLS1.3 only')
    enc_m2 = Encryption(m2)
    enc_m2.replace_many(('sslVersionMin', 'TLS1.3'),
                        ('sslVersionMax', 'TLS1.3'))

    log.info('Restarting both suppliers so the TLS versions take effect')
    m1.restart()
    m2.restart()

    log.info('Adding a user on supplier1 that should replicate to supplier2')
    users_m1.create_test_user(uid=102)
    log.info('Adding a user on supplier2 that should NOT replicate to supplier1')
    users_m2.create_test_user(uid=202)

    log.info('Waiting for the user from supplier1 to be replicated to supplier2')
    repl.wait_for_replication(m1, m2)
    assert users_m2.exists('test_user_102')

    log.info('Waiting to confirm the user from supplier2 is not replicated to supplier1')
    with pytest.raises(Exception):
        repl.wait_for_replication(m2, m1)
    assert not users_m1.exists('test_user_202')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
