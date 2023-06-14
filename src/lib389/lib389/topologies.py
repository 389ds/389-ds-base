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
import time
import subprocess
from signal import SIGALRM, alarm, signal
from datetime import timedelta
import pytest
from lib389 import DirSrv
from lib389.utils import generate_ds_params, is_fips
from lib389.mit_krb5 import MitKrb5
from lib389.saslmap import SaslMappings
from lib389.replica import Agreements, ReplicationManager, Replicas
from lib389.nss_ssl import NssSsl
from lib389._constants import *
from lib389.cli_base import LogCapture

TLS_HOSTNAME_CHECK = os.getenv('TLS_HOSTNAME_CHECK', default=True)
DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


# Github kills workflow after 6 hours so lets keep 1 hour margin
# For workflow and test initialization, and artifacts collection
DEFAULT_TEST_TIMEOUT = 5 * 3600

_test_timeout = DEFAULT_TEST_TIMEOUT


def set_timeout(timeout):
    """Set the test timeout.
       There is an example about how to use it in
       https://github.com/389ds/389-ds-base/tree/main/dirsrvtests/tests/suites/lib389/timeout_test.py

    :param timeout: timeout in seconds
                    0 for no timeout
                    negative value: reset default timeout
    :type timeout: int

    :return - None
    """
    global _test_timeout
    if timeout < 0:
        _test_timeout = DEFAULT_TEST_TIMEOUT
    else:
        _test_timeout = timeout
    log.info(f"Set Topologies timeout to {str(timedelta(seconds=_test_timeout))}")


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
            instance.config.set('nsslapd-accesslog-logbuffering','off')
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
            # We should always enable TLS while in FIPS mode because otherwise NSS database won't be
            # configured in a FIPS compliant way
            if is_fips():
                instance.enable_tls()

            # Disable strict hostname checking for TLS
            if not TLS_HOSTNAME_CHECK:
                instance.config.set('nsslapd-ssl-check-hostname', 'off')
            if DEBUGGING:
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


def create_topology(topo_dict, suffix=DEFAULT_SUFFIX, request=None, cleanup_cb=None):
    """Create a requested topology. Cascading replication scenario isn't supported

    :param topo_dict: a dictionary {ReplicaRole.STANDALONE: num, ReplicaRole.SUPPLIER: num,
                                   ReplicaRole.CONSUMER: num}
    :type topo_dict: dict
    :param suffix: a suffix for the replication
    :type suffix: str
    :param request: The pytest request
    :type request: FixtureRequest
    :param cleanup_cb: a callback for additional cleanup task
    :type cleanup_cb: Callable[[TopologyMain], None]

    :return - TopologyMain object
    """

    if not topo_dict:
        ValueError("You need to specify the dict. For instance: {ReplicaRole.STANDALONE: 1}")

    if cleanup_cb and not request:
        ValueError("You need to specify the pytest fixture request when specifying cleanup callback.")

    topo = _create_instances(topo_dict, suffix)

    # register topo finalizer
    log.info(f"Topology request is {topo}")
    if request:
        log.info(f"Topology has a request")
        def fin():
            alarm(0)
            [inst.stop() for inst in topo]
            if DEBUGGING is None:
                if cleanup_cb:
                    cleanup_cb(topo)
                if not _remove_ssca_db(topo):
                    log.warning("Failed to remove the CA certificate database during the tescase cleanup phase.")
                for inst in topo:
                    if inst.exists():
                        inst.delete()

        request.addfinalizer(fin)

        # Timeout management
        def timeout(signum, frame):
            # Lets try to stop gracefully all instances and off-line "tasks".
            # In case of deadlock or loop a thread will not be able to finish
            # and the server will not die.
            log.error("Timeout. kill ns-slapd processes with SIGTERM.")
            subprocess.run(["/usr/bin/pkill", "--signal", "TERM", "ns-slapd",], stderr=subprocess.STDOUT)
            time.sleep(120)
            # Everything should be stopped except stuck instances
            # lets kill with a signal that generate core and could not
            # be confused with SIGSEGV or SIGBUS
            log.error("Timeout. kill remaining ns-slapd processes with SIGQUIT.")
            subprocess.run(["/usr/bin/pkill", "--signal", "QUIT", "ns-slapd",], stderr=subprocess.STDOUT)
            # let enough time to write the cores
            time.sleep(120)
            raise TimeoutError(f"Test timed out after {str(timedelta(seconds=_test_timeout))}")

        signal(SIGALRM, timeout)
        log.info(f"Armed timeout of {str(timedelta(seconds=_test_timeout))}")
        alarm(_test_timeout)

    # Start with a single supplier, and create it "first".
    first_supplier = None
    repl = ReplicationManager(DEFAULT_SUFFIX)
    try:
        first_supplier = list(topo.ms.values())[0]
        log.info("Creating replication topology.")
        # Now get the first supplier ready.
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

    if ReplicaRole.HUB in topo_dict.keys():
        first_hub = list(topo.hs.values())[0]
        # Initialize the hubs
        for h in topo.hs.values():
            log.info("Joining hub %s from %s ..." % (h.serverid, first_supplier.serverid))
            repl.join_hub(first_supplier, h)

        # Initialize the consumers
        # Add hub -> consumer agreements.
        for c in topo.cs.values():
            log.info("Joining consumer %s from %s ..." % (c.serverid, first_hub.serverid))
            repl.join_consumer(first_hub, c)

        # Mesh the supplier<->hub agreements.
        for mo in topo.ms.values():
            for h in topo.hs.values():
                if mo is not first_supplier:
                    log.info("Ensuring supplier %s to hub %s ..." % (mo.serverid, h.serverid))
                    repl.ensure_agreement(mo, h)
                log.info("Ensuring hub %s to supplier %s ..." % (h.serverid, mo.serverid))
                repl.ensure_agreement(h, mo)

        # Mesh the hub->consumer agreements.
        for h in topo.hs.values():
            if h is first_hub:
                continue
            for c in topo.cs.values():
                log.info("Ensuring consumer %s from hub %s ..." % (c.serverid, h.serverid))
                repl.ensure_agreement(h, c)
    else:
        # Master(s) -> Consumer(s) topologies
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

    topology = create_topology({ReplicaRole.STANDALONE: 1}, request=request)

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

    topology = create_topology({ReplicaRole.STANDALONE: 1}, request=request,
                               cleanup_cb = lambda x: krb.destroy_realm())

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

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_no_sample(request):
    """Create instance without sample entries to reproduce not initialised database"""

    topology = create_topology({ReplicaRole.STANDALONE: 1}, None, request=request)
    topology.standalone.backends.create(properties={
        'cn': 'userRoot',
        'nsslapd-suffix': DEFAULT_SUFFIX,
    })

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_i2(request):
    """Create two instance DS deployment"""

    topology = create_topology({ReplicaRole.STANDALONE: 2}, request=request)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_i3(request):
    """Create three instance DS deployment"""

    topology = create_topology({ReplicaRole.STANDALONE: 3}, request=request)

    topology.logcap = LogCapture()
    return topology

