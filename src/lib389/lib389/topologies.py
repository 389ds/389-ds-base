# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import time

import pytest

from lib389 import DirSrv
from lib389.utils import generate_ds_params
from lib389.replica import Replicas
from lib389._constants import (args_instance, SER_HOST, SER_PORT, SER_SERVERID_PROP, SER_CREATION_SUFFIX,
                               ReplicaRole, DEFAULT_SUFFIX, REPLICA_ID)

DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def create_topology(topo_dict):
    """Create a requested topology

    @param topo_dict - dictionary {REPLICAROLE: number_of_insts}
    @return - dictionary {serverid: topology_instance}
    """

    if not topo_dict:
        ValueError("You need to specify the dict. For instance: {ReplicaRole.STANDALONE: 1}")

    instances = {}
    ms = {}
    hs = {}
    cs = {}
    ins = {}
    replica_dict = {}
    agmts = {}

    # Create instances
    for role in topo_dict.keys():
        for inst_num in range(1, topo_dict[role]+1):
            instance_data = generate_ds_params(inst_num, role)
            if DEBUGGING:
                instance = DirSrv(verbose=True)
            else:
                instance = DirSrv(verbose=False)
            # TODO: Put 'args_instance' to generate_ds_params.
            # Also, we need to keep in mind that the function returns
            # SER_SECURE_PORT and REPLICA_ID that are not used in
            # the instance creation here.
            args_instance[SER_HOST] = instance_data[SER_HOST]
            args_instance[SER_PORT] = instance_data[SER_PORT]
            args_instance[SER_SERVERID_PROP] = instance_data[SER_SERVERID_PROP]
            args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
            args_copied = args_instance.copy()
            instance.allocate(args_copied)
            instance_exists = instance.exists()
            if instance_exists:
                instance.delete()
            instance.create()
            instance.open()
            if role == ReplicaRole.STANDALONE:
                ins[instance.serverid] = instance
                instances.update(ins)
            if role == ReplicaRole.MASTER:
                ms[instance.serverid] = instance
                instances.update(ms)
            if role == ReplicaRole.HUB:
                hs[instance.serverid] = instance
                instances.update(hs)
            if role == ReplicaRole.CONSUMER:
                cs[instance.serverid] = instance
                instances.update(cs)
            log.info("Instance with parameters {} was created.".format(args_copied))

            # Set up replication
            if role in (ReplicaRole.MASTER, ReplicaRole.HUB, ReplicaRole.CONSUMER):
                replicas = Replicas(instance)
                replica = replicas.enable(DEFAULT_SUFFIX, role, instance_data[REPLICA_ID])
                replica_dict[replica] = instance

    # Create agreements
    for role_from in topo_dict.keys():
        for inst_num_from in range(1, topo_dict[role]+1):
            roles_to = [ReplicaRole.HUB, ReplicaRole.CONSUMER]
            if role == ReplicaRole.MASTER:
                roles_to.append(ReplicaRole.MASTER)

            for role_to in [role for role in topo_dict if role in roles_to]:
                for inst_num_to in range(1, topo_dict[role]+1):
                    # Exclude our instance
                    if role_from != role_to or inst_num_from != inst_num_to:
                        inst_from_id = "{}{}".format(role_from.name.lower(), inst_num_from)
                        inst_to_id = "{}{}".format(role_to.name.lower(), inst_num_to)
                        inst_from = instances[inst_from_id]
                        inst_to = instances[inst_to_id]
                        agmt = inst_from.agreement.create(suffix=DEFAULT_SUFFIX,
                                                          host=inst_to.host,
                                                          port=inst_to.port)
                        agmts[agmt] = (inst_from, inst_to)

    # Allow the replicas to get situated with the new agreements
    if agmts:
        time.sleep(5)

    # Initialize all the agreements
    for agmt, insts in agmts.items():
        insts[0].agreement.init(DEFAULT_SUFFIX, insts[1].host, insts[1].port)
        insts[0].waitForReplInit(agmt)

    # Clear out the tmp dir
    for instance in instances.values():
        instance.clearTmpDir(__file__)

    if "standalone1" in instances and len(instances) == 1:
        return TopologyMain(standalones=instances["standalone1"])
    else:
        return TopologyMain(standalones=ins, masters=ms, hubs=hs, consumers=cs)


class TopologyMain(object):
    def __init__(self, standalones=None, masters=None, consumers=None, hubs=None):
        self.all_insts = {}

        if standalones:
            if isinstance(standalones, dict):
                self.ins = standalones
            else:
                self.standalone = standalones
        if masters:
            self.ms = masters
            self.all_insts.update(self.ms)
        if consumers:
            self.cs = consumers
            self.all_insts.update(self.cs)
        if hubs:
            self.hs = hubs
            self.all_insts.update(self.hs)

    def pause_all_replicas(self):
        """Pause all agreements in the class instance"""

        for inst in self.all_insts.values():
            for agreement in inst.agreement.list(suffix=DEFAULT_SUFFIX):
                inst.agreement.pause(agreement.dn)

    def resume_all_replicas(self):
        """Resume all agreements in the class instance"""

        for inst in self.all_insts.values():
            for agreement in inst.agreement.list(suffix=DEFAULT_SUFFIX):
                inst.agreement.resume(agreement.dn)


