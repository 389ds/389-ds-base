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
from lib389.nss_ssl import NssSsl
from lib389.utils import generate_ds_params
from lib389.replica import Replicas
from lib389._constants import (args_instance, SER_HOST, SER_PORT, SER_SERVERID_PROP, SER_CREATION_SUFFIX,
                               SER_SECURE_PORT, ReplicaRole, DEFAULT_SUFFIX, REPLICA_ID,
                               SER_LDAP_URL)

DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def create_topology(topo_dict):
    """Create a requested topology. Cascading replication scenario isn't supported

    @param topo_dict - dictionary {ReplicaRole.STANDALONE: num, ReplicaRole.MASTER: num,
                                   ReplicaRole.CONSUMER: num}
    @return - TopologyMain object
    """

    if not topo_dict:
        ValueError("You need to specify the dict. For instance: {ReplicaRole.STANDALONE: 1}")

    if ReplicaRole.HUB in topo_dict.keys():
        NotImplementedError("Cascading replication scenario isn't supported."
                            "Please, use existing topology or create your own.")

    instances = {}
    ms = {}
    cs = {}
    ins = {}
    replica_dict = {}

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
            args_instance[SER_SECURE_PORT] = instance_data[SER_SECURE_PORT]
            args_instance[SER_SERVERID_PROP] = instance_data[SER_SERVERID_PROP]
            args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX

            args_copied = args_instance.copy()
            instance.allocate(args_copied)
            instance_exists = instance.exists()
            if instance_exists:
                instance.delete()
            instance.create()
            # We set a URL here to force ldap:// only. Once we turn on TLS
            # we'll flick this to ldaps.
            instance.use_ldap_uri()
            instance.open()
            if role == ReplicaRole.STANDALONE:
                ins[instance.serverid] = instance
                instances.update(ins)
            if role == ReplicaRole.MASTER:
                ms[instance.serverid] = instance
                instances.update(ms)
            if role == ReplicaRole.CONSUMER:
                cs[instance.serverid] = instance
                instances.update(cs)
            log.info("Instance with parameters {} was created.".format(args_copied))

            # Set up replication
            if role in (ReplicaRole.MASTER, ReplicaRole.CONSUMER):
                replicas = Replicas(instance)
                replica = replicas.enable(DEFAULT_SUFFIX, role, instance_data[REPLICA_ID])
                replica_dict[replica] = instance

    for role_from in topo_dict.keys():
        # Do not create agreements on consumer
        if role_from == ReplicaRole.CONSUMER:
            continue

        # Create agreements: master -> masters, consumers
        for inst_num_from in range(1, topo_dict[role_from]+1):
            roles_to = [ReplicaRole.MASTER, ReplicaRole.CONSUMER]

            for role_to in [role for role in topo_dict if role in roles_to]:
                for inst_num_to in range(1, topo_dict[role_to]+1):
                    # Exclude the instance we created it from
                    if role_from != role_to or inst_num_from != inst_num_to:
                        inst_from_id = "{}{}".format(role_from.name.lower(), inst_num_from)
                        inst_to_id = "{}{}".format(role_to.name.lower(), inst_num_to)
                        inst_from = instances[inst_from_id]
                        inst_to = instances[inst_to_id]
                        inst_from.agreement.create(suffix=DEFAULT_SUFFIX,
                                                   host=inst_to.host,
                                                   port=inst_to.port)

    # Allow the replicas to get situated with the new agreements
    if replica_dict:
        time.sleep(5)

    # Initialize all agreements of one master (consumers)
    for replica_from, inst_from in replica_dict.items():
        if replica_from.get_role() == ReplicaRole.MASTER:
            agmts = inst_from.agreement.list(DEFAULT_SUFFIX)
            for r in map(lambda agmt: replica_from.start_and_wait(agmt.dn), agmts):
                assert r == 0
            break

    # Clear out the tmp dir
    for instance in instances.values():
        instance.clearTmpDir(__file__)

    if "standalone1" in instances and len(instances) == 1:
        return TopologyMain(standalones=instances["standalone1"])
    else:
        return TopologyMain(standalones=ins, masters=ms, consumers=cs)


