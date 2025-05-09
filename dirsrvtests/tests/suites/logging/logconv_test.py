# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import logging
import os
import pytest
import re
import shutil
import subprocess
import time

from lib389._constants import *
from lib389.idm.account import Anonymous, Accounts
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_st
from lib389.utils import ensure_str
from ldap.controls.vlv import VLVRequestControl
from ldap.controls.sss import SSSRequestControl
from ldap.controls import SimplePagedResultsControl
from ldap.controls.psearch import PersistentSearchControl
from lib389.backend import DatabaseConfig
from lib389.dbgen import dbgen_users
from lib389._mapped_object import DSLdapObjects

log = logging.getLogger(__name__)

# Path to the logconv script (Change to logconv.pl for comparison).
LOGCONV_SCRIPT = "logconv.py"
 # If True, each test validates logconv output individually; otherwise, final summary test runs validation.
VALIDATE_EACH_TEST = True
# Expected summary values used for validation when VALIDATE_EACH_TEST is False.
VALIDATE_ALL_SUMMARY = {}

@pytest.fixture(scope="module", autouse=True)
def get_access_log_path(topology_st):
    """ Truncate logs before running tests to remove startup noise. """
    inst = topology_st.standalone
    path = inst.config.get_attr_val_utf8('nsslapd-accesslog')
    inst.config.set('nsslapd-accesslog-logbuffering', 'off')
    if not path:
        pytest.fail("Access log path not found.")
    with open(path, "w") as f:
        log.info("Truncating access logs after startup.")
        f.truncate(0)
    yield path

# Utilities
def truncate_access_log(path):
    """ Truncate logs if each test validation is enabled. """
    if not path:
        raise ValueError("Access log path not found.")
    if VALIDATE_EACH_TEST:
        with open(path, "w") as f:
            f.truncate(0)
        return path
    else:
        return path

def get_logconv_path():
    """ Get the location of logconv script."""
    return shutil.which(LOGCONV_SCRIPT) or None

def run_logconv(access_log_path: str, debug_output=False):
    """ Run logconv script, if debug is enabled the output is written to file."""
    LOGCONV_OUTPUT = 'logconv-debug.log'
    logconv_path = get_logconv_path()

    if not logconv_path:
        raise FileNotFoundError(f"{LOGCONV_SCRIPT} not found.")

    result = subprocess.run(
        [logconv_path, access_log_path],
        capture_output=True,
        text=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"{LOGCONV_SCRIPT} failed: {result.stderr}")

    summary = result.stdout

    if debug_output:
        with open(LOGCONV_OUTPUT, 'w') as f:
            f.write(summary)
        os.chmod(LOGCONV_OUTPUT, 0o644)

    return summary

