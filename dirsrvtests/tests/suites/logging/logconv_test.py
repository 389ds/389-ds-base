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
from lib389.idm.account import Anonymous
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
from lib389.config import CertmapLegacy
from lib389.nss_ssl import NssSsl
from lib389.topologies import create_topology

log = logging.getLogger(__name__)

class LogConvTest:
    TEST_TIMEOUT = 10

    def __init__(self, instance, log_format="default"):
        self.inst = instance
        self.log_format = log_format 
        self.logconv_path = self.get_logconv_path()
        self.access_log_path = self.get_access_log_path()
        self.expected = None
        self.debug_output = 'logconv-debug.log'

    def get_access_log_path(self, timeout: int = 10):
        """
        Get server access log file.
        """
        start = time.time()
        while not self.inst.state == DIRSRV_STATE_ONLINE:
            if (time.time() - start > timeout):
                pytest.fail("Instance failed to come online before: {timeout}")
            time.sleep(1) 

        path = self.inst.config.get_attr_val_utf8('nsslapd-accesslog')
        if not path:
            pytest.fail("Access log path not found: {path}")
            return None

        return path

    def get_logconv_path(self):
        """ 
        Get the location of logconv script.
        """
        return shutil.which("logconv.py") or None

    def configure_logs(self, format: str = "default", buffering: str = "off"):
        """
        Configure logbuffering and log-format values.
        """
        self.inst.config.set('nsslapd-accesslog-logbuffering', buffering)
        self.inst.config.set('nsslapd-accesslog-log-format', format)

        self.log_format = format 

    def truncate_logs(self):
        """ 
        Truncate logs between test runs.
        """
        if not self.access_log_path:
            pytest.fail(f"Access log not found. {self.access_log_path}")

        with open(self.access_log_path, "w") as f:
            f.truncate(0)
    
    def run_operation(self, operation_function):
        """
        Run an LDAP operation.
        """
        if not callable(operation_function):
            pytest.fail(f"Operation function must be callable. {operation_function}")

        try:
            operation_function(self)
        except Exception as e:
            log.error(f"{operation_function} failed: {e}")
            raise 

    def parse_summary(self, output: str):
        """
        Parse the default logconv output and extract key numeric statistics.

        Uses regex patterns to match lines in the logconv output.

        Args:
            output (str): The raw output from logconv.
        """
        patterns = {
            # Server stats
            "restarts": r"^Restarts:\s+(\d+)",

            # Connections
            "concurrent_conns": r"^Peak Concurrent connections:\s+(\d+)",
            "total_connections": r"^Total connections:\s+(\d+)",
            "ldap_conns": r"- LDAP connections:\s+(\d+)",
            "ldapi_conns": r"- LDAPI connections:\s+(\d+)",
            "ldaps_conns": r"- LDAPS connections:\s+(\d+)",
            "starttls_conns": r"- StartTLS Extended Ops:\s+(\d+)",
            

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
            "ssl_client_binds": r"SSL Client Binds:\s+(\d+)",
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

    def run_logconv(self, debug_output=True):
        """
        Run logconv on the access log and return its output.

        Args:
            debug_output (bool): If True, writes logconv output to a debug file.
        """
        
        if not self.logconv_path:
            raise FileNotFoundError(f"{self.logconv_path} not found.")

        if not self.access_log_path:
            raise FileNotFoundError(f"{self.access_log_path} not found.")
        
        try:
            result = subprocess.run(
                [self.logconv_path, self.access_log_path],
                capture_output=True,
                text=True
            )
        except FileNotFoundError as e:
            print("Exception message 1:", e.value)
            raise FileNotFoundError(f"Logconv script not found: {self.logconv_path}") from e
        except Exception as e:
             print("Exception message 2:", e.value)
             raise RuntimeError(f"Logconv script unexpected error: {e}") from e

        if result.returncode != 0:
            raise RuntimeError(f"{self.logconv_path} failed: {result.stderr}")

        summary = result.stdout

        if debug_output:
            with open(self.debug_output, 'w') as f:
                f.write(summary)
            os.chmod(self.debug_output, 0o644)

        return summary

    def wait_for_log_counts(self, expected: dict, test_name=None, poll_interval=1):
        """
        Wait for the parsed logconv output to meet or exceed the expected op count, 
        retrying every poll_interval until the condition is met or it times out.

        The comparison is loose, it passes once the actual counts are greater than or equal 
        to the expected count.

        Args:
            expected: dict of expected exact counts
            test_name: string for logging
            poll_interval: how often to poll the logs in seconds
        """
        IGNORE_KEYS = {"highest_fd_taken", "concurrent_conns"}

        now = time.time()
        while True:
            errors = []
            try:
                output = self.run_logconv()
            except (FileNotFoundError, RuntimeError) as e:
                log.error(f"{test_name}: Error running logconv: {e}")
                raise

            summary = self.parse_summary(output)
            for key, existing_val in summary.items():
                if key in IGNORE_KEYS:
                    continue
                expected_val = expected.get(key, 0)
                if existing_val < expected_val:
                    errors.append(f"{test_name} - {key}: expected {expected_val}, got {existing_val}")

            if not errors:
                log.info(f"{test_name}: Pass.")
                return

            if (time.time() - now) > self.TEST_TIMEOUT:
                error_report = "\n".join(errors)
                return f"\n{error_report}"
 
            time.sleep(poll_interval)

@pytest.fixture
def fresh_instance():
    topo = create_topology({ReplicaRole.STANDALONE: 1})
    inst = topo.standalone
    inst.start()
    yield inst
    inst.stop()
    inst.delete(escapehatch="i am sure")

@pytest.mark.parametrize("log_format", ["default", "json"])
def test_logconv_suite(fresh_instance, log_format):
    """
    Validate logconv.py reported stats for default and json access log formats.

    :id: aee85f04-ddb8-4732-adba-2100ab94cd7b
    :setup: Standalone Instance
    :steps:
        1. Test ADD stats
        2. Test DELETE stats
        3. Test MODIFY stats
        4. Test MODRDN stats
        5. Test COMPARE stats
        6. Test SEARCH stats
        7. Test internal stats
        8. Test BASE SEARCH stats
        9. Test PAGED SEARCH stats
        10. Test PERSISTENT SEARCH stats
        11. Test SIMPLE BIND stats
        12. Test ANONYMOUS BIND stats
        13. Test LDAPI BIND stats
        14. Test server restart stats
        15. Test invalid filter stats
        16. Test partially unindexed VLV sort stats
        17. Test fully unindexed search stats
        18. Test LDAPS connection stats
        19. Test STARTTLS operation stats
        20. Test behavior with missing access log
        21. Test behavior with invalid access log format
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
        17. Success
        18. Success
        19. Success
        20. Success
        21. Success
    """
    log.info(f"Running logconv test suite in {log_format.upper()} mode\n")

    test_env = LogConvTest(fresh_instance, log_format)
    failures = []

    def run(test_func):
        try:
            test_func(test_env)
            err = test_env.wait_for_log_counts(expected=test_env.expected, test_name=test_func.__name__)
            if err:
                full_name = f"{log_format} - {test_func.__name__}"
                # raise AssertionError(f"\n\n=== Logconv Test Failure ===\n\n{full_name}:\n{err}")
                failures.append((full_name, err))
        except Exception as e:
            full_name = f"{log_format} - {test_func.__name__}"
            failures.append((full_name, f"Unexpected exception: {e}"))

    test_cases = [
        _test_add_del,
        _test_modify,
        _test_modrdn,
        _test_compare,
        _test_search,
        _test_internal_ops,
        _test_base_search,
        _test_paged_search,
        _test_persistant_search,
        _test_simple_bind,
        _test_anon_bind,
        _test_ldapi,
        _test_restart,
        _test_invalid_filter,
        _test_partially_unindexed_vlv_sort,
        _test_fully_unindexed_search,
        _test_ldaps,
        _test_starttls,
        _test_missing_access_log,
        _test_invalid_access_log_format
    ]

    test_env.configure_logs(log_format)

    for test in test_cases:
        run(test)

    if os.path.exists(test_env.debug_output):
        os.remove(test_env.debug_output)

    if failures:
        summary_lines = []
        summary_lines.append("=== Logconv Test Failures ===\n")

        for name, error in failures:
            summary_lines.append(f"{name}:")
            summary_lines.extend(f"  {line}" for line in error.strip().splitlines())
            summary_lines.append("")

        summary = "\n".join(summary_lines)
        raise AssertionError(f"\n{summary}")

def _test_add_del(test_env):

    def _add_del_users(test_env):
        users = UserAccounts(test_env.inst, DEFAULT_SUFFIX)
        try:
            for idx in range(3):
                users.create_test_user(uid=str(idx), gid=str(idx))
        except ldap.ALREADY_EXISTS as e:
            log.warning(f"_test_add_del - User already exists: {e}")

        for dn in users.list():
            try:
                dn.delete()
            except Exception as e:
                log.warning(f"Failed to delete user: {e}")

    test_env.operation = _add_del_users

    test_env.expected = {
        "adds": 3,
        "deletes": 3,
        "searches": 13,
        "operations": 19,
        "results": 19
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_modify(test_env):

    def _mod_user_passwd(test_env):
        users = UserAccounts(test_env.inst, DEFAULT_SUFFIX)
        user = users.create_test_user(uid=1, gid=1)
        user.set("userPassword", "newpass")
        user.delete()

    test_env.operation = _mod_user_passwd

    test_env.expected = {
        "adds": 1,
        "mods": 1,
        "deletes": 1,
        "searches": 2,
        "operations": 5,
        "results": 5
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_modrdn(test_env):

    def _user_modrdn(test_env):
        users = UserAccounts(test_env.inst, DEFAULT_SUFFIX)
        user = users.create_test_user(uid=1, gid=1)
        user.rename('uid=user_modrdn_renamed')
        user.delete()

    test_env.operation = _user_modrdn

    test_env.expected = {
        "adds": 1,
        "deletes": 1,
        "modrdns": 1,
        "searches": 3,
        "operations": 6,
        "results": 6
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_compare(test_env):

    def _user_compare(test_env):
        users = UserAccounts(test_env.inst, DEFAULT_SUFFIX)
        user = users.create_test_user(uid=1, gid=1)
        test_env.inst.compare_ext_s(user.dn, 'cn', b'compare')
        user.delete()

    test_env.operation = _user_compare
    
    test_env.expected = {
        "adds": 1,
        "deletes": 1,
        "compares": 1,
        "searches": 2,
        "operations": 5,
        "results": 5
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_search(test_env):

    def _norm_search(test):
        try:
            test_env.inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=findmeifucan)')
        except ldap.LDAPError as e:
            log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
            raise e

    test_env.operation = _norm_search

    test_env.expected = {
        "searches": 1,
        "operations": 1,
        "results": 1
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_internal_ops(test_env):

    def _internal_op(test_env):
        default_log_level = test_env.inst.config.get_attr_val_utf8(LOG_ACCESS_LEVEL)
        access_log_level = '4'
        test_env.inst.config.set(LOG_ACCESS_LEVEL, access_log_level)
        test_env.inst.config.set('nsslapd-plugin-logging', 'on')
        test_env.inst.config.set(LOG_ACCESS_LEVEL, default_log_level)
        test_env.inst.config.set('nsslapd-plugin-logging', 'off')

    test_env.operation = _internal_op

    test_env.expected = {
        "internal_ops": 2,
        "mods": 2,
        "searches": 3,
        "operations": 5,
        "results": 5,
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_base_search(test_env):

    def _base_search(test_env):
        try:
            test_env.inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'])
        except ldap.LDAPError as e:
                log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
                raise e

    test_env.operation = _base_search

    test_env.expected = {
        "searches": 1,
        "operations": 1,
        "entire_search_base_queries": 1,
        "results": 1
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_paged_search(test_env):

    def _paged_search(test_env):
        req_ctrl = SimplePagedResultsControl(True, size=3, cookie='')
        try:
            test_env.inst.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "sn=user_*", ['cn'], serverctrls=[req_ctrl])
        except ldap.LDAPError as e:
                log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
                raise e

    test_env.operation = _paged_search

    test_env.expected = {
        "searches": 1,
        "operations": 1,
        "paged_searches": 1,
        "results": 1
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_persistant_search(test_env):

    def _persistant_search(test_env):
        psc = PersistentSearchControl()

        msgid = test_env.inst.search_ext(
            base=DEFAULT_SUFFIX,
            scope=ldap.SCOPE_ONELEVEL,
            filterstr='(objectClass=person)',
            attrlist=['uid'],
            serverctrls=[psc]
        )

        test_env.inst.abandon(msgid)

    test_env.operation = _persistant_search

    test_env.expected = {
        "searches": 1,
        "persistent_searches": 1,
        "abandoned": 1,
        "operations": 2,
        "results": 1, # abandoned op counted as a result
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)


def _test_simple_bind(test_env):

    def _simple_bind(test_env):


        users = UserAccounts(test_env.inst, DEFAULT_SUFFIX)
        user = users.create_test_user(uid=1, gid=1)
        user.set("userPassword", "password")

        conn = ldap.initialize("ldap://%s:%s" % (HOST_STANDALONE, PORT_STANDALONE))
        conn.simple_bind_s(user.dn, "password")
        conn.unbind_s()
        user.delete()

    test_env.operation = _simple_bind

    test_env.expected = {
        "adds": 1,
        "mods": 1,
        "deletes": 1,
        "binds": 1,
        "unbinds": 1,
        "searches": 2,
        "operations": 6,
        "results": 6,
        "total_connections": 1,
        "concurrent_conns": 1,
        "ldap_conns": 1,
        "fds_taken": 1,
        "fds_returned": 1
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_anon_bind(test_env):

    def _anon_bind(test_env):
        conn = Anonymous(test_env.inst).bind()
        conn.unbind()

    test_env.operation = _anon_bind

    test_env.expected = {
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

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_ldapi(test_env):

    def _ldapi_search(tetest_envt):
        test_env.inst.config.set('nsslapd-ldapilisten', 'on')
        test_env.inst.config.set('nsslapd-ldapiautobind', 'on')
        test_env.inst.config.set('nsslapd-ldapifilepath', f'/var/run/slapd-{test_env.inst.serverid}.socket')
        test_env.inst.restart()
        ldapi_socket = test_env.inst.config.get_attr_val_utf8('nsslapd-ldapifilepath').replace('/', '%2F')
        cmd = [
            "ldapsearch",
            "-H", f"ldapi://{ldapi_socket}",
            "-Y", "EXTERNAL",
            "-b", "dc=example,dc=com"
        ]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    test_env.operation = _ldapi_search

    test_env.expected = {
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

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_restart(test_env):

    def _server_restart(test_env):
        test_env.inst.restart()
        test_env.inst.restart()

    test_env.operation = _server_restart

    test_env.expected = {
        "binds": 2,
        "operations": 2,
        "results": 2,
        "ldap_conns": 2,
        "total_connections": 2,
        "fds_taken": 2,
        "fds_returned": 2,
        "restarts": 2
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_invalid_filter(test_env):

    def _search_invalid_filter(test_env):
        test_env.inst.search_ext_s(
            base=DEFAULT_SUFFIX,
            scope=ldap.SCOPE_SUBTREE,
            filterstr='(idontexist=*)',
        )

    test_env.operation = _search_invalid_filter

    test_env.expected = {
        "invalid_attribute_filters": 1,
        "searches": 1,
        "operations": 1,
        "results": 1
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_partially_unindexed_vlv_sort(test_env):

    def _search_partially_unindexed_vlv_sort(test_env):
        vlv_control = VLVRequestControl(
            criticality=True,
            before_count="1",
            after_count="3",
            offset="5",
            content_count=0,
            greater_than_or_equal=None,
            context_id=None
        )
        sss_control = SSSRequestControl(criticality=True, ordering_rules=['givenName'])

        test_env.inst.search_ext_s(
            base=DEFAULT_SUFFIX,
            scope=ldap.SCOPE_SUBTREE,
            filterstr='(givenName=*)',
            serverctrls=[vlv_control, sss_control]
        )

    test_env.operation = _search_partially_unindexed_vlv_sort
    
    test_env.expected = {
        "vlv_operations": 1,
        "sort_operations": 1,
        "searches": 1,
        "unindexed_components": 0,
        "operations": 2,
        "results": 1
    }
    
    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_fully_unindexed_search(test_env):

    def _search_fully_unindexed(test_env):
        # Force an unindexed search
        db_cfg = DatabaseConfig(test_env.inst)
        default_idlistscanlimit = db_cfg.get_attr_vals_utf8('nsslapd-idlistscanlimit')
        db_cfg.set([('nsslapd-idlistscanlimit', '100')])
        users = UserAccounts(test_env.inst, DEFAULT_SUFFIX)
        for i in range(10):
            users.create_test_user(uid=i)
        raw_objects = DSLdapObjects(test_env.inst, basedn=DEFAULT_SUFFIX)
        raw_objects.filter("(description=test*)")
        db_cfg.set([('nsslapd-idlistscanlimit', default_idlistscanlimit)])

        for dn in users.list():
            try:
                dn.delete()
            except Exception as e:
                log.warning(f"Failed to delete user: {e}")

    test_env.operation = _search_fully_unindexed

    test_env.expected = {
        "mods": 2,
        "adds": 10,
        "deletes": 10,
        "searches": 44,
        "unindexed_components": 1,
        "operations": 66,
        "results": 66,
    }
    
    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_ldaps(test_env):

    def _search_ldaps(test_env):
        
        RDN_TEST_USER = 'testuser'
        RDN_TEST_USER_WRONG = 'testuser_wrong'
        test_env.inst.enable_tls()
        test_env.inst.restart()

        users = UserAccounts(test_env.inst, DEFAULT_SUFFIX)
        user = users.create(properties={
            'uid': RDN_TEST_USER,
            'cn': RDN_TEST_USER,
            'sn': RDN_TEST_USER,
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': f'/home/{RDN_TEST_USER}'
        })

        ssca_dir = test_env.inst.get_ssca_dir()
        ssca = NssSsl(dbpath=ssca_dir)
        ssca.create_rsa_user(RDN_TEST_USER)
        ssca.create_rsa_user(RDN_TEST_USER_WRONG)

        # Get the details of where the key and crt are.
        tls_locs = ssca.get_rsa_user(RDN_TEST_USER)
        tls_locs_wrong = ssca.get_rsa_user(RDN_TEST_USER_WRONG)

        user.enroll_certificate(tls_locs['crt_der_path'])

        # Turn on the certmap.
        cm = CertmapLegacy(test_env.inst)
        certmaps = cm.list()
        certmaps['default']['DNComps'] = ''
        certmaps['default']['FilterComps'] = ['cn']
        certmaps['default']['VerifyCert'] = 'off'
        cm.set(certmaps)

        # Check that EXTERNAL is listed in supported mechns.
        assert (test_env.inst.rootdse.supports_sasl_external())

        # Restart to allow certmaps to be re-read: Note, we CAN NOT use post_open
        # here, it breaks on auth. see lib389/__init__.py
        test_env.inst.restart(post_open=False)

        # Attempt a bind with TLS external
        test_env.inst.open(saslmethod='EXTERNAL', connOnly=True, certdir=ssca_dir,
                userkey=tls_locs['key'], usercert=tls_locs['crt'])
    
        test_env.inst.restart()

        # Check for failed certmap error
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            test_env.inst.open(saslmethod='EXTERNAL', connOnly=True, certdir=ssca_dir,
                    userkey=tls_locs_wrong['key'], usercert=tls_locs_wrong['crt'])

    test_env.operation = _search_ldaps

    test_env.expected = {
        "mods": 1,
        "binds": 1,
        "searches": 3,
        "operations": 5,
        "results": 5,
        "ldaps_conns": 1,
        "ssl_client_binds": 1,
        "ssl_client_bind_failed": 1,
        "total_connections": 1,
        "fds_taken": 1,
        "fds_returned": 1,
        "restarts": 1
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_starttls(test_env):

    def _search_starttls(test_env):
        cmd = [
            "ldapsearch",
            "-x",
            "-H", f"ldap://localhost:{test_env.inst.port}",
            "-Z",
            "-D", "cn=Directory Manager",
            "-w", "password",
            "-b", DEFAULT_SUFFIX,
            "(objectclass=*)"
        ]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    test_env.operation = _search_starttls
    
    test_env.expected = {
        "starttls_conns": 1,
        "extended_ops": 1,
        "operations": 1,
        "results": 1,
        "ldap_conns": 1,
        "total_connections": 1,
        "fds_taken": 1,
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_missing_access_log(test_env):

    def _missing_access_log(test_env):
        orig_access_log_path = test_env.access_log_path
        test_env.access_log_path = "somewhereovertherainbow"
        try:
            with pytest.raises(RuntimeError):
                test_env.run_logconv()
                
        finally:
            test_env.access_log_path = orig_access_log_path

    test_env.operation = _missing_access_log

    test_env.expected = {}

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

def _test_invalid_access_log_format(test_env):
    def _invalid_access_log(test_env):
        orig_access_log_path = test_env.access_log_path
        invalid_log_path = "invalid_access.log"

        lines = [
            "[what do you mean im not an access log]\n",
            "!!!@@@###$$$%%%^^^&&&***((()))\n",
            "1234567890abcdefg!@#$%^&*()_+\n",
            "???????/////\\\\\\\\\\|||||;;;;;\n",
            "2025-07-28 20:53:16.818100+00:00 Something weird happened here\n",
            "ERROR [2025-07-28] Unexpected token < in JSON at position 0\n",
            "NULL NULL NULL NULL NULL NULL\n",
            "\x00\x01\x02\x03\x04\x05\x06\x07\x08\n",
            "[28/Jul/2025:23:35:34.122912055 +0000] conn=1 op=7 SRCH base=\"dc=example,dc=com\" scope=2 filter=\"(idontexist=*)\" attrs=ALL\n",
            "[28/Jul/2025:23:35:34.188751644 +0000] conn=1 op=7 RESULT err=0 tag=101 nentries=0 wtime=0.000131574 optime=0.065835295 etime=0.065964499 notes=F details=\"Filter Element Missing From Schema\" - Invalid attribute in filter - results may not be complete.\n",
            "{\"local_time\":\"2025-07-28T23:41:31.354305288 +0000\",\"operation\":\"SEARCH\",\"key\":\"1753746090-1\",\"conn_id\":1,\"op_id\":7,\"base_dn\":\"dc=example,dc=com\",\"scope\":2,\"filter\":\"(idontexist=*)\"}\n",
            "{\"local_time\":\"2025-07-28T23:41:31.435371287 +0000\",\"operation\":\"RESULT\",\"key\":\"1753746090-1\",\"conn_id\":1,\"op_id\":7,\"msg\":\" - Invalid attribute in filter - results may not be complete.\",\"tag\":101,\"err\":0,\"nentries\":0,\"wtime\":\"0.000200687\",\"optime\":\"0.081066609\",\"etime\":\"0.081264698\",\"client_ip\":\"127.0.0.1\",\"notes\":[{\"note\":\"F\",\"description\":\"Filter Element Missing From Schema\",\"filter\":\"(idontexist=*)\"}]}\n",
            "{\"local_time\":\"2025-07-28T23:55:11.618760703 +0000\",\"operation\":\"ADD\",\"key\":\"1753746910-1\",\"conn_id\":1,\"op_id\":9,\"target_dn\":\"uid=test_user_1,ou=People,dc=example,dc=com\"}\n",
            "{\"local_time\":\"2025-07-28T23:55:11.805266683 +0000\",\"operation\":\"RESULT\",\"key\":\"1753746910-1\",\"conn_id\":1,\"op_id\":9,\"msg\":\"\",\"tag\":105,\"err\":0,\"nentries\":0,\"wtime\":\"0.000128994\",\"optime\":\"0.186508623\",\"etime\":\"0.186634117\",\"client_ip\":\"127.0.0.1\"}\n",
            "{\"local_time\":\"2025-07-28T23:55:11.805818046 +0000\",\"operation\":\"MODRDN\",\"key\":\"1753746910-1\",\"conn_id\":1,\"op_id\":10,\"target_dn\":\"uid=test_user_1,ou=People,dc=example,dc=com\",\"newrdn\":\"uid=user_modrdn_renamed\",\"deleteoldrdn\":true}\n",
            "{\"local_time\":\"2025-07-28T23:55:12.049130196 +0000\",\"operation\":\"RESULT\",\"key\":\"1753746910-1\",\"conn_id\":1,\"op_id\":10,\"msg\":\"\",\"tag\":109,\"err\":0,\"nentries\":0,\"wtime\":\"0.000127592\",\"optime\":\"0.243312815\",\"etime\":\"0.243438839\",\"client_ip\":\"127.0.0.1\"}\n"
        ]

        with open(invalid_log_path, "w") as f:
            for line in lines:
                f.write(line)

        test_env.access_log_path = invalid_log_path

    test_env.operation = _invalid_access_log

    test_env.expected = {
        "searches": 2,
        "add": 1,
        "modrdn": 1,
        "operations": 4
    }

    test_env.truncate_logs()
    test_env.run_operation(test_env.operation)

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])