@pytest.fixture(scope="module")
def topology_st(request):
    """Create DS standalone instance"""

    topology = create_topology({ReplicaRole.STANDALONE: 1})

    def fin():
        if DEBUGGING:
            topology.standalone.stop()
        else:
            topology.standalone.delete()
    request.addfinalizer(fin)

    return topology


@pytest.fixture(scope="module")
def topology_i2(request):
    """Create two instance DS deployment"""

    topology = create_topology({ReplicaRole.STANDALONE: 2})

    def fin():
        if DEBUGGING:
            map(lambda inst: inst.stop(), topology.all_insts.values())
        else:
            map(lambda inst: inst.delete(), topology.all_insts.values())
    request.addfinalizer(fin)

    return topology


@pytest.fixture(scope="module")
def topology_i3(request):
    """Create three instance DS deployment"""

    topology = create_topology({ReplicaRole.STANDALONE: 3})

    def fin():
        if DEBUGGING:
            map(lambda inst: inst.stop(), topology.all_insts.values())
        else:
            map(lambda inst: inst.delete(), topology.all_insts.values())
    request.addfinalizer(fin)

    return topology


@pytest.fixture(scope="module")
def topology_m1c1(request):
    """Create Replication Deployment with one master and one consumer"""

    topology = create_topology({ReplicaRole.MASTER: 1,
                                ReplicaRole.CONSUMER: 1})
    replicas = Replicas(topology.ms["master1"])
    replicas.test(DEFAULT_SUFFIX, topology.cs["consumer1"])

    def fin():
        if DEBUGGING:
            map(lambda inst: inst.stop(), topology.all_insts.values())
        else:
            map(lambda inst: inst.delete(), topology.all_insts.values())
    request.addfinalizer(fin)

    return topology


@pytest.fixture(scope="module")
def topology_m1h1c1(request):
    """Create Replication Deployment with one master, one consumer and one hub"""

    topology = create_topology({ReplicaRole.MASTER: 1,
                                ReplicaRole.HUB: 1,
                                ReplicaRole.CONSUMER: 1})
    replicas = Replicas(topology.ms["master1"])
    replicas.test(DEFAULT_SUFFIX, topology.cs["consumer1"])

    def fin():
        if DEBUGGING:
            map(lambda inst: inst.stop(), topology.all_insts.values())
        else:
            map(lambda inst: inst.delete(), topology.all_insts.values())
    request.addfinalizer(fin)

    return topology


@pytest.fixture(scope="module")
def topology_m2(request):
    """Create Replication Deployment with two masters"""

    topology = create_topology({ReplicaRole.MASTER: 2})
    replicas = Replicas(topology.ms["master1"])
    replicas.test(DEFAULT_SUFFIX, topology.ms["master2"])

    def fin():
        if DEBUGGING:
            map(lambda inst: inst.stop(), topology.all_insts.values())
        else:
            map(lambda inst: inst.delete(), topology.all_insts.values())
    request.addfinalizer(fin)

    return topology


@pytest.fixture(scope="module")
def topology_m3(request):
    """Create Replication Deployment with three masters"""

    topology = create_topology({ReplicaRole.MASTER: 3})
    replicas = Replicas(topology.ms["master1"])
    replicas.test(DEFAULT_SUFFIX, topology.ms["master3"])

    def fin():
        if DEBUGGING:
            map(lambda inst: inst.stop(), topology.all_insts.values())
        else:
            map(lambda inst: inst.delete(), topology.all_insts.values())
    request.addfinalizer(fin)

    return topology


@pytest.fixture(scope="module")
def topology_m4(request):
    """Create Replication Deployment with four masters"""

    topology = create_topology({ReplicaRole.MASTER: 4})
    replicas = Replicas(topology.ms["master1"])
    replicas.test(DEFAULT_SUFFIX, topology.ms["master4"])

    def fin():
        if DEBUGGING:
            map(lambda inst: inst.stop(), topology.all_insts.values())
        else:
            map(lambda inst: inst.delete(), topology.all_insts.values())
    request.addfinalizer(fin)

    return topology


@pytest.fixture(scope="module")
def topology_m2c2(request):
    """Create Replication Deployment with two masters and two consumers"""

    topology = create_topology({ReplicaRole.MASTER: 2,
                                ReplicaRole.CONSUMER: 2})
    replicas = Replicas(topology.ms["master1"])
    replicas.test(DEFAULT_SUFFIX, topology.cs["consumer1"])

    def fin():
        if DEBUGGING:
            map(lambda inst: inst.stop(), topology.all_insts.values())
        else:
            map(lambda inst: inst.delete(), topology.all_insts.values())
    request.addfinalizer(fin)

    return topology
