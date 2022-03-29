# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import pytest
import ldap
import uuid
from lib389.utils import ds_is_older, valgrind_enable, valgrind_disable, valgrind_get_results_file, valgrind_check_file

from lib389.idm.services import ServiceAccounts
from lib389.idm.group import Groups
from lib389.config import CertmapLegacy, Config
from lib389._constants import DEFAULT_SUFFIX
from lib389.agreement import Agreements
from lib389._mapped_object import DSLdapObject
from lib389.replica import ReplicationManager, Replicas, BootstrapReplicationManager
from lib389.topologies import topology_m2 as topo_m2

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

def set_sasl_md5_client_auth(inst, to):
    # Create the certmap before we restart
    cm = CertmapLegacy(to)
    certmaps = cm.list()
    certmaps['default']['nsSaslMapRegexString'] = '^dn:\\(.*\\)'
    certmaps['default']['nsSaslMapBaseDNTemplate'] = 'cn=config'
    certmaps['default']['nsSaslMapFilterTemplate'] = '(objectclass=*)'
    cm.set(certmaps)

    Config(to).replace("passwordStorageScheme", 'CLEAR')

    # Create a repl manager on the replica
    replication_manager_pwd = 'secret12'
    brm = BootstrapReplicationManager(to)
    try:
        brm.delete()
    except ldap.NO_SUCH_OBJECT:
        pass
    brm.create(properties={
        'cn': brm.common_name,
        'userPassword': replication_manager_pwd
    })
    replication_manager_dn = brm.dn

    replica = Replicas(inst).get(DEFAULT_SUFFIX)
    replica.set('nsDS5ReplicaBindDN', brm.dn)
    replica.remove_all('nsDS5ReplicaBindDNgroup')
    agmt = replica.get_agreements().list()[0]
    agmt.replace_many(
        ('nsDS5ReplicaBindMethod', 'SASL/DIGEST-MD5'),
        ('nsDS5ReplicaTransportInfo', 'LDAP'),
        ('nsDS5ReplicaPort', str(to.port)),
        ('nsDS5ReplicaBindDN', replication_manager_dn),
        ('nsDS5ReplicaCredentials', replication_manager_pwd),
    )


def gen_valgrind_wrapper(dir):
    name=f"{dir}/VALGRIND"
    with open(name, 'w') as f:
        f.write('#!/bin/sh\n')
        f.write('export SASL_PATH=foo\n')
        f.write(f'valgrind -q --tool=memcheck --leak-check=yes --leak-resolution=high --num-callers=50 --log-file=/var/tmp/slapd.vg.$$ {dir}/ns-slapd.original "$@"\n')
    os.chmod(name, 0o755)
    return name

@pytest.fixture
def use_valgrind(topo_m2, request):
    """Adds entries to the supplier1"""

    log.info("Enable valgrind")
    m1 = topo_m2.ms['supplier1']
    m2 = topo_m2.ms['supplier2']
    if m1.has_asan():
        pytest.skip('Tescase using valgring cannot run on asan enabled build')
        return
    set_sasl_md5_client_auth(m1, m2)
    set_sasl_md5_client_auth(m2, m1)
    m1.stop()
    m2.stop()
    m1.systemd_override = False
    m2.systemd_override = False
    valgrind_enable(m1.ds_paths.sbin_dir, gen_valgrind_wrapper(m1.ds_paths.sbin_dir))

    def fin():
        log.info("Disable valgrind")
        valgrind_disable(m1.ds_paths.sbin_dir)

    request.addfinalizer(fin)


def test_repl_sasl_md5_auth(topo_m2):
    """Test replication with SASL digest-md5 authentication

    :id: 922d16f8-662a-4915-a39e-0aecd7c8e6e2
    :setup: Two supplier replication
    :steps:
        1. Set sasl digest/md4 on both suppliers 
        2. Restart the instance
        3. Check that replication works
    :expectedresults:
        1. Success
        2. Success
        3. Replication works
    """

    m1 = topo_m2.ms['supplier1']
    m2 = topo_m2.ms['supplier2']

    set_sasl_md5_client_auth(m1, m2)
    set_sasl_md5_client_auth(m2, m1)

    m1.restart()
    m2.restart()

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication_topology(topo_m2)


@pytest.mark.skipif(not os.path.exists('/usr/bin/valgrind'), reason="valgrind is not installed.")
def test_repl_sasl_leak(topo_m2, use_valgrind):
    """Test replication with SASL digest-md5 authentication

    :id: 180e088e-841c-11ec-af4f-482ae39447e5
    :setup: Two supplier replication,  valgrind
    :steps:
        1. Set sasl digest/md4 on both suppliers 
        2. Break sasl by setting invalid PATH
        3. Restart the instances
        4. Perform a change
        5. Poke replication 100 times
        6. Stop server
        7. Check presence of "SASL(-4): no mechanism available: No worthy mechs found" message in error log
        8 Check that there is no leak about slapi_ldap_get_lderrno
    :expectedresults:
        1. Success
        2. Success
        2. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """

    m1 = topo_m2.ms['supplier1']
    m2 = topo_m2.ms['supplier2']

    os.environ["SASL_PATH"] = 'foo'

    m1.start()
    m2.start()

    resfile=valgrind_get_results_file(m1)

    # Perform a change
    from_groups = Groups(m1, basedn=DEFAULT_SUFFIX, rdn=None)
    from_group = from_groups.get('replication_managers')
    change = str(uuid.uuid4())
    from_group.replace('description', change)

    # Poke replication to trigger thev leak
    replica = Replicas(m1).get(DEFAULT_SUFFIX)
    agmt = Agreements(m1, replica.dn).list()[0]
    for i in range(0, 100):
        agmt.pause()
        agmt.resume()

    m1.stop()
    assert m1.searchErrorsLog("worthy")
    assert not valgrind_check_file(resfile, 'slapi_ldap_get_lderrno');

