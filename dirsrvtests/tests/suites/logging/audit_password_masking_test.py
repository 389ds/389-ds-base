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
from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts
from lib389.dirsrv_log import DirsrvAuditJSONLog

log = logging.getLogger(__name__)

MASKED_PASSWORD = "**********************"
TEST_PASSWORD = "MySecret123"
TEST_PASSWORD_2 = "NewPassword789"


def setup_audit_logging(inst, log_format='default', display_attrs=None):
    """Configure audit logging settings"""
    inst.config.replace('nsslapd-auditlog-logbuffering', 'off')
    inst.config.replace('nsslapd-auditlog-logging-enabled', 'on')
    inst.config.replace('nsslapd-auditlog-log-format', log_format)

    if display_attrs is not None:
        inst.config.replace('nsslapd-auditlog-display-attrs', display_attrs)

    inst.deleteAuditLogs()


def check_password_masked(inst, log_format, expected_password, actual_password):
    """Helper function to check password masking in audit logs"""

    time.sleep(1)  # Allow log to flush

    # Get password schemes to check for hash leakage
    user_password_scheme = inst.config.get_attr_val_utf8('passwordStorageScheme')
    root_password_scheme = inst.config.get_attr_val_utf8('nsslapd-rootpwstoragescheme')

    if log_format == 'json':
        # Check JSON format logs
        audit_log = DirsrvAuditJSONLog(inst)
        log_lines = audit_log.readlines()

        found_masked = False
        found_actual = False
        found_hashed = False

        for line in log_lines:
            if 'userPassword' in line or 'nsslapd-rootpw' in line:
                if expected_password in line:
                    found_masked = True
                if actual_password in line:
                    found_actual = True
                # Check for password scheme indicators (hashed passwords)
                if user_password_scheme and f'{{{user_password_scheme}}}' in line:
                    found_hashed = True
                if root_password_scheme and f'{{{root_password_scheme}}}' in line:
                    found_hashed = True

        return found_masked, found_actual, found_hashed

    else:
        # Check LDIF format logs
        found_masked_user = inst.ds_audit_log.match(f"userPassword: {re.escape(expected_password)}")
        found_masked_root = inst.ds_audit_log.match(f"nsslapd-rootpw: {re.escape(expected_password)}")
        found_masked = found_masked_user or found_masked_root

        found_actual_user = inst.ds_audit_log.match(f"userPassword: {actual_password}")
        found_actual_root = inst.ds_audit_log.match(f"nsslapd-rootpw: {actual_password}")
        found_actual = found_actual_user or found_actual_root

        # Check for hashed passwords in LDIF format
        found_hashed = False
        if user_password_scheme:
            found_hashed = inst.ds_audit_log.match(f"userPassword: {{{user_password_scheme}}}")
        if not found_hashed and root_password_scheme:
            found_hashed = inst.ds_audit_log.match(f"nsslapd-rootpw: {{{root_password_scheme}}}")

        return bool(found_masked), bool(found_actual), bool(found_hashed)


@pytest.mark.parametrize("log_format,display_attrs", [
    ("default", None),
    ("default", "*"),
    ("default", "userPassword"),
    ("json", None),
    ("json", "*"),
    ("json", "userPassword")
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
    inst = topo.standalone
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
    ("default", "*"),
    ("default", "userPassword"),
    ("json", None),
    ("json", "*"),
    ("json", "userPassword")
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
    inst = topo.standalone
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
    ("default", "*"),
    ("default", "nsslapd-rootpw"),
    ("json", None),
    ("json", "*"),
    ("json", "nsslapd-rootpw")
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
    inst = topo.standalone
    setup_audit_logging(inst, log_format, display_attrs)

    try:
        inst.config.replace('nsslapd-rootpw', TEST_PASSWORD)

        found_masked, found_actual, found_hashed = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD)
        assert found_masked, f"Masked root password not found in {log_format} MODIFY operation (first root password)"
        assert not found_actual, f"Actual root password found in {log_format} MODIFY log (should be masked)"
        assert not found_hashed, f"Hashed root password found in {log_format} MODIFY log (should be masked)"

        inst.config.replace('nsslapd-rootpw', TEST_PASSWORD_2)

        found_masked_2, found_actual_2, found_hashed_2 = check_password_masked(inst, log_format, MASKED_PASSWORD, TEST_PASSWORD_2)
        assert found_masked_2, f"Masked root password not found in {log_format} MODIFY operation (second root password)"
        assert not found_actual_2, f"Second actual root password found in {log_format} MODIFY log (should be masked)"
        assert not found_hashed_2, f"Second hashed root password found in {log_format} MODIFY log (should be masked)"

    finally:
        inst.config.replace('nsslapd-rootpw', PW_DM)


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])