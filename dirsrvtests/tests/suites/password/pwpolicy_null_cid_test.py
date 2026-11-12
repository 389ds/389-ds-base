# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import pytest
from lib389._constants import DEFAULT_SUFFIX, PASSWORD, PORT_CONSUMER_1, PORT_CONSUMER_2, PORT_CONSUMER_3
from lib389.replica import Replicas
from test389.topologies import topology_m1 as topo

pytestmark = pytest.mark.tier1
DEBUGGING = os.getenv('DEBUGGING', default=False)

def test_pwpolicy_identity_initialized(topo):
    """Test that internal ADD operations at startup do not search
    with an uninitialized password policy identity.

    :id: a486aae7-6c50-4919-b6b3-1caa2a3da262
    :customerscenario: True
    :setup: One supplier with 3 agreements and local password policy enabled
    :steps:
        1. Create 3 agreements to trigger a race condition
        2. Enable local password policy
        3. Restart instance twice
        4. Check error log for missing component or plugin identities
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. No internal operation reports a missing component or plugin identity
    """
    supplier = topo.ms['supplier1']
    replica = Replicas(supplier).get(DEFAULT_SUFFIX)
    agmts = replica.get_agreements()
    for port in (PORT_CONSUMER_1, PORT_CONSUMER_2, PORT_CONSUMER_3):
        agmts.create(properties={
            'cn': f'replica-{port}',
            'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
            'nsDS5ReplicaHost': '127.0.0.1',
            'nsDS5ReplicaPort': str(port),
            'nsDS5ReplicaBindDN': 'cn=replication manager,cn=config',
            'nsDS5ReplicaCredentials': PASSWORD,
            'nsDS5ReplicaBindMethod': 'SIMPLE',
            'nsDS5ReplicaTransportInfo': 'LDAP',
            'nsDS5ReplicaEnabled': 'on',
            'nsDS5ReplicaTimeout': '1',
        })
    supplier.config.replace('nsslapd-pwpolicy-local', 'on')

    for _ in range(2):
        supplier.restart()
        errors = supplier.ds_error_log.match(
            r'.*allow_operation - (Component|Plugin) identity is NULL',
            after_pattern=r'.*main - .* starting up')
        assert not errors, ''.join(errors)
