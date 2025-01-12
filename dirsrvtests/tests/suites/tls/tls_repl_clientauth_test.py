# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import pytest
import logging
from lib389.replica import Replicas, ReplicationManager
from lib389._constants import DEFAULT_SUFFIX
from lib389.config import CertmapLegacy
from lib389.idm.services import ServiceAccounts
from lib389.topologies import topology_m2 as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def topo_tls_ldapi(topo):
    """Enable TLS on both suppliers and reconfigure both agreements
    to use TLS Client auth. Also, setup ldapi and export DB
    """

    m1, m2 = topo.ms["supplier1"], topo.ms["supplier2"]
    
    # Create and configure certmaps
    cm_m1, cm_m2 = CertmapLegacy(m1), CertmapLegacy(m2)
    certmaps = cm_m1.list()
    certmaps['default'].update({'DNComps': None, 'CmapLdapAttr': 'nsCertSubjectDN'})
    cm_m1.set(certmaps)
    cm_m2.set(certmaps)

    # Enable TLS on both instances
    for instance in topo:
        instance.enable_tls()

    # Create replication DNs
    services = ServiceAccounts(m1, DEFAULT_SUFFIX)
    for instance in (m1, m2):
        repl = services.get(f'{instance.host}:{instance.sslport}')
        repl.set('nsCertSubjectDN', instance.get_server_tls_subject())

    # Check the replication is "done".
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(m1, m2)

    # Now change the auth type
    for instance, other in [(m1, m2), (m2, m1)]:
        replica = Replicas(instance).get(DEFAULT_SUFFIX)
        agmt = replica.get_agreements().list()[0]
        agmt.replace_many(
            ('nsDS5ReplicaBindMethod', 'SSLCLIENTAUTH'),
            ('nsDS5ReplicaTransportInfo', 'SSL'),
            ('nsDS5ReplicaPort', str(other.sslport)),
        )
        agmt.remove_all('nsDS5ReplicaBindDN')

    # Set up LDAPI
    for instance in topo:
        instance.config.set('nsslapd-ldapilisten', 'on')
        instance.config.set('nsslapd-ldapifilepath', f'/var/run/slapd-{instance.serverid}.socket')
        instance.restart()

    repl.test_replication(m1, m2)
    repl.test_replication(m2, m1)

    return topo


def find_ca_files():
    ca_files = []
    for root, dirs, files in os.walk('/tmp'):
        if 'Self-Signed-CA.pem' in files and 'dirsrv@' in root:
            ca_files.append(os.path.join(root, 'Self-Signed-CA.pem'))
    return ca_files


def test_new_tls_context_error(topo_tls_ldapi):
    """Test TLS context error when CA certificate is removed

    :id: 88d4c841-9f91-499b-ba30-f834225effd8
    :setup: Two supplier replication with SSLCLIENTAUTH agmts
    :steps:
        1. Remove tmp's Self-Signed-CA.pem dirsrv file
        2. Reinit agreement
        3. Check errors log and make sure the detailed error is there
    :expectedresults:
        1. Self-Signed-CA.pem file is removed
        2. Replication reinitialization fails
        3. Error log contains the expected SSL certificate verify failed message
    """

    m1 = topo_tls_ldapi.ms["supplier1"]

    log.info('Find and remove Self-Signed-CA.pem dirsrv files')
    ca_files = find_ca_files()
    if not ca_files:
        pytest.skip("No Self-Signed-CA.pem files found. Skipping test.")

    log.info(f'Found {len(ca_files)} Self-Signed-CA.pem files')
    log.info(f'CA files are: {", ".join(ca_files)}')

    for ca_file in ca_files:
        try:
            os.remove(ca_file)
            log.info(f"Removed file: {ca_file}")
        except OSError as e:
            log.info(f"Error removing file {ca_file}: {e}")

    log.info('Reinit agreement')
    replica_m1 = Replicas(m1).get(DEFAULT_SUFFIX)
    agmt_m1 = replica_m1.get_agreements().list()[0]
    agmt_m1.begin_reinit()
    agmt_m1.wait_reinit()
    
    log.info('Restart the server to flush logs')
    m1.restart()

    log.info('Check errors log for certificate verify failed message')
    error_msg = "error:80000002:system library::No such file or directory"
    assert m1.searchErrorsLog(error_msg), f"Expected error message not found: {error_msg}"

    log.info('Test completed successfully')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
