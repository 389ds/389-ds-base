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

# For hostname detection for GSSAPI tests
import socket

import pytest

from lib389 import DirSrv
from lib389.utils import generate_ds_params
from lib389.mit_krb5 import MitKrb5
from lib389.saslmap import SaslMappings
from lib389.replica import ReplicationManager, Replicas
from lib389.nss_ssl import NssSsl
from lib389._constants import *
from lib389.cli_base import LogCapture

PYINSTALL = True if os.getenv('PYINSTALL') else False
DEBUGGING = os.getenv('DEBUGGING', default=False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _remove_ssca_db(topology):
    ssca = NssSsl(dbpath=topology[0].get_ssca_dir())
    if ssca._db_exists():
        return ssca.remove_db()
    else:
        return True


def _create_instances(topo_dict, suffix):
    """Create requested instances without replication or any other modifications

    :param topo_dict: a dictionary {ReplicaRole.STANDALONE: num, ReplicaRole.MASTER: num,
                                    ReplicaRole.HUB: num, ReplicaRole.CONSUMER: num}
    :type topo_dict: dict
    :param suffix: a suffix
    :type suffix: str

    :return - TopologyMain object
    """

    instances = {}
    ms = {}
    cs = {}
    hs = {}
    ins = {}

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
            # args_instance[SER_HOST] = instance_data[SER_HOST]
            args_instance = {}
            args_instance[SER_PORT] = instance_data[SER_PORT]
            args_instance[SER_SECURE_PORT] = instance_data[SER_SECURE_PORT]
            args_instance[SER_SERVERID_PROP] = instance_data[SER_SERVERID_PROP]
            # It's required to be able to make a suffix-less install for
            # some cli tests. It's invalid to require replication with
            # no suffix however ....
            if suffix is not None:
                args_instance[SER_CREATION_SUFFIX] = suffix
            elif role != ReplicaRole.STANDALONE:
                raise AssertionError("Invalid request to make suffix-less replicated environment")

            instance.allocate(args_instance)

            instance_exists = instance.exists()

            if instance_exists:
                instance.delete(pyinstall=PYINSTALL)

            instance.create(pyinstall=PYINSTALL)
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
            if role == ReplicaRole.HUB:
                hs[instance.serverid] = instance
                instances.update(hs)
            if DEBUGGING:
                instance.config.set('nsslapd-accesslog-logbuffering','off')
                instance.config.set('nsslapd-errorlog-level','8192')
                instance.config.set('nsslapd-auditlog-logging-enabled','on')
            log.info("Instance with parameters {} was created.".format(args_instance))

    if "standalone1" in instances and len(instances) == 1:
        return TopologyMain(standalones=instances["standalone1"])
    else:
        return TopologyMain(standalones=ins, masters=ms, consumers=cs, hubs=hs)


def create_topology(topo_dict, suffix=DEFAULT_SUFFIX):
    """Create a requested topology. Cascading replication scenario isn't supported

    :param topo_dict: a dictionary {ReplicaRole.STANDALONE: num, ReplicaRole.MASTER: num,
                                   ReplicaRole.CONSUMER: num}
    :type topo_dict: dict
    :param suffix: a suffix for the replication
    :type suffix: str

    :return - TopologyMain object
    """

    if not topo_dict:
        ValueError("You need to specify the dict. For instance: {ReplicaRole.STANDALONE: 1}")

    if ReplicaRole.HUB in topo_dict.keys():
        NotImplementedError("Cascading replication scenario isn't supported."
                            "Please, use existing topology or create your own.")

    topo = _create_instances(topo_dict, suffix)

    # Start with a single master, and create it "first".
    first_master = None
    try:
        first_master = list(topo.ms.values())[0]
        log.info("Creating replication topology.")
        # Now get the first master ready.
        repl = ReplicationManager(DEFAULT_SUFFIX)
        repl.create_first_master(first_master)
    except IndexError:
        pass

    # Now init the other masters from this.
    # This will reinit m, and put a bi-directional agreement
    # in place.
    for m in topo.ms.values():
        # Skip firstmaster.
        if m is first_master:
            continue
        log.info("Joining master %s to %s ..." % (m.serverid, first_master.serverid))
        repl.join_master(first_master, m)

    # Mesh the master agreements.
    for mo in topo.ms.values():
        for mi in topo.ms.values():
            if mo is mi:
                continue
            log.info("Ensuring master %s to %s ..." % (mo.serverid, mi.serverid))
            repl.ensure_agreement(mo, mi)

    # Add master -> consumer agreements.
    for c in topo.cs.values():
        log.info("Joining consumer %s from %s ..." % (c.serverid, first_master.serverid))
        repl.join_consumer(first_master, c)

    for m in topo.ms.values():
        for c in topo.cs.values():
            log.info("Ensuring consumer %s from %s ..." % (c.serverid, m.serverid))
            repl.ensure_agreement(m, c)

    # Clear out the tmp dir
    for instance in topo:
        instance.clearTmpDir(__file__)

    return topo


class TopologyMain(object):
    def __init__(self, standalones=None, masters=None, consumers=None, hubs=None):
        self.ms = {}
        self.cs = {}
        self.hs = {}
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

    def __getitem__(self, index):
        return list(self.all_insts.values())[index]

    def pause_all_replicas(self):
        """Pause all agreements in the class instance"""

        for inst in self.all_insts.values():
            replicas = Replicas(inst)
            replica = replicas.get(DEFAULT_SUFFIX)
            for agreement in replica.get_agreements().list():
                agreement.pause()

    def resume_all_replicas(self):
        """Resume all agreements in the class instance"""

        for inst in self.all_insts.values():
            replicas = Replicas(inst)
            replica = replicas.get(DEFAULT_SUFFIX)
            for agreement in replica.get_agreements().list():
                agreement.resume()

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
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)

        if DEBUGGING:
            topology.standalone.stop()
        else:
            assert _remove_ssca_db(topology)
            if topology.standalone.exists():
                topology.standalone.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


