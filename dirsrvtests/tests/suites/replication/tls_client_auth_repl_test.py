# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.topologies import topology_m2 as topo_m2

from lib389.idm.organisationalunit import OrganisationalUnits
from lib389.idm.group import Groups
from lib389.idm.services import ServiceAccounts
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES

from lib389.nss_ssl import NssSsl

from lib389.config import CertmapLegacy

from lib389._constants import DEFAULT_SUFFIX

from lib389.replica import ReplicationManager, Replicas

def test_tls_client_auth(topo_m2):
    """Test TLS client authentication between two masters operates
    as expected.

    :id: 922d16f8-662a-4915-a39e-0aecd7c8e6e6
    :steps:
        1. Enable TLS on both masters
        2. Reconfigure both agreements to use TLS Client auth
        3. Ensure replication events work
    :expectedresults:
        1. Tls is setup
        2. The configuration works, and authentication works
        3. Replication ... replicates.
    """
    m1 = topo_m2.ms['master1']
    m2 = topo_m2.ms['master2']
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
        ('nsDS5ReplicaTransportInfo', 'SSL'),
        ('nsDS5ReplicaPort', '%s' % m2.sslport),
    )
    agmt_m1.remove_all('nsDS5ReplicaBindDN')

    replica_m2 = Replicas(m2).get(DEFAULT_SUFFIX)
    agmt_m2 = replica_m2.get_agreements().list()[0]

    agmt_m2.replace_many(
        ('nsDS5ReplicaBindMethod', 'SSLCLIENTAUTH'),
        ('nsDS5ReplicaTransportInfo', 'SSL'),
        ('nsDS5ReplicaPort', '%s' % m1.sslport),
    )
    agmt_m2.remove_all('nsDS5ReplicaBindDN')

    repl.test_replication(m1, m2)
    repl.test_replication(m2, m1)