def parse_summary(output: str):
    """ Key patterns from logconv default output. """
    patterns = {
        # Server stats
        "restarts": r"^Restarts:\s+(\d+)",

        # Connections
        "concurrent_conns": r"^Peak Concurrent connections:\s+(\d+)",
        "total_connections": r"^Total connections:\s+(\d+)",
        "ldap_conns": r"- LDAP connections:\s+(\d+)",
        "ldapi_conns": r"- LDAPI connections:\s+(\d+)",
        "ldaps_conns": r"- LDAPS connections:\s+(\d+)",

        # Operations
        "operations": r"^Total Operations:\s+(\d+)",
        "results": r"^Total Results:\s+(\d+)",
        "searches": r"^Searches:\s+(\d+)",
        "mods": r"^Modifications:\s+(\d+)",
        "adds": r"^Adds:\s+(\d+)",
        "deletes": r"^Deletes:\s+(\d+)",
        "modrdns": r"^Mod RDNs:\s+(\d+)",
        "compares": r"Compares:\s+(\d+)",
        "binds": r"^Binds:\s+(\d+)",
        "unbinds": r"^Unbinds:\s+(\d+)",
        "autobinds": r"AUTOBINDs\(LDAPI\):\s+(\d+)",
        "sasl_binds": r"SASL Binds:\s+(\d+)",
        "anon_binds": r"Anonymous Binds:\s+(\d+)",
        "ssl_client_binds": r"SSL Client Binds\s+(\d+)",
        "ssl_client_bind_failed": r"Failed SSL Client Binds:\s+(\d+)",

        # Advanced operations
        "persistent_searches": r"^Persistent Searches:\s+(\d+)",
        "extended_ops": r"^Extended Operations:\s+(\d+)",
        "internal_ops": r"^Internal Operations:\s+(\d+)",
        "abandoned": r"^Abandoned Requests:\s+(\d+)",
        "proxied_auth_ops": r"^Proxied Auth Operations:\s+(\d+)",
        "vlv_operations": r"^VLV Operations:\s+(\d+)",
        "vlv_unindexed_searches": r"VLV Unindexed Searches:\s+(\d+)",
        "sort_operations": r"^SORT Operations:\s+(\d+)",
        "paged_searches": r"^Paged Searches:\s+(\d+)",

        # Errors and filter issues
        "invalid_attribute_filters": r"^Invalid Attribute Filters:\s+(\d+)",
        "broken_pipes": r"^Broken Pipes:\s+(\d+)",
        "connection_reset": r"^Connection Reset By Peer:\s+(\d+)",
        "resource_unavailable": r"^Resource Unavailable:\s+(\d+)",
        "max_ber_exceeded": r"^Max BER Size Exceeded:\s+(\d+)",

        # File descriptors
        "fds_taken": r"^FDs Taken:\s+(\d+)",
        "fds_returned": r"^FDs Returned:\s+(\d+)",
        "highest_fd_taken": r"^Highest FD Taken:\s+(\d+)",

        # Special cases
        "entire_search_base_queries": r"Entire Search Base Queries:\s+(\d+)",
        "unindexed_searches": r"^Unindexed Searches:\s+(\d+)",
        "unindexed_components": r"^Unindexed Components:\s+(\d+)",
        "multi_factor_auth": r"^Multi-factor Authentications:\s+(\d+)",
        "smart_referrals": r"^Smart Referrals Received:\s+(\d+)",
    }

    summary = {}
    for key, pattern in patterns.items():
        match = re.search(pattern, output, re.IGNORECASE | re.MULTILINE)
        if match:
            value = match.group(1)
            try:
                summary[key] = float(value) if '.' in value else int(value)
            except (ValueError) as e:
                log.info(f"Failed to parse value for {key}")

    return summary

def assert_summary(expected: dict, output: str, test_name=""):
    """ Asserts that the logconv output matches expected values.

        If a key is not present in expected but is in summary
        we assume its a 0.
    """
    IGNORE_KEYS = {"highest_fd_taken", "concurrent_conns"}
    errors = []

    summary = parse_summary(output)
    for key, existing_val in summary.items():
        if key in IGNORE_KEYS:
            continue
        actual_val = expected.get(key, 0)
        if actual_val != existing_val:
            errors.append(f"{test_name} - {key}: expected {actual_val}, got {existing_val}")
    if errors:
        error_report = "\n".join(errors)
        raise AssertionError(f"\nSummary mismatches:\n{error_report}")

def run_and_validate(expected: dict, access_log_path: str, test_name=""):
    """ Run logconv and validate the output against expected dict. """
    time.sleep(2)

    if VALIDATE_EACH_TEST:
        output = run_logconv(access_log_path, debug_output=True)
        assert_summary(expected, output, test_name)
    else:
        for k, v in expected.items():
            VALIDATE_ALL_SUMMARY[k] = VALIDATE_ALL_SUMMARY.get(k, 0) + v

