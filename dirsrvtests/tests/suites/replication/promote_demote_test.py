import logging
import pytest
import os
from lib389._constants import DEFAULT_SUFFIX,  ReplicaRole
from lib389.topologies import topology_m1h1c1 as topo
from lib389.replica import Replicas, ReplicationManager, Agreements

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

def test_promote_demote(topo):
    """Test promoting and demoting a replica

    :id: 75edff64-f987-4ed5-a03d-9bee73c0fbf0
    :setup: 2 Supplier Instances
    :steps:
        1. Promote Hub to a Supplier
        2. Test replication works
        3. Demote the supplier to a consumer
        4. Test replication works
        5. Promote consumer to supplier
        6. Test replication works
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    supplier = topo.ms["supplier1"]
    supplier_replica = Replicas(supplier).get(DEFAULT_SUFFIX)
    bind_dn = supplier_replica.get_attr_val_utf8('nsDS5ReplicaBindDN')
    hub = topo.hs["hub1"]
    hub_replica = Replicas(hub).get(DEFAULT_SUFFIX)
    consumer = topo.cs["consumer1"]

    repl = ReplicationManager(DEFAULT_SUFFIX)

    # promote replica
    hub_replica.promote(ReplicaRole.SUPPLIER, binddn=bind_dn, rid='55')
    repl.test_replication(supplier, consumer)

    # Demote the replica
    hub_replica.demote(ReplicaRole.CONSUMER)
    repl.test_replication(supplier, hub)

    # promote replica and init it
    hub_replica.promote(ReplicaRole.SUPPLIER, binddn=bind_dn, rid='56')
    agmt = Agreements(supplier).list()[0]
    agmt.begin_reinit()
    agmt.wait_reinit()

    # init consumer
    agmt = Agreements(hub).list()[0]
    agmt.begin_reinit()
    agmt.wait_reinit()
    repl.test_replication(supplier, consumer)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