gssapi_ack = pytest.mark.skipif(not os.environ.get('GSSAPI_ACK', False), reason="GSSAPI tests may damage system configuration.")

@pytest.fixture(scope="module")
def topology_st_gssapi(request):
    """Create a DS standalone instance with GSSAPI enabled.

    This will alter the instance to remove the secure port, to allow
    GSSAPI to function.
    """
    hostname = socket.gethostname().split('.', 1)

    # Assert we have a domain setup in some kind.
    assert len(hostname) == 2

    REALM = hostname[1].upper()

    topology = create_topology({ReplicaRole.STANDALONE: 1})

    # Fix the hostname.
    topology.standalone.host = socket.gethostname()

    krb = MitKrb5(realm=REALM, debug=DEBUGGING)

    # Destroy existing realm.
    if krb.check_realm():
        krb.destroy_realm()
    krb.create_realm()

    # Now add krb to our instance.
    krb.create_principal(principal='ldap/%s' % topology.standalone.host)
    krb.create_keytab(principal='ldap/%s' % topology.standalone.host, keytab='/etc/krb5.keytab')
    os.chown('/etc/krb5.keytab', topology.standalone.get_user_uid(), topology.standalone.get_group_gid())

    # Add sasl mappings
    saslmappings = SaslMappings(topology.standalone)
    # First, purge all the default maps.
    [m.delete() for m in saslmappings.list()]
    # Now create a single map that works for our case.
    saslmappings.create(properties={
        'cn': 'suffix map',
        # Don't add the realm due to a SASL bug
        # 'nsSaslMapRegexString': '\\(.*\\)@%s' % self.realm,
        'nsSaslMapRegexString': '\\(.*\\)',
        'nsSaslMapBaseDNTemplate': topology.standalone.creation_suffix,
        'nsSaslMapFilterTemplate': '(uid=\\1)'
    })
    topology.standalone.realm = krb

    topology.standalone.config.set('nsslapd-localhost', topology.standalone.host)

    topology.standalone.sslport = None

    topology.standalone.restart()

    topology.standalone.clearTmpDir(__file__)

    def fin():
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
        if DEBUGGING:
            topology.standalone.stop()
        else:
            assert _remove_ssca_db(topology)
            if topology.standalone.exists():
                topology.standalone.delete(pyinstall=PYINSTALL)
            krb.destroy_realm()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_no_sample(request):
    """Create instance without sample entries to reproduce not initialised database"""

    topology = create_topology({ReplicaRole.STANDALONE: 1}, None)
    topology.standalone.backends.create(properties={
        'cn': 'userRoot',
        'nsslapd-suffix': DEFAULT_SUFFIX,
    })

    def fin():
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
        if DEBUGGING:
            topology.standalone.stop()
        else:
            assert _remove_ssca_db(topology)
            if topology.standalone.exists():
                topology.standalone.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_i2(request):
    """Create two instance DS deployment"""

    topology = create_topology({ReplicaRole.STANDALONE: 2})

    def fin():
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.simple_bind_s(DN_DM, PASSWORD)
                    inst.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_i3(request):
    """Create three instance DS deployment"""

    topology = create_topology({ReplicaRole.STANDALONE: 3})

    def fin():
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.simple_bind_s(DN_DM, PASSWORD)
                    inst.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology

@pytest.fixture(scope="module")
def topology_m1(request):
    """Create Replication Deployment with one master and one consumer"""

    topology = create_topology({ReplicaRole.MASTER: 1})

    def fin():
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.simple_bind_s(DN_DM, PASSWORD)
                    inst.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology

@pytest.fixture(scope="module")
def topology_m1c1(request):
    """Create Replication Deployment with one master and one consumer"""

    topology = create_topology({ReplicaRole.MASTER: 1,
                                ReplicaRole.CONSUMER: 1})

    def fin():
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.simple_bind_s(DN_DM, PASSWORD)
                    inst.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m2(request):
    """Create Replication Deployment with two masters"""

    topology = create_topology({ReplicaRole.MASTER: 2})

    def fin():
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.simple_bind_s(DN_DM, PASSWORD)
                    inst.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m3(request):
    """Create Replication Deployment with three masters"""

    topology = create_topology({ReplicaRole.MASTER: 3})

    def fin():
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.simple_bind_s(DN_DM, PASSWORD)
                    inst.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m4(request):
    """Create Replication Deployment with four masters"""

    topology = create_topology({ReplicaRole.MASTER: 4})

    def fin():
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.simple_bind_s(DN_DM, PASSWORD)
                    inst.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m2c2(request):
    """Create Replication Deployment with two masters and two consumers"""

    topology = create_topology({ReplicaRole.MASTER: 2,
                                ReplicaRole.CONSUMER: 2})

    def fin():
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.simple_bind_s(DN_DM, PASSWORD)
                    inst.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m1h1c1(request):
    """Create Replication Deployment with one master, one consumer and one hub"""

    topo_roles = {ReplicaRole.MASTER: 1, ReplicaRole.HUB: 1, ReplicaRole.CONSUMER: 1}
    topology = _create_instances(topo_roles, DEFAULT_SUFFIX)
    master = topology.ms["master1"]
    hub = topology.hs["hub1"]
    consumer = topology.cs["consumer1"]

    # Start with the master, and create it "first".
    log.info("Creating replication topology.")
    # Now get the first master ready.
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.create_first_master(master)
    # Finish the topology creation
    repl.join_hub(master, hub)
    repl.join_consumer(hub, consumer)

    repl.test_replication(master, consumer)

    # Clear out the tmp dir
    for instance in topology:
        instance.clearTmpDir(__file__)

    def fin():
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.simple_bind_s(DN_DM, PASSWORD)
                    inst.delete(pyinstall=PYINSTALL)

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology
