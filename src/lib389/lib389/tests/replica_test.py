# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import ldap
import pytest
import logging

from lib389 import NoSuchEntryError
from lib389.replica import Replicas
from lib389.backend import Backends
from lib389.idm.domain import Domain
from lib389._constants import (ReplicaRole, BACKEND_SUFFIX, BACKEND_NAME, REPLICA_RUV_FILTER, CONSUMER_REPLICAID,
                               REPLICA_FLAGS_WRITE, REPLICA_RDWR_TYPE)
from lib389.properties import (REPL_FLAGS, REPL_TYPE)
from lib389.topologies import topology_i3 as topo

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

DEBUGGING = os.getenv('DEBUGGING', default=False)
NEW_SUFFIX = 'dc=test,dc=com'
NEW_BACKEND = 'test_backend'
REPLICA_SUPPLIER_ID = 1


@pytest.fixture(scope="module")
def new_suffixes(topo):
    """Create new suffix, backend and mapping tree"""

    for num in range(1, 4):
        backends = Backends(topo.ins["standalone{}".format(num)])
        backends.create(properties={BACKEND_SUFFIX: NEW_SUFFIX,
                                    BACKEND_NAME: NEW_BACKEND})
        domain = Domain(topo.ins["standalone{}".format(num)], NEW_SUFFIX)
        domain.create(properties={'dc': 'test', 'description': NEW_SUFFIX})


@pytest.fixture(scope="function")
def simple_replica(topo, new_suffixes, request):
    """Enable simple multi-supplier replication"""

    supplier1 = topo.ins["standalone1"]
    supplier2 = topo.ins["standalone2"]

    log.info("Enable two supplier replicas")
    replicas_m1 = Replicas(supplier1)
    replica_m1 = replicas_m1.enable(suffix=NEW_SUFFIX,
                                    role=ReplicaRole.SUPPLIER,
                                    replicaID=REPLICA_SUPPLIER_ID)
    replicas_m2 = Replicas(supplier2)
    replica_m2 = replicas_m2.enable(suffix=NEW_SUFFIX,
                                    role=ReplicaRole.SUPPLIER,
                                    replicaID=REPLICA_SUPPLIER_ID+1)

    log.info("Create agreements between the instances")
    supplier1.agreement.create(suffix=NEW_SUFFIX,
                               host=supplier2.host,
                               port=supplier2.port)
    supplier2.agreement.create(suffix=NEW_SUFFIX,
                               host=supplier1.host,
                               port=supplier1.port)

    log.info("Test replication")
    replicas_m1.test(NEW_SUFFIX, supplier2)

    def fin():
            replicas_m1.disable(NEW_SUFFIX)
            replicas_m2.disable(NEW_SUFFIX)
    request.addfinalizer(fin)

    return [replica_m1, replica_m2]


@pytest.fixture(scope="function")
def clean_up(topo, new_suffixes, request):
    """Check that all replicas were disabled and disable if not"""

    def fin():
        for num in range(1, 4):
            try:
                replicas = Replicas(topo.ins["standalone{}".format(num)])
                replicas.disable(NEW_SUFFIX)
                log.info("standalone{} is disabled now".format(num))
            except:
                pass
    request.addfinalizer(fin)


def test_delete_agreements(topo, simple_replica):
    """Check deleteAgreements method

    :feature: Replication
    :steps: 1. Enable replication with agreements
            2. Delete the agreements
            3. Check that agreements were deleted
            4. Disable replication
    :expectedresults: No errors happen, agreements successfully deleted
    """

    supplier1 = topo.ins["standalone1"]
    supplier2 = topo.ins["standalone2"]

    log.info("Check that agreements in place")
    ents = supplier1.agreement.list(suffix=NEW_SUFFIX)
    assert(len(ents) == 1)
    ents = supplier2.agreement.list(suffix=NEW_SUFFIX)
    assert(len(ents) == 1)

    log.info("Delete the agreements")
    simple_replica[0].deleteAgreements()
    simple_replica[1].deleteAgreements()

    log.info("Check that agreements were deleted")
    ents = supplier1.agreement.list(suffix=NEW_SUFFIX)
    assert(len(ents) == 0)
    ents = supplier2.agreement.list(suffix=NEW_SUFFIX)
    assert(len(ents) == 0)


def test_get_ruv_entry(topo, simple_replica):
    """Check get_ruv_entry method

    :feature: Replication
    :steps: 1. Enable replication with agreements
            2. Get ruv entry with get_ruv_entry() method
            3. Get ruv entry with ldap.search
            4. Disable replication
    :expectedresults: Entries should be equal
    """

    ruv_entry = simple_replica[0].get_ruv_entry()
    entry = topo.ins["standalone1"].search_s(NEW_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)[0]

    assert ruv_entry == entry