# Tests
def test_add(topology_st, get_access_log_path):
    """Test that user add operations are correctly parsed by logconv.

    :id: c94809d5-e3b8-4692-a0e1-33932c766944
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Add 3 LDAP users.
        3. Run logconv on the access log.
        4. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. Users are added without errors.
        3. logconv returns correct updated stats.
        4. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    for i in range(3):
        users.create(properties={
            'uid': f'testuser{i}',
            'sn': f'testuser{i}',
            'cn': f'testuser{i}',
            'uidNumber': str(i + 100),
            'gidNumber': str(i + 100),
            'homeDirectory': f'/home/testuser{i}',
            'mail': f'testuser{i}@example.com',
            'userpassword': 'password',
        })

    expected = {
        "adds": 3,
        "searches": 6,
        "operations": 9,
        "results": 9
    }
    run_and_validate(expected, access_log, test_name="test_add")

def test_modify(topology_st, get_access_log_path):
    """Test that modify operations are correctly parsed by logconv.

    :id: 37f27ef7-29ed-4968-b048-c5ecff6d50a1
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Create a user.
        3. Modify the user's password.
        4. Run logconv on the access log.
        5. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. User is created successfully.
        3. Modify operation is applied without error.
        4. logconv returns correct updated stats.
        5. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'user_mod',
        'sn': 'test',
        'cn': 'test',
        'uidNumber': '2000',
        'gidNumber': '2000',
        'homeDirectory': '/home/user_mod',
        'mail': 'user_mod@example.com',
        'userpassword': 'initialpass',
    })
    user.set("userPassword", "newpass")

    expected = {
        "adds": 1,
        "mods": 1,
        "searches": 2,
        "operations": 4,
        "results": 4
    }
    run_and_validate(expected, access_log, test_name="test_modify")

def test_delete(topology_st, get_access_log_path):
    """Test that delete operations are correctly parsed by logconv.

    :id: ad124b47-7f10-4188-af55-bf20a0d6b8ab
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Create a user.
        3. Delete the user.
        4. Run logconv on the access log.
        5. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. User is created successfully.
        3. User is deleted successfully.
        4. logconv returns correct updated stats.
        5. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'user_del',
        'sn': 'test',
        'cn': 'test',
        'uidNumber': '3000',
        'gidNumber': '3000',
        'homeDirectory': '/home/user_del',
        'mail': 'user_del@example.com',
        'userpassword': 'password',
    })
    user.delete()

    expected = {
        "adds": 1,
        "deletes": 1,
        "searches": 2,
        "operations": 4,
        "results": 4
    }
    run_and_validate(expected, access_log, test_name="test_delete")

def test_modrdn(topology_st, get_access_log_path):
    """Test that modrdn operations are correctly parsed by logconv.

    :id: c6fa0fb5-7148-4043-8370-c698445fd964
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Create a user.
        3. Rename the user's DN using modrdn.
        4. Run logconv on the access log.
        5. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. User is created successfully.
        3. User is renamed successfully.
        4. logconv returns correct updated stats.
        5. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'user_modrdn',
        'sn': 'test',
        'cn': 'test',
        'uidNumber': '4000',
        'gidNumber': '4000',
        'homeDirectory': '/home/user_modrdn',
        'mail': 'user_modrdn@example.com',
        'userpassword': 'password',
    })
    user.rename('uid=user_modrdn_renamed')

    expected = {
        "adds": 1,
        "modrdns": 1,
        "searches": 3,
        "operations": 5,
        "results": 5
    }
    run_and_validate(expected, access_log, test_name="test_modrdn")

def test_compare(topology_st, get_access_log_path):
    """Test that compare operations are correctly parsed by logconv.

    :id: eb035506-47a8-4693-b1d8-8167f17b350b
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Create a user.
        3. Perform a compare operation against the user.
        4. Run logconv on the access log.
        5. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. User is created successfully.
        3. Compare operation executes without error.
        4. logconv returns correct updated stats.
        5. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'user_compare',
        'sn': 'compare',
        'cn': 'compare',
        'uidNumber': '5000',
        'gidNumber': '5000',
        'homeDirectory': '/home/user_compare',
        'mail': 'user_compare@example.com',
        'userpassword': 'password',
    })
    inst.compare_ext_s(user.dn, 'cn', b'compare')

    expected = {
        "adds": 1,
        "compares": 1,
        "searches": 2,
        "operations": 4,
        "results": 4
    }
    run_and_validate(expected, access_log, test_name="test_compare")

def test_internal_ops(topology_st, get_access_log_path):
    """Test that internal operations (plugin-generated) are correctly parsed by logconv.

    :id:  71f93a9f-3f9d-450e-ae98-79fabe5f21f4
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Enable plugin and internal operation logging.
        3. Trigger an internal operation via plugin activity.
        4. Disable plugin and internal operation logging.
        5. Run logconv on the access log.
        6. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. Logging level enabled.
        3. Internal operation is logged as expected.
        4. Log level set back to default.
        5. logconv returns correct updated stats.
        6. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    default_log_level = inst.config.get_attr_val_utf8(LOG_ACCESS_LEVEL)
    access_log_level = '4'
    inst.config.set(LOG_ACCESS_LEVEL, access_log_level)
    inst.config.set('nsslapd-plugin-logging', 'on')
    inst.config.set(LOG_ACCESS_LEVEL, default_log_level)
    inst.config.set('nsslapd-plugin-logging', 'off')

    expected = {
        "internal_ops": 2,
        "mods": 2,
        "searches": 3,
        "operations": 5,
        "results": 5,
    }
    run_and_validate(expected, access_log, test_name="test_internal_ops_2")

def test_search(topology_st, get_access_log_path):
    """Test that a standard LDAP search is correctly parsed by logconv.

    :id: 781b2bd3-9cbf-4263-a683-8f1de01f1d58
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Perform a search with a simple filter.
        3. Run logconv on the access log.
        4. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. Search executes without error.
        3. logconv returns correct updated stats.
        4. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    try:
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=findmeifucan)')
    except ldap.LDAPError as e:
            log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
            raise e

    expected = {
        "searches": 1,
        "operations": 1,
        "results": 1
    }
    run_and_validate(expected, access_log, test_name="test_search")

def test_base_search(topology_st, get_access_log_path):
    """Test that a base-level search is correctly parsed by logconv.

    :id: 24ee92d4-5510-4c27-b8fb-555da222bd88
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Perform a base search requesting only the DN.
        3. Run logconv on the access log.
        4. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. Base search completes successfully.
        3. logconv returns correct updated stats.
        4. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    try:
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'])
    except ldap.LDAPError as e:
            log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
            raise e

    expected = {
        "searches": 1,
        "operations": 1,
        "entire_search_base_queries": 1,
        "results": 1
    }
    run_and_validate(expected, access_log, test_name="test_search")

def test_paged_search(topology_st, get_access_log_path):
    """Test that a paged result search is correctly parsed by logconv.

    :id: a51ced03-8220-408a-9e51-c8df4b536ac6
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Perform a base search.
        3. Perform a paged search using the SimplePagedResultsControl.
        4. Run logconv on the access log.
        5. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. Base search completes successfully.
        3. Paged search completes without errors.
        4. logconv returns correct updated stats.
        5. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    req_ctrl = SimplePagedResultsControl(True, size=3, cookie='')
    try:
        inst.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "sn=user_*", ['cn'], serverctrls=[req_ctrl])
    except ldap.LDAPError as e:
            log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
            raise e

    expected = {
        "searches": 1,
        "operations": 1,
        "paged_searches": 1,
        "results": 1
    }
    run_and_validate(expected, access_log, test_name="test_paged_search")

def test_persistant_search(topology_st, get_access_log_path):
    """Test that a persistant search is correctly parsed by logconv.

    :id: 01afcd41-dc47-46c2-a852-5084e1acd9ac
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Perform a persistant search using the PersistentSearchControl.
        3. Run logconv on the access log.
        4. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. Paged search completes without errors.
        3. logconv returns correct updated stats.
        4. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    psc = PersistentSearchControl()
    try:
        inst.search_ext(base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE, attrlist=['*'], serverctrls=[psc])
    except ldap.LDAPError as e:
            log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
            raise e

    expected = {
        "searches": 1,
        "persistent_searches": 1,
        "operations": 1,
        "entire_search_base_queries": 1,
    }
    run_and_validate(expected, access_log, test_name="test_persistant_search")

