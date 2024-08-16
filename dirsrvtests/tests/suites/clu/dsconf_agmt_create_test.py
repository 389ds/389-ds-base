# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
import os
import logging
import pytest
import time
import subprocess
from lib389.topologies import topology_i2 as topo_i2
from lib389.replica import ReplicationManager, Replicas
from lib389._constants import *
from lib389.agreement import Agreements
from lib389.properties import *

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

def test_replication_agreement_creation_time(topo_i2):
    """Test the creation time of a replication agreement using dsconf command

    :id: 30b2d6a4-64a2-4732-adac-9f73b5c4d07f
    :setup: Two standalone instances
    :steps:
        1. Create and Enable Replication on standalone2 and set role as consumer
        2. Create and Enable Replication on standalone1 and set role as supplier
        3. Prepare dsconf command with replication agreement parameters
        4. Execute dsconf command to create replication agreement
        5. Measure execution time of dsconf command
        6. Verify that the agreement was created successfully
        7. Check if replication works
    :expectedresults:
        1. Replication should be enabled successfully on consumer
        2. Replication should be enabled successfully on supplier
        3. dsconf command should be prepared correctly
        4. dsconf command should execute successfully
        5. Execution time should be less than 5 seconds
        6. Agreement should be present in the supplier's configuration
        7. Replication works
    """

    # Step 1: Get instances
    supplier = topo_i2.ins["standalone1"]
    consumer = topo_i2.ins["standalone2"]

    # Step 2: Enable replication on both instances
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.create_first_supplier(supplier)
    consumer.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=ReplicaRole.CONSUMER, replicaId=CONSUMER_REPLICAID)

    # Step 3: Prepare variables for the dsconf command
    BINDDN = defaultProperties[REPLICATION_BIND_DN]
    BINDPW = defaultProperties[REPLICATION_BIND_PW]
    BIND_METHOD = defaultProperties[REPLICATION_BIND_METHOD]
    TRANSPORT = defaultProperties[REPLICATION_TRANSPORT]

    # Step 4: Prepare the dsconf command
    cmd = [
        'dsconf', '-D', 'cn=Directory Manager', '-w', PW_DM,
        f'ldap://{supplier.host}:{supplier.port}',
        'repl-agmt', 'create', f'--suffix={DEFAULT_SUFFIX}',
        f'--host={consumer.host}', f'--port={consumer.port}',
        f'--conn-protocol={TRANSPORT}', f'--bind-dn={BINDDN}',
        f'--bind-passwd={BINDPW}', f'--bind-method={BIND_METHOD}', '--init', 'example-agreement'
    ]

    log.info(f'Executing dsconf command: {" ".join(cmd)}')

    # Step 5: Execute the command and measure the time
    start_time = time.time()
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    _, stderr = process.communicate()
    end_time = time.time()

    execution_time = end_time - start_time

    # Check if the command was successful
    if process.returncode == 0:
        log.info(f'Command executed successfully in {execution_time:.2f} seconds')
    else:
        log.error(f'Command failed with error: {stderr.decode("utf-8")}')
    assert process.returncode == 0, f"Command failed with error: {stderr.decode('utf-8')}"

    # Assert that the execution time is less than 5 seconds
    log.info(f'Checking execution time: {execution_time:.2f} seconds')
    assert execution_time < 5, f"Command took too long: {execution_time:.2f} seconds"

    # Step 6: Verify that the agreement was created
    log.info('Verifying that the agreement was created')
    agreements = Agreements(supplier).list()
    assert len(agreements) == 1, "Agreement was not created successfully"

    # Step 7: Check if replication works
    replicas = Replicas(supplier)
    replica = replicas.get(DEFAULT_SUFFIX)
    log.info(f"Testing replication for {DEFAULT_SUFFIX}")
    assert replica.test_replication([consumer])

    log.info('Replication initialization completed successfully')
    log.info('Test completed successfully')

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
