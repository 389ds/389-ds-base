# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""
Regression test for the race between replica config registration and
replication session auth checks (CVE-2026-11770 auth gate regression).

When nsDS5ReplicaBindDNGroup is set on the consumer AFTER the agreement
is created on the supplier, the supplier's replication thread may send
StartNSDS50ReplicationRequest before the bind DN group is configured.
With the CVE auth gate in StartNSDS50ReplicationRequest, the consumer
rejects with a bare LDAP INSUFFICIENT_ACCESS (not a BER-encoded extop
response), which the supplier cannot parse and treats as fatal.

This test reproduces the pre-fix lib389 join_supplier ordering:
create agreement first, set nsDS5ReplicaBindDNGroup second.
"""

import logging
import ldap
import os
import time
import pytest

from lib389._constants import (
    DEFAULT_SUFFIX,
    ReplicaRole,
)
from lib389.idm.group import Groups
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.services import ServiceAccounts
from lib389.idm.user import UserAccounts
from lib389.passwd import password_generate
from lib389.replica import (
    BootstrapReplicationManager,
    Replicas,
)
try:
    from test389.topologies import create_topology
except ImportError:
    from lib389.topologies import create_topology

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def topo_i2(request):
    """Two standalone instances, no replication configured yet."""
    topology = create_topology({ReplicaRole.STANDALONE: 2}, request=request)
    return topology


def test_total_init_binddn_group_race(topo_i2):
    """Total init must succeed even when nsDS5ReplicaBindDNGroup is set
    after the agreement is created (reproducing the pre-fix lib389
    join_supplier ordering and the IPA replica-install race).

    :id: 12aa5bcf-a95f-4454-9bb6-a96110a0050c
    :setup: Two standalone instances (no replication)
    :steps:
        1. Create service account infrastructure on instance A
        2. Create bootstrap repl manager and enable replication on both
        3. Bootstrap total init so service account data lands on B
        4. Create permanent agreement from A to B using service account
           but do NOT set nsDS5ReplicaBindDNGroup on B yet
        5. Trigger total init BEFORE setting nsDS5ReplicaBindDNGroup
        6. Set nsDS5ReplicaBindDNGroup on B after total init started
        7. Verify total init completes without auth rejection
        8. Verify entry replication works
    :expectedresults:
        1. Service group and account created on A
        2. Replicas configured, bootstrap repl manager created
        3. Bootstrap total init succeeds, data on B
        4. Permanent agreement created, supplier thread running
        5. Total init starts without fatal auth rejection
        6. BindDNGroup set, consumer now authorizes the bind DN
        7. Total init succeeds, no check_replica_auth in error log
        8. Entry replicates from A to B
    """
    inst_a = topo_i2.ins["standalone1"]
    inst_b = topo_i2.ins["standalone2"]

    # Step 1: Create service account group and member on A.
    # These will be replicated to B during bootstrap total init.
    groups_a = Groups(inst_a, basedn=DEFAULT_SUFFIX, rdn=None)
    repl_group = groups_a.ensure_state(properties={
        'cn': 'replication_managers',
    })

    ous_a = OrganizationalUnits(inst_a, DEFAULT_SUFFIX)
    ous_a.ensure_state(properties={'ou': 'Services'})

    svc_password = password_generate()
    svc_name = f"{DEFAULT_SUFFIX}:{inst_a.host}:{inst_a.sslport}"
    services_a = ServiceAccounts(inst_a, DEFAULT_SUFFIX)
    svc_account = services_a.ensure_state(properties={
        'cn': svc_name,
        'userPassword': svc_password,
    })
    repl_group.ensure_member(svc_account.dn)
    log.info(f"Service account {svc_account.dn} in group {repl_group.dn} on {inst_a.serverid}")

    # Step 2: Create bootstrap repl manager and enable replication.
    brm_password = password_generate()
    brm = BootstrapReplicationManager(inst_b)
    brm.ensure_state(properties={
        'cn': brm.common_name,
        'userPassword': brm_password,
    })

    replicas_a = Replicas(inst_a)
    replica_a = replicas_a.ensure_state(properties={
        'cn': 'replica',
        'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
        'nsDS5ReplicaId': '1',
        'nsDS5Flags': '1',
        'nsDS5ReplicaType': '3',
        'nsds5replicabinddngroupcheckinterval': '0',
    })

    replicas_b = Replicas(inst_b)
    replica_b = replicas_b.ensure_state(properties={
        'cn': 'replica',
        'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
        'nsDS5ReplicaId': '2',
        'nsDS5Flags': '1',
        'nsDS5ReplicaType': '3',
        'nsDS5ReplicaBindDN': brm.dn,
        'nsds5replicabinddngroupcheckinterval': '0',
    })
    log.info("Replicas configured, bootstrap manager ready")

    # Step 3: Bootstrap total init from A to B using direct BindDN.
    # This replicates the service account/group data to B.
    agmts_a = replica_a.get_agreements()
    bootstrap_name = f"bootstrap_{inst_b.host}:{inst_b.port}"
    bootstrap_agmt = agmts_a.create(properties={
        'cn': bootstrap_name,
        'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
        'nsDS5ReplicaBindDN': brm.dn,
        'nsDS5ReplicaBindMethod': 'simple',
        'nsDS5ReplicaTransportInfo': 'LDAP',
        'nsds5replicaTimeout': '120',
        'description': bootstrap_name,
        'nsDS5ReplicaHost': inst_b.host,
        'nsDS5ReplicaPort': str(inst_b.port),
        'nsDS5ReplicaCredentials': brm_password,
    })
    bootstrap_agmt.begin_reinit()
    (done, error) = bootstrap_agmt.wait_reinit(timeout=120)
    assert done, f"Bootstrap total init failed: {error}"
    assert not error, f"Bootstrap total init error: {error}"
    bootstrap_agmt.delete()
    log.info("Bootstrap total init completed, agreement removed")

    # Verify service account exists on B now.
    services_b = ServiceAccounts(inst_b, DEFAULT_SUFFIX)
    services_b.get(svc_name)
    log.info(f"Service account confirmed on {inst_b.serverid}")

    # Step 4: Create permanent agreement from A to B using service account.
    # The supplier's replication thread starts immediately.
    # nsDS5ReplicaBindDNGroup is NOT yet set on B's replica -- this is
    # the race window.  check_replica_auth() on B will enumerate all
    # replicas and check if the service account DN is authorized.
    # Without BindDNGroup, it won't find it via group membership.
    perm_name = f"perm_{inst_b.host}:{inst_b.port}"
    perm_agmt = agmts_a.create(properties={
        'cn': perm_name,
        'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
        'nsDS5ReplicaBindDN': svc_account.dn,
        'nsDS5ReplicaBindMethod': 'simple',
        'nsDS5ReplicaTransportInfo': 'LDAP',
        'nsds5replicaTimeout': '120',
        'description': perm_name,
        'nsDS5ReplicaHost': inst_b.host,
        'nsDS5ReplicaPort': str(inst_b.port),
        'nsDS5ReplicaCredentials': svc_password,
    })
    log.info("Permanent agreement created -- supplier thread active")

    # Step 5: Trigger total init BEFORE setting nsDS5ReplicaBindDNGroup.
    # The service account is NOT listed in nsDS5ReplicaBindDN and
    # nsDS5ReplicaBindDNGroup is not set yet on B's replica config.
    # check_replica_auth() must not fatally reject this.
    perm_agmt.begin_reinit()
    log.info("Total init triggered BEFORE nsDS5ReplicaBindDNGroup is set")

    # Step 6: Now set nsDS5ReplicaBindDNGroup on B.
    # This reproduces the pre-fix lib389 join_supplier ordering where
    # the group is set after ensure_agreement (which triggers init).
    replica_b.set('nsDS5ReplicaBindDNGroup', repl_group.dn)
    log.info("nsDS5ReplicaBindDNGroup set on consumer AFTER total init started")

    # Step 7: Wait for total init to complete.
    (done, error) = perm_agmt.wait_reinit(timeout=120)
    assert done, (
        f"Total init not done (error={error}). "
        f"StartNSDS50ReplicationRequest was likely rejected "
        f"by check_replica_auth() before nsDS5ReplicaBindDNGroup took effect."
    )
    assert not error, (
        f"Total init error: {error}. "
        f"The consumer sent a bare LDAP error instead of a "
        f"BER-encoded extop response."
    )
    log.info("Total init completed successfully")

    # The consumer's check_replica_auth() may log "Invalid binddn" during
    # the race window -- that's expected and harmless as long as the
    # supplier receives a proper BER-encoded extop response it can parse.
    # The fatal symptom is the supplier logging "Unable to parse the
    # response" which means it got a bare LDAP error instead.
    assert not inst_a.searchErrorsLog(
        "Unable to parse the response to the startReplication"
    ), (
        "Supplier received a bare LDAP error instead of a BER extop "
        "response from check_replica_auth() rejection. This causes "
        "ACQUIRE_FATAL_ERROR with no retry."
    )

    # Step 8: Verify replication works.
    users_a = UserAccounts(inst_a, DEFAULT_SUFFIX)
    test_user = users_a.create_test_user(uid=30001)
    log.info(f"Test user created on {inst_a.serverid}: {test_user.dn}")

    users_b = UserAccounts(inst_b, DEFAULT_SUFFIX)
    replicated = False
    for _i in range(30):
        try:
            users_b.get(test_user.rdn)
            replicated = True
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
    assert replicated, (
        f"Entry {test_user.dn} did not replicate to {inst_b.serverid} within 30s"
    )
    log.info(f"Entry replicated to {inst_b.serverid}")


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
