# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import ldap
import pytest
import time
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts
from lib389.replica import ReplicationManager
from lib389.schema import Schema
from lib389.topologies import topology_m2 as topology

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

def trigger_update(topology, user_rdn, num):
    """Trigger an update on the supplier to start a replication
    session and a schema push.
    """
    users_m1 = UserAccounts(topology.ms["supplier1"], DEFAULT_SUFFIX)
    user = users_m1.get(user_rdn)
    user.replace('telephonenumber', str(num))

    # Wait for the update to be replicated
    users_m2 = UserAccounts(topology.ms["supplier2"], DEFAULT_SUFFIX)
    for _ in range(30):
        try:
            user = users_m2.get(user_rdn)
            val = user.get_attr_val_int('telephonenumber')
            if val == num:
                log.debug(f"Replication successful for {user_rdn} with value {num}.")
                return
            time.sleep(1)
            log.debug(f"Waiting for replication: received {val}, expected {num}.")
        except ldap.NO_SUCH_OBJECT:
            log.debug("User not found, retrying...")
            time.sleep(1)

def test_custom_schema_rewrites_standard_schema(topology):
    """Test if custom schema modifications are properly replicated between servers
    and they persist after server restarts

    :id: 385a46d9-288f-41f8-8fde-2b83c7ca2aec
    :setup: Two supplier replication topology
    :steps:
        1. Create a test user on the first supplier.
        2. Retrieve and modify the schema on the first supplier, changing 'surname' attribute's matching rule.
        3. Trigger replication and wait for completion.
        4. Restart both suppliers.
        5. Verify schema replication and persistence across restarts.
        6. Check that the custom schema is present in the 99user.ldif file.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    replication_manager = ReplicationManager(DEFAULT_SUFFIX)
    m1 = topology.ms["supplier1"]
    m2 = topology.ms["supplier2"]

    # Create a test user on m1
    users_m1 = UserAccounts(m1, DEFAULT_SUFFIX)
    test_user = users_m1.create_test_user()

    # Retrieve and modify the schema on m1
    schema_m1 = Schema(m1)
    schema_m2 = Schema(m2)
    surname_attr_type = schema_m1.query_attributetype('surname')[0]
    assert 'caseIgnoreMatch' in schema_m1.query_attributetype('surname')[0], "Standard schema not found in m1."

    surname_attr_type = surname_attr_type.replace('caseIgnoreMatch', 'caseIgnoreIA5Match')
    surname_attr_type = surname_attr_type.replace('caseIgnoreSubstringsMatch', 'caseIgnoreIA5SubstringsMatch')
    schema_m1.add('attributetypes', surname_attr_type)

    # Trigger replication and wait for completion
    trigger_update(topology, test_user.rdn, 1)
    replication_manager.wait_for_replication(m1, m2)

    # Verify schema replication
    assert m1.schema.get_schema_csn() == m2.schema.get_schema_csn(), "Schema CSN mismatch after replication."
    assert 'caseIgnoreIA5Match' in schema_m1.query_attributetype('surname')[0], "Custom schema not found in m1."
    assert 'caseIgnoreIA5Match' in schema_m2.query_attributetype('surname')[0], "Custom schema not replicated to m2."

    # Restart servers to ensure persistence of changes
    m1.restart()
    m2.restart()

    # Verify schema persistence post-restart
    assert 'caseIgnoreIA5Match' in schema_m1.query_attributetype('surname')[0], "Custom schema not persisted in m1."
    assert 'caseIgnoreIA5Match' in schema_m2.query_attributetype('surname')[0], "Custom schema not persisted in m2."

    # Test that the custom schema is present in schema 99user.ldif in server's schema dir
    schema_filename = (m2.schemadir + "/99user.ldif")
    with open(schema_filename, 'r') as f:
        assert 'caseIgnoreIA5SubstringsMatch' in f.read(), "Custom schema not found in 99user.ldif."