def test_simple_bind(topology_st, get_access_log_path):
    """Test that bind operations are correctly parsed by logconv.

    :id: 40d61b93-c9a0-4e64-b310-b01bc7bd4e68
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Add an LDAP user.
        3. Bind as user.
        4. Unbind.
        5. Run logconv on the access log.
        6. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. Users are added without errors.
        3. Bind succeeds.
        4. Unbind suceeds.
        5. logconv returns correct updated stats.
        6. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'user_bind',
        'sn': 'bind',
        'cn': 'bind',
        'uidNumber': '6000',
        'gidNumber': '6000',
        'homeDirectory': '/home/user_bind',
        'mail': 'user_bind@example.com',
        'userpassword': 'password',
    })
    conn = user.bind('password')
    conn.close()

    expected = {
        "adds": 1,
        "binds": 1,
        "unbinds": 1,
        "searches": 3,
        "operations": 5,
        "results": 5,
        "total_connections": 1,
        "concurrent_conns": 1,
        "ldap_conns": 1,
        "fds_taken": 1,
        "fds_returned": 1
    }
    run_and_validate(expected, access_log, test_name="test_simple_bind")

def test_anon_bind(topology_st, get_access_log_path):
    """Test that anonymous bind operations are correctly parsed by logconv.

    :id: 33d1c79b-920b-4eb7-b3da-fb268c60efcb
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Perform an anonymous bind and unbind.
        3. Run logconv on the access log.
        4. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. Anonymous bind and unbind complete without error.
        3. logconv returns correct updated stats.
        4. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    conn = Anonymous(inst).bind()
    conn.close()

    expected = {
        "binds": 1,
        "unbinds": 1,
        "anon_binds": 1,
        "searches": 1,
        "operations": 2,
        "results": 2,
        "ldap_conns": 1,
        "concurrent_conns": 1,
        "total_connections": 1,
        "fds_taken": 1,
        "fds_returned": 1
    }
    run_and_validate(expected, access_log, test_name="test_anon_bind")

def test_ldapi(topology_st, get_access_log_path):
    """Test that LDAPI connections and autobinds are correctly parsed by logconv.

    :id: 2bb88c61-d0b0-4967-9918-a9d6aef04fe1
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Enable and configure LDAPI and autobind.
        3. Restart the instance.
        4. Perform an LDAPI search using EXTERNAL bind.
        5. Run logconv on the access log.
        6. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. LDAPI setup succeed.
        3. Restart succeed.
        4. EXTERNAL bind works.
        5. logconv returns correct updated stats.
        6. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    inst.config.set('nsslapd-ldapilisten', 'on')
    inst.config.set('nsslapd-ldapiautobind', 'on')
    inst.config.set('nsslapd-ldapifilepath', f'/var/run/slapd-{inst.serverid}.socket')
    inst.restart()
    ldapi_socket = inst.config.get_attr_val_utf8('nsslapd-ldapifilepath').replace('/', '%2F')
    cmd = [
        "ldapsearch",
        "-H", f"ldapi://{ldapi_socket}",
        "-Y", "EXTERNAL",
        "-b", "dc=example,dc=com"
    ]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    expected = {
        "mods": 3,
        "autobinds": 1,
        "sasl_binds": 1,
        "binds": 3,
        "unbinds": 1,
        "searches": 2,
        "operations": 8,
        "results": 7,
        "entire_search_base_queries": 1,
        "unindexed_components": 1,
        "ldap_conns": 1,
        "ldapi_conns": 1,
        "concurrent_conns": 1,
        "total_connections": 2,
        "fds_taken": 2,
        "fds_returned": 2,
        "restarts": 1
    }
    run_and_validate(expected, access_log, test_name="test_ldapi")

def test_restart(topology_st, get_access_log_path):
    """Test that instance restarts are correctly parsed by logconv.

    :id: e3338725-c2c5-487a-b296-80c0f2d94243
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Restart the instance twice.
        3. Run logconv on the access log.
        4. Validate that restart-related stats are accurate.
    :expectedresults:
        1. Access log is successfully cleared.
        2. Restarts complete without issue.
        3. logconv returns correct updated stats.
        4. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    inst.restart()
    inst.restart()

    expected = {
        "binds": 2,
        "operations": 2,
        "results": 2,
        "ldap_conns": 2,
        "total_connections": 2,
        "fds_taken": 2,
        "fds_returned": 2,
        "restarts": 2
    }
    run_and_validate(expected, access_log, test_name="test_restart")

def test_invalid_filter(topology_st, get_access_log_path):
    """Test that VLV and SSS (server-side sort) operations are correctly parsed by logconv.

    :id: 9a9eef5e-56dd-4365-8cc9-7e9b2de03004
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Perform a search with VLV and SSS controls.
        3. Run logconv on the access log.
        4. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. VLV/SSS search completes successfully.
        3. logconv returns correct updated stats.
        4. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    inst.search_ext_s(
        base=DEFAULT_SUFFIX,
        scope=ldap.SCOPE_SUBTREE,
        filterstr='(fuckoff=*)',
    )

    expected = {
        "invalid_attribute_filters": 1,
        "searches": 1,
        "operations": 1,
        "results": 1
    }
    run_and_validate(expected, access_log, test_name="test_invalid_filter")

def test_partially_unindexed_vlv_sort(topology_st, get_access_log_path):
    """Test that an unindexed VLV and SSS search is correctly identified by logconv.

    :id: b387deee-0c35-46c5-95a2-110017c3cdb7
    :setup: Standalone Instance with no ordering index on givenName
    :steps:
        1. Truncate the access log.
        2. Perform a search with VLV and SSS controls using an unindexed attribute.
        3. Run logconv on the access log.
        4. Validate that logconv detects unindexed component usage.
    :expectedresults:
        1. Access log is successfully cleared.
        2. VLV/SSS search completes (may be slower).
        3. logconv returns correct updated stats.
        4. Unindexed component is correctly flagged in stats.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    vlv_control = VLVRequestControl(
        criticality=True,
        before_count="1",
        after_count="3",
        offset="5",
        content_count=0,
        greater_than_or_equal=None,
        context_id=None
    )
    sss_control = SSSRequestControl(criticality=True, ordering_rules=['givenName'])  # assume not indexed

    inst.search_ext_s(
        base=DEFAULT_SUFFIX,
        scope=ldap.SCOPE_SUBTREE,
        filterstr='(givenName=*)',
        serverctrls=[vlv_control, sss_control]
    )

    expected = {
        "vlv_operations": 1,
        "sort_operations": 1,
        "searches": 1,
        "unindexed_components": 0,
        "operations": 2,
        "results": 1
    }
    run_and_validate(expected, access_log, test_name="test_partially_unindexed_vlv_sort")

def test_fully_unindexed_search(topology_st, get_access_log_path):
    """Test that a fully unindexed search is correctly identified by logconv.

    :id: 71f93a9f-3f9d-450e-ae98-79fabe5f21f4
    :setup: Standalone Instance with no index on 'uid' attribute
    :steps:
        1. Truncate the access log.
        2. Delete the index on 'uid' attribute if it exists.
        3. Perform a search filtering on 'uid' attribute which is now unindexed.
        4. Run logconv on the access log.
        5. Validate that logconv detects fully unindexed filter usage.
    :expectedresults:
        1. Access log is successfully cleared.
        2. Search completes (may be slower due to missing index).
        3. logconv returns correct updated stats.
        4. Fully unindexed component is correctly flagged in stats.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    # Force an unindexed search
    db_cfg = DatabaseConfig(inst)
    default_idlistscanlimit = db_cfg.get_attr_vals_utf8('nsslapd-idlistscanlimit')
    db_cfg.set([('nsslapd-idlistscanlimit', '100')])
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    for i in range(101):
        users.create_test_user(uid=i)
    raw_objects = DSLdapObjects(inst, basedn=DEFAULT_SUFFIX)
    raw_objects.filter("(description=test*)")
    db_cfg.set([('nsslapd-idlistscanlimit', default_idlistscanlimit)])


    expected = {
        "mods": 2,
        "adds": 101,
        "searches": 205,
        "unindexed_searches": 1,
        "operations": 308,
        "results": 308,
    }
    run_and_validate(expected, access_log, test_name="test_fully_unindexed_search")

def test_ldaps(topology_st, get_access_log_path):
    """Test that LDAPS operations are correctly parsed by logconv.

    :id: bde3cf6c-d8e4-41cd-b6c8-060fde6a3613
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Enable TLS (LDAPS).
        3. Perform a secure search.
        4. Run logconv on the access log.
        5. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. TLS is enabled.
        3. Search over LDAPS works.
        4. logconv returns correct updated stats.
        5. Expected stats match logconv output.
    """
    inst = topology_st.standalone
    access_log = truncate_access_log(get_access_log_path)

    inst.enable_tls()
    inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=*)')

    expected = {
        "mods": 1,
        "binds": 1,
        "searches": 3,
        "operations": 5,
        "results": 5,
        "entire_search_base_queries": 1,
        "unindexed_components": 1,
        "ldaps_conns": 1,
        "total_connections": 1,
        "fds_taken": 1,
        "fds_returned": 1,
        "restarts": 1
    }
    run_and_validate(expected, access_log, test_name="test_ldaps")

def test_starttls(topology_st, get_access_log_path):
    """Test that StartTLS operations are correctly parsed by logconv.

    :id: e3a23a36-1f08-4d5d-9a11-88c1a8643902
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Perform an LDAP search over StartTLS.
        3. Run logconv on the access log.
        4. Validate that StartTLS and search operations are correctly reported.
    :expectedresults:
        1. Access log is successfully cleared.
        2. StartTLS search completes without errors.
        3. logconv returns correct updated stats.
        4. Expected stats match logconv output.
    """
    access_log = truncate_access_log(get_access_log_path)

    cmd = [
        "ldapsearch",
        "-x",
        "-H", f"ldap://localhost:38901",
        "-Z",
        "-D", "cn=Directory Manager",
        "-w", "password",
        "-b", DEFAULT_SUFFIX,
        "(objectclass=*)"
    ]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    expected = {
        "starttls": 1,
        "extended_ops": 1,
        "operations": 1,
        "results": 1,
        "ldap_conns": 1,
        "total_connections": 1,
        "fds_taken": 1,
        "fds_returned": 1
    }
    run_and_validate(expected, access_log, test_name="test_starttls")

def test_validate_all_summary(get_access_log_path):
    """Validate the summary output from logconv if per test validation is disabled.

    :id: 63df9ed9-e38e-4228-974b-3e4c34845e27
    :setup: Standalone Instance
    :steps:
        1. Truncate the access log.
        2. Run logconv on the accumulated access log.
        3. Validate that output stats match expected values.
    :expectedresults:
        1. Access log is successfully cleared.
        2. logconv returns correct updated stats.
        3. Expected stats match logconv output.
    """
    if not VALIDATE_EACH_TEST:
        access_log = truncate_access_log(get_access_log_path)
        output = run_logconv(access_log, debug_output=True)
        assert_summary(VALIDATE_ALL_SUMMARY, output, test_name="final_summary")

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])