class TopologyMain(object):
    def __init__(self, standalones=None, masters=None, consumers=None, hubs=None):
        self.all_insts = {}

        if standalones:
            if isinstance(standalones, dict):
                self.ins = standalones
                self.all_insts.update(standalones)
            else:
                self.standalone = standalones
                self.all_insts['standalone1'] = standalones
        if masters:
            self.ms = masters
            self.all_insts.update(self.ms)
        if consumers:
            self.cs = consumers
            self.all_insts.update(self.cs)
        if hubs:
            self.hs = hubs
            self.all_insts.update(self.hs)

    def __iter__(self):
        return self.all_insts.values().__iter__()

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

    def all_get_dsldapobject(self, dn, otype):
        result = []
        for inst in self.all_insts.values():
            o = otype(inst, dn)
            result.append(o)
        return result


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
    assert replicas.test(DEFAULT_SUFFIX, topology.cs["consumer1"])

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
    assert replicas.test(DEFAULT_SUFFIX, topology.ms["master2"])

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
    assert replicas.test(DEFAULT_SUFFIX, topology.ms["master3"])

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
    assert replicas.test(DEFAULT_SUFFIX, topology.ms["master4"])

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
    assert replicas.test(DEFAULT_SUFFIX, topology.cs["consumer1"])

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

    roles = (ReplicaRole.MASTER, ReplicaRole.HUB, ReplicaRole.CONSUMER)
    instances = []
    replica_dict = {}

    # Create instances
    for role in roles:
        instance_data = generate_ds_params(1, role)
        if DEBUGGING:
            instance = DirSrv(verbose=True)
        else:
            instance = DirSrv(verbose=False)
        args_instance[SER_HOST] = instance_data[SER_HOST]
        args_instance[SER_PORT] = instance_data[SER_PORT]
        args_instance[SER_SECURE_PORT] = instance_data[SER_SECURE_PORT]
        args_instance[SER_SERVERID_PROP] = instance_data[SER_SERVERID_PROP]
        args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
        args_copied = args_instance.copy()
        instance.allocate(args_copied)
        instance_exists = instance.exists()
        if instance_exists:
            instance.delete()
        instance.create()
        instance.open()
        log.info("Instance with parameters {} was created.".format(args_copied))

        # Set up replication
        replicas = Replicas(instance)
        replica = replicas.enable(DEFAULT_SUFFIX, role, instance_data[REPLICA_ID])

        if role == ReplicaRole.MASTER:
            master = instance
            replica_master = replica
            instances.append(master)
        if role == ReplicaRole.HUB:
            hub = instance
            replica_hub = replica
            instances.append(hub)
        if role == ReplicaRole.CONSUMER:
            consumer = instance
            instances.append(consumer)

    # Create all the agreements
    # Creating agreement from master to hub
    master.agreement.create(suffix=DEFAULT_SUFFIX, host=hub.host, port=hub.port)

    # Creating agreement from hub to consumer
    hub.agreement.create(suffix=DEFAULT_SUFFIX, host=consumer.host, port=consumer.port)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    # Initialize all the agreements
    agmt = master.agreement.list(DEFAULT_SUFFIX)[0].dn
    replica_master.start_and_wait(agmt)

    agmt = hub.agreement.list(DEFAULT_SUFFIX)[0].dn
    replica_hub.start_and_wait(agmt)

    # Check replication is working...
    replicas = Replicas(master)
    assert replicas.test(DEFAULT_SUFFIX, consumer)

    # Clear out the tmp dir
    master.clearTmpDir(__file__)

    def fin():
        if DEBUGGING:
            map(lambda inst: inst.stop(), instances)
        else:
            map(lambda inst: inst.delete(), instances)
    request.addfinalizer(fin)

    return TopologyMain(masters={"master1": master}, hubs={"hub1": hub}, consumers={"consumer1": consumer})

