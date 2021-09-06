# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import socket  # For hostname detection for GSSAPI tests
import pytest
import subprocess
from lib389 import DirSrv
from lib389.utils import generate_ds_params
from lib389.mit_krb5 import MitKrb5
from lib389.saslmap import SaslMappings
from lib389.replica import ReplicationManager, Replicas
from lib389.nss_ssl import NssSsl
from lib389._constants import *
from lib389.cli_base import LogCapture

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

    :param topo_dict: a dictionary {ReplicaRole.STANDALONE: num, ReplicaRole.SUPPLIER: num,
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
                instance.delete()

            instance.create()
            # We set a URL here to force ldap:// only. Once we turn on TLS
            # we'll flick this to ldaps.
            instance.use_ldap_uri()
            instance.open()
            if role == ReplicaRole.STANDALONE:
                ins[instance.serverid] = instance
                instances.update(ins)
            if role == ReplicaRole.SUPPLIER:
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
                instance.config.set('nsslapd-accesslog-level','260')
                instance.config.set('nsslapd-auditlog-logging-enabled','on')
                instance.config.set('nsslapd-auditfaillog-logging-enabled','on')
                instance.config.set('nsslapd-plugin-logging', 'on')
            log.info("Instance with parameters {} was created.".format(args_instance))

    if "standalone1" in instances and len(instances) == 1:
        return TopologyMain(standalones=instances["standalone1"])
    else:
        return TopologyMain(standalones=ins, suppliers=ms, consumers=cs, hubs=hs)


def create_topology(topo_dict, suffix=DEFAULT_SUFFIX):
    """Create a requested topology. Cascading replication scenario isn't supported

    :param topo_dict: a dictionary {ReplicaRole.STANDALONE: num, ReplicaRole.SUPPLIER: num,
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

    # Start with a single supplier, and create it "first".
    first_supplier = None
    try:
        first_supplier = list(topo.ms.values())[0]
        log.info("Creating replication topology.")
        # Now get the first supplier ready.
        repl = ReplicationManager(DEFAULT_SUFFIX)
        repl.create_first_supplier(first_supplier)
    except IndexError:
        pass

    # Now init the other suppliers from this.
    # This will reinit m, and put a bi-directional agreement
    # in place.
    for m in topo.ms.values():
        # Skip firstsupplier.
        if m is first_supplier:
            continue
        log.info("Joining supplier %s to %s ..." % (m.serverid, first_supplier.serverid))
        repl.join_supplier(first_supplier, m)

    # Mesh the supplier agreements.
    for mo in topo.ms.values():
        for mi in topo.ms.values():
            if mo is mi:
                continue
            log.info("Ensuring supplier %s to %s ..." % (mo.serverid, mi.serverid))
            repl.ensure_agreement(mo, mi)

    # Add supplier -> consumer agreements.
    for c in topo.cs.values():
        log.info("Joining consumer %s from %s ..." % (c.serverid, first_supplier.serverid))
        repl.join_consumer(first_supplier, c)

    for m in topo.ms.values():
        for c in topo.cs.values():
            log.info("Ensuring consumer %s from %s ..." % (c.serverid, m.serverid))
            repl.ensure_agreement(m, c)

    # Clear out the tmp dir
    for instance in topo:
        instance.clearTmpDir(__file__)

    return topo


__topologies = []

class LogFilter:
# Generic filter class that logs everything
    def __init__(self):
        self.stop_now = False
        self.last_line = None

    def filter(self, line):
        self.last_line = line
        return True

class ErrorLogFilter(LogFilter):
# Generic filter class that keep errors and critical errors everything

    def filter(self, line):
        self.last_line = line
        if "- ERR -" in line:
            return True
        if "- CRIT -" in line:
            return True
        if "MDB_MAP_FULL" in line:
            self.stop_now = True
        return False


def log2Report(path, section_name, filter=LogFilter()):
    # Concat filtered lines from a file as a string
    # filter instance must have filter.filter(line) that returns a boolean
    # and a filter.stop_now boolean
    res = f"\n *** {section_name} *** \n\n"
    with open(path) as file:
        for line in file:
            if filter.filter(line) is True:
                res += line
            if filter.stop_now is True:
                break
    return res
def getInstancesReport():
    # Capture data about stoped instances
    # Get the list of instances that are down
    stopped_instances = []
    for topology in __topologies:
        for inst in topology:
            if inst.exists() and not inst.status():
                stopped_instances.append(inst)
    if len(stopped_instances) is 0:
        return "All instances are running."
    # Get core file informations
    cmd = [ "/usr/bin/coredumpctl", "info", "ns-slapd" ]
    coreinfo = subprocess.run(cmd, capture_output=True, shell=False, check=False, text=True)
    res = "Core files:\n"
    res += coreinfo.stdout
    res += "\n\ncoredumpctl STDERR\n"
    res += coreinfo.stderr
    res += "\n"

    # Get error log informations
    for inst in stopped_instances:
        res += f"Instance {inst.getServerId()} is not running:\nCore file information {str(cmd)}:\n"
        # Let get the important data in error log file 
        path = inst.ds_paths.error_log.format(instance_name=inst.getServerId())
        logFilter = ErrorLogFilter()
        res1 = log2Report(path, f"Extract of instance {inst.getServerId()} error log", logFilter)
        if '- INFO - main - slapd stopped.' in logFilter.last_line:
            res1 += logFilter.line
        if logFilter.stop_now:
            res1 = log2Report(path, f"Instance {inst.getServerId()} error log")
            path = inst.ds_paths.access_log.format(instance_name=inst.getServerId())
            res1 += log2Report(path, f"Instance {inst.getServerId()} access log")
        res += res1
    return res
class TopologyMain(object):
    def __init__(self, standalones=None, suppliers=None, consumers=None, hubs=None):
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
        if suppliers:
            self.ms = suppliers
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
    __topologies.append(topology)

    def fin():
        topology.standalone.stop()
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            if topology.standalone.exists():
                topology.standalone.delete()

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
    __topologies.append(topology)

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
        topology.standalone.stop()
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            if topology.standalone.exists():
                topology.standalone.delete()
            krb.destroy_realm()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_no_sample(request):
    """Create instance without sample entries to reproduce not initialised database"""

    topology = create_topology({ReplicaRole.STANDALONE: 1}, None)
    __topologies.append(topology)
    topology.standalone.backends.create(properties={
        'cn': 'userRoot',
        'nsslapd-suffix': DEFAULT_SUFFIX,
    })

    def fin():
        topology.standalone.stop()
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            if topology.standalone.exists():
                topology.standalone.delete()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_i2(request):
    """Create two instance DS deployment"""

    topology = create_topology({ReplicaRole.STANDALONE: 2})
    __topologies.append(topology)

    def fin():
        [inst.stop() for inst in topology]
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.delete()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_i3(request):
    """Create three instance DS deployment"""

    topology = create_topology({ReplicaRole.STANDALONE: 3})
    __topologies.append(topology)

    def fin():
        [inst.stop() for inst in topology]
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.delete()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology

@pytest.fixture(scope="module")
def topology_m1(request):
    """Create Replication Deployment with one supplier and one consumer"""

    topology = create_topology({ReplicaRole.SUPPLIER: 1})
    __topologies.append(topology)

    def fin():
        __topologies.remove(topology)
        [inst.stop() for inst in topology]
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.delete()


    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology

@pytest.fixture(scope="module")
def topology_m1c1(request):
    """Create Replication Deployment with one supplier and one consumer"""

    topology = create_topology({ReplicaRole.SUPPLIER: 1,
                                ReplicaRole.CONSUMER: 1})
    __topologies.append(topology)

    def fin():
        [inst.stop() for inst in topology]
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.delete()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m2(request):
    """Create Replication Deployment with two suppliers"""

    topology = create_topology({ReplicaRole.SUPPLIER: 2})
    __topologies.append(topology)

    def fin():
        [inst.stop() for inst in topology]
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.delete()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m3(request):
    """Create Replication Deployment with three suppliers"""

    topology = create_topology({ReplicaRole.SUPPLIER: 3})
    __topologies.append(topology)

    def fin():
        [inst.stop() for inst in topology]
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.delete()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m4(request):
    """Create Replication Deployment with four suppliers"""

    topology = create_topology({ReplicaRole.SUPPLIER: 4})
    __topologies.append(topology)

    def fin():
        [inst.stop() for inst in topology]
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.delete()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m2c2(request):
    """Create Replication Deployment with two suppliers and two consumers"""

    topology = create_topology({ReplicaRole.SUPPLIER: 2,
                                ReplicaRole.CONSUMER: 2})
    __topologies.append(topology)

    def fin():
        [inst.stop() for inst in topology]
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.delete()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m1h1c1(request):
    """Create Replication Deployment with one supplier, one consumer and one hub"""

    topo_roles = {ReplicaRole.SUPPLIER: 1, ReplicaRole.HUB: 1, ReplicaRole.CONSUMER: 1}
    topology = _create_instances(topo_roles, DEFAULT_SUFFIX)
    __topologies.append(topology)
    supplier = topology.ms["supplier1"]
    hub = topology.hs["hub1"]
    consumer = topology.cs["consumer1"]

    # Start with the supplier, and create it "first".
    log.info("Creating replication topology.")
    # Now get the first supplier ready.
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.create_first_supplier(supplier)
    # Finish the topology creation
    repl.join_hub(supplier, hub)
    repl.join_consumer(hub, consumer)

    repl.test_replication(supplier, consumer)

    # Clear out the tmp dir
    for instance in topology:
        instance.clearTmpDir(__file__)

    def fin():
        [inst.stop() for inst in topology]
        __topologies.remove(topology)
        if DEBUGGING is None:
            assert _remove_ssca_db(topology)
            for inst in topology:
                if inst.exists():
                    inst.delete()

    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology
