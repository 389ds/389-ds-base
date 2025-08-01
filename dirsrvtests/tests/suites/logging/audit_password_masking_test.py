# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import re
import time
import ldap
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PW_DM
from lib389.topologies import topology_m2 as topo
from lib389.idm.user import UserAccounts
from lib389.plugins import ChainingBackendPlugin
from lib389.chaining import ChainingLinks
from lib389.agreement import Agreements
from lib389.replica import ReplicationManager, Replicas
from lib389.idm.directorymanager import DirectoryManager

log = logging.getLogger(__name__)

MASKED_PASSWORD = "**********************"
TEST_PASSWORD = "MySecret123"
TEST_PASSWORD_2 = "NewPassword789"
TEST_PASSWORD_3 = "NewPassword101"


def setup_audit_logging(inst, log_format='default', display_attrs=None):
    """Configure audit logging settings"""
    inst.config.replace('nsslapd-auditlog-logging-enabled', 'on')

    if display_attrs is not None:
        inst.config.replace('nsslapd-auditlog-display-attrs', display_attrs)

    inst.deleteAuditLogs()


def check_password_masked(inst, log_format, expected_password, actual_password):
    """Helper function to check password masking in audit logs"""

    inst.restart() # Flush the logs

    # List of all password/credential attributes that should be masked
    password_attributes = [
        'userPassword',
        'nsslapd-rootpw',
        'nsmultiplexorcredentials',
        'nsDS5ReplicaCredentials',
        'nsDS5ReplicaBootstrapCredentials'
    ]

    # Get password schemes to check for hash leakage
    user_password_scheme = inst.config.get_attr_val_utf8('passwordStorageScheme')
    root_password_scheme = inst.config.get_attr_val_utf8('nsslapd-rootpwstoragescheme')

    # Check LDIF format logs
    found_masked = False
    found_actual = False
    found_hashed = False

    # Check each password attribute for masked password
    for attr in password_attributes:
        if inst.ds_audit_log.match(f"{attr}: {re.escape(expected_password)}"):
            found_masked = True
        if inst.ds_audit_log.match(f"{attr}: {actual_password}"):
            found_actual = True

    # Check for hashed passwords in LDIF format
    if user_password_scheme:
        if inst.ds_audit_log.match(f"userPassword: {{{user_password_scheme}}}"):
            found_hashed = True
    if root_password_scheme:
        if inst.ds_audit_log.match(f"nsslapd-rootpw: {{{root_password_scheme}}}"):
            found_hashed = True

    # Delete audit logs to avoid interference with other tests
    # We need to reset the root password to default as deleteAuditLogs()
    # opens a new connection with the default password
    dm = DirectoryManager(inst)
    dm.change_password(PW_DM)
    inst.deleteAuditLogs()

    return found_masked, found_actual, found_hashed


@pytest.mark.parametrize("log_format,display_attrs", [
    ("default", None),
    pytest.param("default", "*", marks=pytest.mark.xfail(reason="DS6886")),
    ("default", "userPassword"),
])
def test_password_masking_add_operation(topo, log_format, display_attrs):
    """Test password masking in ADD operations

    :id: 4358bd75-bcc7-401c-b492-d3209b10412d
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Configure audit logging format
        2. Add user with password
        3. Check that password is masked in audit log
        4. Verify actual password does not appear in log
    :expectedresults:
        1. Success
        2. Success
        3. Password should be masked with asterisks
        4. Actual password should not be found in log
    """
    inst = topo.ms['supplier1']
    setup_audit_logging(inst, log_format, display_attrs)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = None

    try:
        user = users.create(properties={
            'uid': 'test_add_pwd_mask',
            'cn': 'Test Add User',
            'sn': 'User',
            'uidNumber': '1000',
            'gidNumber': '1000',
            'homeDirectory': '/home/test_add',
            'userPassword': TEST_PASSWORD
        })

        found_masked, found_actual, found_hashed = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD)

        assert found_masked, f"Masked password not found in {log_format} ADD operation"
        assert not found_actual, f"Actual password found in {log_format} ADD log (should be masked)"
        assert not found_hashed, f"Hashed password found in {log_format} ADD log (should be masked)"

    finally:
        if user is not None:
            try:
                user.delete()
            except:
                pass


@pytest.mark.parametrize("log_format,display_attrs", [
    ("default", None),
    pytest.param("default", "*", marks=pytest.mark.xfail(reason="DS6886")),
    ("default", "userPassword"),
])
def test_password_masking_modify_operation(topo, log_format, display_attrs):
    """Test password masking in MODIFY operations

    :id: e6963aa9-7609-419c-aae2-1d517aa434bd
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Configure audit logging format
        2. Add user without password
        3. Add password via MODIFY operation
        4. Check that password is masked in audit log
        5. Modify password to new value
        6. Check that new password is also masked
        7. Verify actual passwords do not appear in log
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Password should be masked with asterisks
        5. Success
        6. New password should be masked with asterisks
        7. No actual password values should be found in log
    """
    inst = topo.ms['supplier1']
    setup_audit_logging(inst, log_format, display_attrs)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = None

    try:
        user = users.create(properties={
            'uid': 'test_modify_pwd_mask',
            'cn': 'Test Modify User',
            'sn': 'User',
            'uidNumber': '2000',
            'gidNumber': '2000',
            'homeDirectory': '/home/test_modify'
        })

        user.replace('userPassword', TEST_PASSWORD)

        found_masked, found_actual, found_hashed = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD)
        assert found_masked, f"Masked password not found in {log_format} MODIFY operation (first password)"
        assert not found_actual, f"Actual password found in {log_format} MODIFY log (should be masked)"
        assert not found_hashed, f"Hashed password found in {log_format} MODIFY log (should be masked)"

        user.replace('userPassword', TEST_PASSWORD_2)

        found_masked_2, found_actual_2, found_hashed_2 = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD_2)
        assert found_masked_2, f"Masked password not found in {log_format} MODIFY operation (second password)"
        assert not found_actual_2, f"Second actual password found in {log_format} MODIFY log (should be masked)"
        assert not found_hashed_2, f"Second hashed password found in {log_format} MODIFY log (should be masked)"

    finally:
        if user is not None:
            try:
                user.delete()
            except:
                pass


@pytest.mark.parametrize("log_format,display_attrs", [
    ("default", None),
    pytest.param("default", "*", marks=pytest.mark.xfail(reason="DS6886")),
    ("default", "nsslapd-rootpw"),
])
def test_password_masking_rootpw_modify_operation(topo, log_format, display_attrs):
    """Test password masking for nsslapd-rootpw MODIFY operations

    :id: ec8c9fd4-56ba-4663-ab65-58efb3b445e4
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Configure audit logging format
        2. Modify nsslapd-rootpw in configuration
        3. Check that root password is masked in audit log
        4. Modify root password to new value
        5. Check that new root password is also masked
        6. Verify actual root passwords do not appear in log
    :expectedresults:
        1. Success
        2. Success
        3. Root password should be masked with asterisks
        4. Success
        5. New root password should be masked with asterisks
        6. No actual root password values should be found in log
    """
    inst = topo.ms['supplier1']
    setup_audit_logging(inst, log_format, display_attrs)
    dm = DirectoryManager(inst)

    try:
        dm.change_password(TEST_PASSWORD)
        dm.rebind(TEST_PASSWORD)
        dm.change_password(PW_DM)

        found_masked, found_actual, found_hashed = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD)
        assert found_masked, f"Masked root password not found in {log_format} MODIFY operation (first root password)"
        assert not found_actual, f"Actual root password found in {log_format} MODIFY log (should be masked)"
        assert not found_hashed, f"Hashed root password found in {log_format} MODIFY log (should be masked)"

        dm.change_password(TEST_PASSWORD_2)
        dm.rebind(TEST_PASSWORD_2)
        dm.change_password(PW_DM)

        found_masked_2, found_actual_2, found_hashed_2 = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD_2)
        assert found_masked_2, f"Masked root password not found in {log_format} MODIFY operation (second root password)"
        assert not found_actual_2, f"Second actual root password found in {log_format} MODIFY log (should be masked)"
        assert not found_hashed_2, f"Second hashed root password found in {log_format} MODIFY log (should be masked)"

    finally:
        dm.change_password(PW_DM)
        dm.rebind(PW_DM)


@pytest.mark.parametrize("log_format,display_attrs", [
    ("default", None),
    pytest.param("default", "*", marks=pytest.mark.xfail(reason="DS6886")),
    ("default", "nsmultiplexorcredentials"),
])
def test_password_masking_multiplexor_credentials(topo, log_format, display_attrs):
    """Test password masking for nsmultiplexorcredentials in chaining/multiplexor configurations

    :id: 161a9498-b248-4926-90be-a696a36ed36e
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Configure audit logging format
        2. Create a chaining backend configuration entry with nsmultiplexorcredentials
        3. Check that multiplexor credentials are masked in audit log
        4. Modify the credentials
        5. Check that updated credentials are also masked
        6. Verify actual credentials do not appear in log
    :expectedresults:
        1. Success
        2. Success
        3. Multiplexor credentials should be masked with asterisks
        4. Success
        5. Updated credentials should be masked with asterisks
        6. No actual credential values should be found in log
    """
    inst = topo.ms['supplier1']
    setup_audit_logging(inst, log_format, display_attrs)

    # Enable chaining plugin and create chaining link
    chain_plugin = ChainingBackendPlugin(inst)
    chain_plugin.enable()

    chains = ChainingLinks(inst)
    chain = None

    try:
        # Create chaining link with multiplexor credentials
        chain = chains.create(properties={
            'cn': 'testchain',
            'nsfarmserverurl': 'ldap://localhost:389/',
            'nsslapd-suffix': 'dc=example,dc=com',
            'nsmultiplexorbinddn': 'cn=manager',
            'nsmultiplexorcredentials': TEST_PASSWORD,
            'nsCheckLocalACI': 'on',
            'nsConnectionLife': '30',
        })

        found_masked, found_actual, found_hashed = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD)
        assert found_masked, f"Masked multiplexor credentials not found in {log_format} ADD operation"
        assert not found_actual, f"Actual multiplexor credentials found in {log_format} ADD log (should be masked)"
        assert not found_hashed, f"Hashed multiplexor credentials found in {log_format} ADD log (should be masked)"

        # Modify the credentials
        chain.replace('nsmultiplexorcredentials', TEST_PASSWORD_2)

        found_masked_2, found_actual_2, found_hashed_2 = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD_2)
        assert found_masked_2, f"Masked multiplexor credentials not found in {log_format} MODIFY operation"
        assert not found_actual_2, f"Actual multiplexor credentials found in {log_format} MODIFY log (should be masked)"
        assert not found_hashed_2, f"Hashed multiplexor credentials found in {log_format} MODIFY log (should be masked)"

    finally:
        chain_plugin.disable()
        if chain is not None:
            inst.delete_branch_s(chain.dn, ldap.SCOPE_ONELEVEL)
            chain.delete()


@pytest.mark.parametrize("log_format,display_attrs", [
    ("default", None),
    pytest.param("default", "*", marks=pytest.mark.xfail(reason="DS6886")),
    ("default", "nsDS5ReplicaCredentials"),
])
def test_password_masking_replica_credentials(topo, log_format, display_attrs):
    """Test password masking for nsDS5ReplicaCredentials in replication agreements

    :id: 7bf9e612-1b7c-49af-9fc0-de4c7df84b2a
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Configure audit logging format
        2. Create a replication agreement entry with nsDS5ReplicaCredentials
        3. Check that replica credentials are masked in audit log
        4. Modify the credentials
        5. Check that updated credentials are also masked
        6. Verify actual credentials do not appear in log
    :expectedresults:
        1. Success
        2. Success
        3. Replica credentials should be masked with asterisks
        4. Success
        5. Updated credentials should be masked with asterisks
        6. No actual credential values should be found in log
    """
    inst = topo.ms['supplier2']
    setup_audit_logging(inst, log_format, display_attrs)
    agmt = None

    try:
        replicas = Replicas(inst)
        replica = replicas.get(DEFAULT_SUFFIX)
        agmts = replica.get_agreements()
        agmt = agmts.create(properties={
            'cn': 'testagmt',
            'nsDS5ReplicaHost': 'localhost',
            'nsDS5ReplicaPort': '389',
            'nsDS5ReplicaBindDN': 'cn=replication manager,cn=config',
            'nsDS5ReplicaCredentials': TEST_PASSWORD,
            'nsDS5ReplicaRoot': DEFAULT_SUFFIX
        })

        found_masked, found_actual, found_hashed = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD)
        assert found_masked, f"Masked replica credentials not found in {log_format} ADD operation"
        assert not found_actual, f"Actual replica credentials found in {log_format} ADD log (should be masked)"
        assert not found_hashed, f"Hashed replica credentials found in {log_format} ADD log (should be masked)"

        # Modify the credentials
        agmt.replace('nsDS5ReplicaCredentials', TEST_PASSWORD_2)

        found_masked_2, found_actual_2, found_hashed_2 = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD_2)
        assert found_masked_2, f"Masked replica credentials not found in {log_format} MODIFY operation"
        assert not found_actual_2, f"Actual replica credentials found in {log_format} MODIFY log (should be masked)"
        assert not found_hashed_2, f"Hashed replica credentials found in {log_format} MODIFY log (should be masked)"

    finally:
        if agmt is not None:
            agmt.delete()


@pytest.mark.parametrize("log_format,display_attrs", [
    ("default", None),
    pytest.param("default", "*", marks=pytest.mark.xfail(reason="DS6886")),
    ("default", "nsDS5ReplicaBootstrapCredentials"),
])
def test_password_masking_bootstrap_credentials(topo, log_format, display_attrs):
    """Test password masking for nsDS5ReplicaCredentials and nsDS5ReplicaBootstrapCredentials in replication agreements

    :id: 248bd418-ffa4-4733-963d-2314c60b7c5b
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Configure audit logging format
        2. Create a replication agreement entry with both nsDS5ReplicaCredentials and nsDS5ReplicaBootstrapCredentials
        3. Check that both credentials are masked in audit log
        4. Modify both credentials
        5. Check that both updated credentials are also masked
        6. Verify actual credentials do not appear in log
    :expectedresults:
        1. Success
        2. Success
        3. Both credentials should be masked with asterisks
        4. Success
        5. Both updated credentials should be masked with asterisks
        6. No actual credential values should be found in log
    """
    inst = topo.ms['supplier2']
    setup_audit_logging(inst, log_format, display_attrs)
    agmt = None

    try:
        replicas = Replicas(inst)
        replica = replicas.get(DEFAULT_SUFFIX)
        agmts = replica.get_agreements()
        agmt = agmts.create(properties={
            'cn': 'testbootstrapagmt',
            'nsDS5ReplicaHost': 'localhost',
            'nsDS5ReplicaPort': '389',
            'nsDS5ReplicaBindDN': 'cn=replication manager,cn=config',
            'nsDS5ReplicaCredentials': TEST_PASSWORD,
            'nsDS5replicabootstrapbinddn': 'cn=bootstrap manager,cn=config',
            'nsDS5ReplicaBootstrapCredentials': TEST_PASSWORD_2,
            'nsDS5ReplicaRoot': DEFAULT_SUFFIX
        })

        found_masked_bootstrap, found_actual_bootstrap, found_hashed_bootstrap = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD_2)
        assert found_masked_bootstrap, f"Masked bootstrap credentials not found in {log_format} ADD operation"
        assert not found_actual_bootstrap, f"Actual bootstrap credentials found in {log_format} ADD log (should be masked)"
        assert not found_hashed_bootstrap, f"Hashed bootstrap credentials found in {log_format} ADD log (should be masked)"

        agmt.replace('nsDS5ReplicaBootstrapCredentials', TEST_PASSWORD_3)

        found_masked_bootstrap_2, found_actual_bootstrap_2, found_hashed_bootstrap_2 = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD_3)
        assert found_masked_bootstrap_2, f"Masked bootstrap credentials not found in {log_format} MODIFY operation"
        assert not found_actual_bootstrap_2, f"Actual bootstrap credentials found in {log_format} MODIFY log (should be masked)"
        assert not found_hashed_bootstrap_2, f"Hashed bootstrap credentials found in {log_format} MODIFY log (should be masked)"

    finally:
        if agmt is not None:
            agmt.delete()



if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
