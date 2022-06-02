# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import pytest
from lib389.utils import ds_is_older
from lib389.idm.services import ServiceAccounts
from lib389.config import CertmapLegacy
from lib389._constants import DEFAULT_SUFFIX
from lib389.replica import ReplicationManager, Replicas
from lib389.topologies import topology_m2 as topo_m2

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def tls_client_auth(topo_m2):
    """Enable TLS on both suppliers and reconfigure
    both agreements to use TLS Client auth
    """

    m1 = topo_m2.ms['supplier1']
    m2 = topo_m2.ms['supplier2']

    if ds_is_older('1.4.0.6'):
        transport = 'SSL'
    else:
        transport = 'LDAPS'

    # Create the certmap before we restart for enable_tls
    cm_m1 = CertmapLegacy(m1)
    cm_m2 = CertmapLegacy(m2)

    # We need to configure the same maps for both ....
    certmaps = cm_m1.list()
    certmaps['default']['DNComps'] = None
    certmaps['default']['CmapLdapAttr'] = 'nsCertSubjectDN'

    cm_m1.set(certmaps)
    cm_m2.set(certmaps)

    [i.enable_tls() for i in topo_m2]

    # Create the replication dns
    services = ServiceAccounts(m1, DEFAULT_SUFFIX)
    repl_m1 = services.get('%s:%s' % (m1.host, m1.sslport))
    repl_m1.set('nsCertSubjectDN', m1.get_server_tls_subject())

    repl_m2 = services.get('%s:%s' % (m2.host, m2.sslport))
    repl_m2.set('nsCertSubjectDN', m2.get_server_tls_subject())

    # Check the replication is "done".
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(m1, m2)
    # Now change the auth type

    replica_m1 = Replicas(m1).get(DEFAULT_SUFFIX)
    agmt_m1 = replica_m1.get_agreements().list()[0]

    agmt_m1.replace_many(
        ('nsDS5ReplicaBindMethod', 'SSLCLIENTAUTH'),
        ('nsDS5ReplicaTransportInfo', transport),
        ('nsDS5ReplicaPort', str(m2.sslport)),
    )
    agmt_m1.remove_all('nsDS5ReplicaBindDN')

    replica_m2 = Replicas(m2).get(DEFAULT_SUFFIX)
    agmt_m2 = replica_m2.get_agreements().list()[0]

    agmt_m2.replace_many(
        ('nsDS5ReplicaBindMethod', 'SSLCLIENTAUTH'),
        ('nsDS5ReplicaTransportInfo', transport),
        ('nsDS5ReplicaPort', str(m1.sslport)),
    )
    agmt_m2.remove_all('nsDS5ReplicaBindDN')

    repl.test_replication_topology(topo_m2)

    return topo_m2


def test_ssl_transport(tls_client_auth):
    """Test different combinations for nsDS5ReplicaTransportInfo values

    :id: a3157108-cb98-43e9-ba16-8fb21a4a03e9
    :setup: Two supplier replication, enabled TLS client auth
    :steps:
        1. Set nsDS5ReplicaTransportInfoCheck: SSL or StartTLS or TLS
        2. Restart the instance
        3. Check that replication works
        4. Set nsDS5ReplicaTransportInfoCheck: LDAPS back
    :expectedresults:
        1. Success
        2. Success
        3. Replication works
        4. Success
    """

    m1 = tls_client_auth.ms['supplier1']
    m2 = tls_client_auth.ms['supplier2']
    repl = ReplicationManager(DEFAULT_SUFFIX)
    replica_m1 = Replicas(m1).get(DEFAULT_SUFFIX)
    replica_m2 = Replicas(m2).get(DEFAULT_SUFFIX)
    agmt_m1 = replica_m1.get_agreements().list()[0]
    agmt_m2 = replica_m2.get_agreements().list()[0]

    if ds_is_older('1.4.0.6'):
        check_list = (('TLS', False),)
    else:
        check_list = (('SSL', True), ('StartTLS', False), ('TLS', False))

    for transport, secure_port in check_list:
        agmt_m1.replace_many(('nsDS5ReplicaTransportInfo', transport),
                             ('nsDS5ReplicaPort', '{}'.format(m2.port if not secure_port else m2.sslport)))
        agmt_m2.replace_many(('nsDS5ReplicaTransportInfo', transport),
                             ('nsDS5ReplicaPort', '{}'.format(m1.port if not secure_port else m1.sslport)))
        repl.test_replication_topology(tls_client_auth)

    if ds_is_older('1.4.0.6'):
        agmt_m1.replace_many(('nsDS5ReplicaTransportInfo', 'SSL'),
                             ('nsDS5ReplicaPort', str(m2.sslport)))
        agmt_m2.replace_many(('nsDS5ReplicaTransportInfo', 'SSL'),
                             ('nsDS5ReplicaPort', str(m1.sslport)))
    else:
        agmt_m1.replace_many(('nsDS5ReplicaTransportInfo', 'LDAPS'),
                             ('nsDS5ReplicaPort', str(m2.sslport)))
        agmt_m2.replace_many(('nsDS5ReplicaTransportInfo', 'LDAPS'),
                             ('nsDS5ReplicaPort', str(m1.sslport)))
    repl.test_replication_topology(tls_client_auth)


def test_extract_pemfiles(tls_client_auth):
    """Test TLS client authentication between two suppliers operates
    as expected with 'on' and 'off' options of nsslapd-extract-pemfiles

    :id: 922d16f8-662a-4915-a39e-0aecd7c8e6e1
    :setup: Two supplier replication, enabled TLS client auth
    :steps:
        1. Check that nsslapd-extract-pemfiles default value is right
        2. Check that replication works with both 'on' and 'off' values
    :expectedresults:
        1. Success
        2. Replication works
    """

    m1 = tls_client_auth.ms['supplier1']
    m2 = tls_client_auth.ms['supplier2']
    repl = ReplicationManager(DEFAULT_SUFFIX)

    if ds_is_older('1.3.7'):
        default_val = 'off'
    else:
        default_val = 'on'
    attr_val = m1.config.get_attr_val_utf8('nsslapd-extract-pemfiles')
    log.info("Check that nsslapd-extract-pemfiles is {}".format(default_val))
    assert attr_val == default_val

    for extract_pemfiles in ('on', 'off'):
        log.info("Set nsslapd-extract-pemfiles = '{}' and check replication works)")
        m1.config.set('nsslapd-extract-pemfiles', extract_pemfiles)
        m2.config.set('nsslapd-extract-pemfiles', extract_pemfiles)
        repl.test_replication_topology(tls_client_auth)