@pytest.fixture(scope="module")
def topology_m1(request):
    """Create Replication Deployment with one supplier and one consumer"""

    topology = create_topology({ReplicaRole.SUPPLIER: 1}, request=request)

    topology.logcap = LogCapture()
    return topology

@pytest.fixture(scope="module")
def topology_m1c1(request):
    """Create Replication Deployment with one supplier and one consumer"""

    topology = create_topology({ReplicaRole.SUPPLIER: 1,
                                ReplicaRole.CONSUMER: 1}, request=request)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m2(request):
    """Create Replication Deployment with two suppliers"""

    topology = create_topology({ReplicaRole.SUPPLIER: 2}, request=request)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m3(request):
    """Create Replication Deployment with three suppliers"""

    topology = create_topology({ReplicaRole.SUPPLIER: 3}, request=request)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m4(request):
    """Create Replication Deployment with four suppliers"""

    topology = create_topology({ReplicaRole.SUPPLIER: 4}, request=request)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m2c2(request):
    """Create Replication Deployment with two suppliers and two consumers"""

    topology = create_topology({ReplicaRole.SUPPLIER: 2,
                                ReplicaRole.CONSUMER: 2}, request=request)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_m1h1c1(request):
    """Create Replication Deployment with one supplier, one consumer and one hub"""

    topo_roles = {ReplicaRole.SUPPLIER: 1, ReplicaRole.HUB: 1, ReplicaRole.CONSUMER: 1}
    topology = create_topology(topo_roles, request=request)

    # Since topology implements timeout, create_topology supports hub 
    # but hub and suppliers are fully meshed while historically this topology
    # did not have hub->master agreement.
    # ==> we must remove hub->master agmt that breaks some test (like promote_demote)
    supplier = topology.ms["supplier1"]
    hub = topology.hs["hub1"]
    for agmt in Agreements(hub).list():
        if supplier.port == agmt.get_attr_val_int("nsDS5ReplicaPort"):
            agmt.delete()

    topology.logcap = LogCapture()
    return topology
