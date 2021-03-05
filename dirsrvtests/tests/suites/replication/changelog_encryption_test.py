import logging
import pytest
import os
import time
from lib389._constants import DEFAULT_SUFFIX,  DN_CHANGELOG,  DN_USERROOT_LDBM
from lib389.topologies import topology_m1c1 as topo
from lib389.dseldif import DSEldif
from lib389.utils import ds_supports_new_changelog
from lib389.replica import Replicas

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_cl_encryption_setup_process(topo):
    """Take an already working replication deployment, and setup changelog
    encryption

    :id: 1a1b7d29-69f5-4f0e-91c4-e7f66140ff17
    :setup: Supplier Instance, Consumer Instance
    :steps:
        1. Enable TLS for the server
        2. Export changelog
        3. Enable changelog encryption
        4. Import changelog
        5. Verify replication is still working
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    supplier = topo.ms['supplier1']
    consumer = topo.cs['consumer1']

    # Enable TLS
    log.info('Enable TLS ...')
    supplier.enable_tls()
    consumer.enable_tls()

    # Export changelog
    log.info('Export changelog ...')
    replicas = Replicas(supplier)
    replica = replicas.get(DEFAULT_SUFFIX)
    replica.begin_task_cl2ldif()
    replica.task_finished()

    # Enable changelog encryption
    log.info('Enable changelog encryption ...')
    dse_ldif = DSEldif(supplier)
    supplier.stop()
    if ds_supports_new_changelog():
        changelog = 'cn=changelog,{}'.format(DN_USERROOT_LDBM)
    else:
        changelog = DN_CHANGELOG
    dse_ldif.replace(changelog, 'nsslapd-encryptionalgorithm', 'AES')
    if dse_ldif.get(changelog, 'nsSymmetricKey'):
        dse_ldif.delete(changelog, 'nsSymmetricKey')
    supplier.start()

    # Import changelog
    log.info('Import changelog ...')
    replica.begin_task_ldif2cl()
    replica.task_finished()

    # Verify replication is still working
    log.info('Test replication is still working ...')
    assert replica.test_replication([consumer])

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