def test_get_role(topo, simple_replica):
    """Check get_role method

    :feature: Replication
    :steps: 1. Enable replication with agreements
            2. Get role with get_role() method
            3. Get repl_flags, repl_type from the replica entry with ldap.search
            4. Compare the values
            5. Disable replication
    :expectedresults: The role 'supplier' should have flags=1 and type=3
    """

    replica_role = simple_replica[0].get_role()
    replica_flags = simple_replica[0].get_attr_val_int(REPL_FLAGS)
    replica_type = simple_replica[0].get_attr_val_int(REPL_TYPE)

    log.info("Check that we've got role 'supplier', while {}=1 and {}=3".format(REPL_FLAGS, REPL_TYPE))
    assert replica_role == ReplicaRole.supplier and replica_flags == REPLICA_FLAGS_WRITE \
        and replica_type == REPLICA_RDWR_TYPE, \
        "Failure, get_role() gave {}, while {} has {} and {} has {}".format(replica_role,
                                                                            REPL_FLAGS, replica_flags,
                                                                            REPL_TYPE, replica_type)


def test_basic(topo, new_suffixes, clean_up):
    """Check basic replica functionality

    :feature: Replication
    :steps: 1. Enable replication on supplier. hub and consumer
            2. Create agreements: supplier-hub, hub-consumer
            3. Test supplier-consumer replication
            4. Disable replication
            5. Check that replica, agreements and changelog were deleted
    :expectedresults: No errors happen, replication is successfully enabled and disabled
    """

    supplier = topo.ins["standalone1"]
    hub = topo.ins["standalone2"]
    consumer = topo.ins["standalone3"]

    log.info("Enable replicas (create replica and changelog entries)")
    supplier_replicas = Replicas(supplier)
    supplier_replicas.enable(suffix=NEW_SUFFIX,
                             role=ReplicaRole.SUPPLIER,
                             replicaID=REPLICA_SUPPLIER_ID)
    ents = supplier_replicas.list()
    assert len(ents) == 1
    ents = supplier.changelog.list()
    assert len(ents) == 1

    hub_replicas = Replicas(hub)
    hub_replicas.enable(suffix=NEW_SUFFIX,
                        role=ReplicaRole.HUB,
                        replicaID=CONSUMER_REPLICAID)
    ents = hub_replicas.list()
    assert len(ents) == 1
    ents = hub.changelog.list()
    assert len(ents) == 1

    consumer_replicas = Replicas(consumer)
    consumer_replicas.enable(suffix=NEW_SUFFIX,
                             role=ReplicaRole.CONSUMER)
    ents = consumer_replicas.list()
    assert len(ents) == 1

    log.info("Create agreements between the instances")
    supplier.agreement.create(suffix=NEW_SUFFIX,
                              host=hub.host,
                              port=hub.port)
    ents = supplier.agreement.list(suffix=NEW_SUFFIX)
    assert len(ents) == 1
    hub.agreement.create(suffix=NEW_SUFFIX,
                         host=consumer.host,
                         port=consumer.port)
    ents = hub.agreement.list(suffix=NEW_SUFFIX)
    assert len(ents) == 1

    log.info("Test replication")
    supplier_replicas.test(NEW_SUFFIX, consumer)

    log.info("Disable replication")
    supplier_replicas.disable(suffix=NEW_SUFFIX)
    hub_replicas.disable(suffix=NEW_SUFFIX)
    consumer_replicas.disable(suffix=NEW_SUFFIX)

    log.info("Check that replica, agreements and changelog were deleted")
    for num in range(1, 4):
        log.info("Checking standalone{} instance".format(num))
        inst = topo.ins["standalone{}".format(num)]

        log.info("Checking that replica entries don't exist")
        replicas = Replicas(inst)
        ents = replicas.list()
        assert len(ents) == 0

        log.info("Checking that changelog doesn't exist")
        ents = inst.changelog.list()
        assert len(ents) == 0

        log.info("Checking that agreements can't be acquired because the replica entry doesn't exist")
        with pytest.raises(NoSuchEntryError) as e:
            inst.agreement.list(suffix=NEW_SUFFIX)
            assert "no replica set up" in e.msg


@pytest.mark.parametrize('role_from,role_to',
                         ((ReplicaRole.CONSUMER, ReplicaRole.HUB),
                          (ReplicaRole.CONSUMER, ReplicaRole.SUPPLIER),
                          (ReplicaRole.HUB, ReplicaRole.SUPPLIER)))
def test_promote(topo, new_suffixes, clean_up, role_from, role_to):
    """Check that replica promote method works properly

    :feature: Replication
    :steps: 1. Enable replication on the instance
            2. Promote it to another role
               (check consumer-hub, consumer-supplier, hub-supplier
            3. Check that role was successfully changed
            4. Disable replication
    :expectedresults: No errors happen, replica successfully promoted
    """

    inst = topo.ins["standalone1"]

    log.info("Enable replication on instance with a role - {}".format(role_from))
    replicas = Replicas(inst)
    replica = replicas.enable(suffix=NEW_SUFFIX,
                              role=role_from)

    log.info("Promote replica to {}".format(role_to))
    replica.promote(newrole=role_to,
                    rid=REPLICA_SUPPLIER_ID)

    log.info("Check that replica was successfully promoted")
    replica_role = replica.get_role()
    assert replica_role == role_to


@pytest.mark.parametrize('role_from,role_to',
                         ((ReplicaRole.SUPPLIER, ReplicaRole.HUB),
                          (ReplicaRole.SUPPLIER, ReplicaRole.CONSUMER),
                          (ReplicaRole.HUB, ReplicaRole.CONSUMER)))
def test_demote(topo, new_suffixes, clean_up, role_from, role_to):
    """Check that replica demote method works properly

    :feature: Replication
    :steps: 1. Enable replication on the instance
            2. Demote it to another role
               (check supplier-hub, supplier-consumer, hub-consumer)
            3. Check that role was successfully changed
            4. Disable replication
    :expectedresults: No errors happen, replica successfully demoted
    """

    inst = topo.ins["standalone1"]

    log.info("Enable replication on instance with a role - {}".format(role_from.name))
    replicas = Replicas(inst)
    replica = replicas.enable(suffix=NEW_SUFFIX,
                              role=role_from,
                              replicaID=REPLICA_SUPPLIER_ID)

    log.info("Promote replica to {}".format(role_to.name))
    replica.demote(newrole=role_to)

    log.info("Check that replica was successfully promoted")
    replica_role = replica.get_role()
    assert replica_role == role_to


@pytest.mark.parametrize('role_from', (ReplicaRole.SUPPLIER,
                                       ReplicaRole.HUB,
                                       ReplicaRole.CONSUMER))
def test_promote_fail(topo, new_suffixes, clean_up, role_from):
    """Check that replica promote method fails
    when promoted to wrong direction

    :feature: Replication
    :steps: 1. Enable replication on the instance
            2. Try to promote it to wrong role
               (for example, supplier-hub, hub-consumer)
            3. Disable replication
    :expectedresults: Replica shouldn't be promoted
    """

    inst = topo.ins["standalone1"]

    log.info("Enable replication on instance with a role - {}".format(role_from.name))
    replicas = Replicas(inst)
    replica = replicas.enable(suffix=NEW_SUFFIX,
                              role=role_from,
                              replicaID=REPLICA_SUPPLIER_ID)

    for role_to in [x for x in range(1, 4) if x <= role_from.value]:
        role_to = ReplicaRole(role_to)
        log.info("Try to promote replica to {}".format(role_to.name))
        with pytest.raises(ValueError):
            replica.promote(newrole=role_to,
                            rid=REPLICA_SUPPLIER_ID)


@pytest.mark.parametrize('role_from', (ReplicaRole.SUPPLIER,
                                       ReplicaRole.HUB,
                                       ReplicaRole.CONSUMER))
def test_demote_fail(topo, new_suffixes, clean_up, role_from):
    """Check that replica demote method fails
    when demoted to wrong direction

    :feature: Replication
    :steps: 1. Enable replication on the instance
            2. Try to demote it to wrong role
               (for example, consumer-supplier, hub-supplier)
            3. Disable replication
    :expectedresults: Replica shouldn't be demoted
    """

    inst = topo.ins["standalone1"]

    log.info("Enable replication on instance with a role - {}".format(role_from.name))
    replicas = Replicas(inst)
    replica = replicas.enable(suffix=NEW_SUFFIX,
                              role=role_from,
                              replicaID=REPLICA_SUPPLIER_ID)

    for role_to in [x for x in range(1, 4) if x >= role_from.value]:
        role_to = ReplicaRole(role_to)
        log.info("Try to demote replica to {}".format(role_to.name))
        with pytest.raises(ValueError):
            replica.demote(newrole=role_to)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
