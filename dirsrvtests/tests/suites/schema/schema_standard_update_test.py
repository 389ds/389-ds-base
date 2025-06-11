# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import ldap
import pytest
import time
import pwd, grp
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts
from lib389.replica import ReplicationManager, ReplicaRole
from lib389.schema import Schema
from lib389.topologies import topology_st, create_topology

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

DEBUGGING = os.getenv("DEBUGGING", default=False)


# Redefine some fixtures so we can use them with function scope
@pytest.fixture(scope="function")
def topology(request):
    """Create Replication Deployment with two suppliers"""

    topology = create_topology({ReplicaRole.SUPPLIER: 2})

    def fin():
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            [inst.delete() for inst in topology]
    request.addfinalizer(fin)

    return topology


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


def validate_schema_changes(topology, attr_name, rule_to_verify, m1_not_present=False, m2_not_present=False):
    """Validates a schema change after replication and restart."""

    m1 = topology.ms["supplier1"]
    m2 = topology.ms["supplier2"]
    schema_m1 = Schema(m1)
    schema_m2 = Schema(m2)

    # Verify schema replication
    assert m1.schema.get_schema_csn() == m2.schema.get_schema_csn(), "Schema CSN mismatch after replication."
    assert f'{rule_to_verify}Match' not in schema_m1.query_attributetype(attr_name)[0] if m1_not_present else \
        f'{rule_to_verify}Match' in schema_m1.query_attributetype(attr_name)[0], "Custom schema incorrectly replicated to m1."
    assert f'{rule_to_verify}Match' not in schema_m2.query_attributetype(attr_name)[0] if m2_not_present else \
        f'{rule_to_verify}Match' in schema_m2.query_attributetype(attr_name)[0], "Custom schema incorrectly replicated to m2."

    # Test that the custom schema is present in schema 99user.ldif in server's schema dir
    schema_filename = (m2.schemadir + "/99user.ldif")
    with open(schema_filename, 'r') as f:
        assert f'{rule_to_verify}SubstringsMatch' not in f.read() if m2_not_present else \
            f'{rule_to_verify}SubstringsMatch' in f.read(), "Custom schema incorrectly replicated to 99user.ldif."


def test_subset_syntax_accepted(topology):
    """Test that a superset syntax is rejected when modifying the schema.

    :id: 067de984-dea7-4701-805d-d4eca1a35d14
    :setup: Two supplier replication topology
    :steps:
        1. Create a test user on the first supplier.
        2. Create testSn attribute type with IA5String syntax
        3. Verify it's replicated to the second supplier
        4. Change the syntax to Directory String
        5. Verify it's replicated to the second supplier
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    replication_manager = ReplicationManager(DEFAULT_SUFFIX)
    m1 = topology.ms["supplier1"]
    m2 = topology.ms["supplier2"]

    # Create a test user on m1
    users_m1 = UserAccounts(m1, DEFAULT_SUFFIX)
    test_user = users_m1.create_test_user()

    # Retrieve and modify the schema on m1
    schema_m1 = Schema(m1)
    surname_attr_type = schema_m1.query_attributetype('surname')[0]
    assert 'caseIgnoreMatch' in schema_m1.query_attributetype('surname')[0], "Standard schema not found in m1."

    surname_attr_type = surname_attr_type.replace('sn', 'testSn')
    surname_attr_type = surname_attr_type.replace('surName', 'testSurname')
    surname_attr_type = surname_attr_type.replace('2.5.4.4', '1.3.6.1.4.1.131313.9999.1')
    surname_attr_type = surname_attr_type.replace('RFC 4519', 'user defined')
    surname_attr_type = surname_attr_type.replace('caseIgnoreMatch', 'caseIgnoreIA5Match')
    surname_attr_type = surname_attr_type.replace('caseIgnoreSubstringsMatch', 'caseIgnoreIA5SubstringsMatch')
    surname_attr_type = surname_attr_type.replace('1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.26')
    schema_m1.add('attributetypes', surname_attr_type)

    # Trigger replication and wait for completion
    trigger_update(topology, test_user.rdn, 1)
    replication_manager.wait_for_replication(m1, m2)

    validate_schema_changes(topology, 'testSurname', 'caseIgnoreIA5')

    # Restart servers to ensure persistence of changes
    m1.restart()
    m2.restart()

    validate_schema_changes(topology, 'testSurname', 'caseIgnoreIA5')

    # Change to a different matching rule and Directory String syntax on supplier1
    m2.stop()
    surname_attr_type = schema_m1.query_attributetype('testSurname')[0]
    assert 'caseIgnoreIA5Match' in schema_m1.query_attributetype('testSurname')[0], "Standard schema not found in m1."
    schema_m1.remove_attributetype('testSurname')

    surname_attr_type = surname_attr_type.replace('caseIgnoreIA5Match', 'caseIgnoreMatch')
    surname_attr_type = surname_attr_type.replace('caseIgnoreIA5SubstringsMatch', 'caseIgnoreSubstringsMatch')
    surname_attr_type = surname_attr_type.replace('1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.15')
    schema_m1.add('attributetypes', surname_attr_type)
    m2.start()

    # Trigger replication and wait for completion
    trigger_update(topology, test_user.rdn, 2)
    replication_manager.wait_for_replication(m1, m2)

    validate_schema_changes(topology, 'testSurname', 'caseIgnore')

    # Restart servers to ensure persistence of changes
    m1.restart()
    m2.restart()

    validate_schema_changes(topology, 'testSurname', 'caseIgnore')


def test_standard_schema_superset_change_correctly_rejected(topology):
    """Test if custom schema modifications are properly rejected when
    changing to a superset syntax.

    :id: ea8a32b0-9901-4ec9-8ed4-dda551a3eb43
    :setup: Two supplier replication topology
    :steps:
        1. Create a test user on the first supplier.
        2. Retrieve and modify the schema on the first supplier, changing 'surname' attribute's syntax to a Directory String subset
        3. Trigger replication and wait for completion.
        4. Verify schema replication was rejected
        5. Restart both suppliers.
        6. Verify schema replication was rejected
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
    surname_attr_type = surname_attr_type.replace('1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.26')
    schema_m1.add('attributetypes', surname_attr_type)

    # Trigger replication and wait for completion
    trigger_update(topology, test_user.rdn, 1)
    replication_manager.wait_for_replication(m1, m2)

    validate_schema_changes(topology, 'surname', 'caseIgnoreIA5', m1_not_present=True, m2_not_present=True)

    # Restart servers to ensure persistence of changes
    m1.restart()
    m2.restart()

    validate_schema_changes(topology, 'surname', 'caseIgnoreIA5', m1_not_present=True, m2_not_present=True)


def test_custom_standard_schema_stays_local(topology):
    """Test if custom standard schema defined in 98test.ldif on one supplier
    is not replicated to the other supplier.

    :id: b166334d-ed71-4b96-84e1-c376aedc355c
    :setup: Two supplier replication topology
    :steps:
        1. Create a test user on the first supplier.
        2. Create a custom standard schema in 98test.ldif on the first supplier.
        3. Trigger replication and wait for completion.
        4. Verify it was not replicated to the second supplier.
        5. Restart both suppliers.
        6. Verify it was not replicated to the second supplier.
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
    file_path = os.path.join(m1.schemadir, "98test.ldif")
    m1.stop()
    with open(file_path, 'a') as f:
        f.write("""dn: cn=schema
attributeTypes: ( 2.5.4.4 NAME ( 'sn' 'surName' ) SUP name \
EQUALITY caseIgnoreIA5Match SUBSTR caseIgnoreIA5SubstringsMatch \
SYNTAX 1.3.6.1.4.1.1466.115.121.1.26 X-ORIGIN 'user defined' )""")

    # Adjust the permissions to 600:
    os.chmod(file_path, 0o600)
    dirsrv_uid = pwd.getpwnam('dirsrv').pw_uid
    dirsrv_gid = grp.getgrnam('dirsrv').gr_gid
    os.chown(file_path, dirsrv_uid, dirsrv_gid)

    m1.start()

    # Retrieve and modify the schema on m1
    schema_m1 = Schema(m1)
    schema_m2 = Schema(m2)

    # Trigger replication and wait for completion
    trigger_update(topology, test_user.rdn, 1)
    replication_manager.wait_for_replication(m1, m2)

    validate_schema_changes(topology, 'surname', 'caseIgnoreIA5', m2_not_present=True)

    # Restart servers to ensure persistence of changes
    m1.restart()
    m2.restart()

    validate_schema_changes(topology, 'surname', 'caseIgnoreIA5', m2_not_present=True)


def test_custom_schema_persistent_after_restart_locally(topology_st):
    """Test if custom schema modifications persist after server restarts on standalone instance.

    :id: e662d3e1-9d0f-45fe-b9f9-912f847602bc
    :setup: Two supplier replication topology
    :steps:
        1. Create a test user on the first supplier.
        2. Retrieve and modify the schema on the first supplier, changing 'surname' attribute's matching rule.
        3. Restart both suppliers.
        4. Verify schema is persisted after restart.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    st = topology_st.standalone

    # Create a test user on m1
    users_st = UserAccounts(st, DEFAULT_SUFFIX)
    test_user = users_st.create_test_user()

    # Retrieve and modify the schema on st
    schema_st = Schema(st)
    surname_attr_type = schema_st.query_attributetype('surname')[0]
    assert 'caseIgnoreMatch' in schema_st.query_attributetype('surname')[0], "Standard schema not found in st."

    surname_attr_type = surname_attr_type.replace('caseIgnoreMatch', 'caseIgnoreIA5Match')
    surname_attr_type = surname_attr_type.replace('caseIgnoreSubstringsMatch', 'caseIgnoreIA5SubstringsMatch')
    surname_attr_type = surname_attr_type.replace('1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.26')
    schema_st.add('attributetypes', surname_attr_type)

    test_user.replace('telephonenumber', "1")

    # Verify schema
    assert 'caseIgnoreIA5Match' in schema_st.query_attributetype('surname')[0], "Custom schema not found in st."

    # Restart servers to ensure persistence of changes
    st.restart()

    # Verify schema persistence post-restart
    assert 'caseIgnoreIA5Match' in schema_st.query_attributetype('surname')[0], "Custom schema not persisted in st."

    # Test that the custom schema is present in schema 99user.ldif in server's schema dir
    schema_filename = (st.schemadir + "/99user.ldif")
    with open(schema_filename, 'r') as f:
        assert 'caseIgnoreIA5SubstringsMatch' in f.read(), "Custom schema not found in 99user.ldif."
