# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import time
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_m4
from lib389.tasks import CleanAllRUVTask
from lib389.replica import ReplicationManager, Replicas
from lib389.config import CertmapLegacy
from lib389.idm.services import ServiceAccounts

log = logging.getLogger(__name__)


def test_clean_shutdown_crash(topology_m2):
    """Check that server didn't crash after shutdown when running CleanAllRUV task

    :id: c34d0b40-3c3e-4f53-8656-5e4c2a310aaf
    :setup: Replication setup with two suppliers
    :steps:
        1. Enable TLS on both suppliers
        2. Reconfigure both agreements to use TLS Client auth
        3. Stop supplier2
        4. Run the CleanAllRUV task
        5. Restart supplier1
        6. Check if supplier1 didn't crash
        7. Restart supplier1 again
        8. Check if supplier1 didn't crash

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """

    m1 = topology_m2.ms["supplier1"]
    m2 = topology_m2.ms["supplier2"]

    repl = ReplicationManager(DEFAULT_SUFFIX)

    cm_m1 = CertmapLegacy(m1)
    cm_m2 = CertmapLegacy(m2)

    certmaps = cm_m1.list()
    certmaps['default']['DNComps'] = None
    certmaps['default']['CmapLdapAttr'] = 'nsCertSubjectDN'

    cm_m1.set(certmaps)
    cm_m2.set(certmaps)

    log.info('Enabling TLS')
    [i.enable_tls() for i in topology_m2]

    log.info('Creating replication dns')
    services = ServiceAccounts(m1, DEFAULT_SUFFIX)
    repl_m1 = services.get('%s:%s' % (m1.host, m1.sslport))
    repl_m1.set('nsCertSubjectDN', m1.get_server_tls_subject())

    repl_m2 = services.get('%s:%s' % (m2.host, m2.sslport))
    repl_m2.set('nsCertSubjectDN', m2.get_server_tls_subject())

    log.info('Changing auth type')
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

    log.info('Stopping supplier2')
    m2.stop()

    log.info('Run the cleanAllRUV task')
    cruv_task = CleanAllRUVTask(m1)
    cruv_task.create(properties={
        'replica-id': repl.get_rid(m1),
        'replica-base-dn': DEFAULT_SUFFIX,
        'replica-force-cleaning': 'no',
        'replica-certify-all': 'yes'
    })

    m1.restart()

    log.info('Check if supplier1 crashed')
    assert not m1.detectDisorderlyShutdown()

    log.info('Repeat')
    m1.restart()
    assert not m1.detectDisorderlyShutdown()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

