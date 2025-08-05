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

@pytest.fixture(scope="class", params=["default", "json"])
def topology_st(request):
    """Create DS standalone instance"""
    log_format = request.param
    topology = create_topology({ReplicaRole.STANDALONE: 1})
    topology.standalone.config.set('nsslapd-accesslog-logbuffering', "off")
    topology.standalone.config.set('nsslapd-accesslog-log-format', log_format)

    def fin():
        if topology.standalone.exists():
            topology.standalone.delete()
    request.addfinalizer(fin)

    return topology

class TestLogconv:

    @pytest.fixture(autouse=True)
    def setup(self, topology_st):
        self.inst = topology_st.standalone
        self.logconv_path = shutil.which("logconv.py") or None
        self.log_format = self.inst.config.get_attr_val_utf8("nsslapd-accesslog-log-format")
        self.access_log_path = self.get_access_log_path()
        self.debug_output = 'logconv-debug.log'

    def get_access_log_path(self):
        """
        Get server access log file.
        """
        path = self.inst.config.get_attr_val_utf8('nsslapd-accesslog')
        if not path:
            pytest.fail("Access log path not found: {path}")
            return None

        return path

    def truncate_logs(self):
        """ 
        Truncate logs between test runs.
        """
        if not self.access_log_path:
            pytest.fail(f"Access log not found. {self.access_log_path}")

        time.sleep(3)
        with open(self.access_log_path, "w") as f:
            f.truncate(0)

    def extract_logconv_stats(self, output: str):
        """
        Parse the default logconv output and extract key counts.

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

        logconv_stats = {}
        for key, pattern in patterns.items():
            match = re.search(pattern, output, re.IGNORECASE | re.MULTILINE)
            if match:
                value = match.group(1)
                try:
                    logconv_stats[key] = float(value) if '.' in value else int(value)
                except (ValueError) as e:
                    pytest.fail(f"Failed to parse value for {key}")

        return logconv_stats

    def run_logconv(self, debug_output=True):
        """
        Run logconv on the access log and return its output.

        Args:
            debug_output (bool): If True, writes logconv output to a debug file.
        """

        if not self.logconv_path:
            raise FileNotFoundError("logconv.py not found.")

        if not self.access_log_path:
            raise FileNotFoundError("access log not found.")

        try:
            result = subprocess.run(
                [self.logconv_path, self.access_log_path],
                capture_output=True,
                text=True
            )
        except Exception as e:
            raise RuntimeError(f"Failed to run logconv.py: {e}")

        if result.returncode != 0:
            raise RuntimeError(f"logconv.py exited with error: {result.stderr}")

        logconv_stats = result.stdout

        if debug_output:
            with open(self.debug_output, 'w') as f:
                f.write(logconv_stats)
            os.chmod(self.debug_output, 0o644)

        return logconv_stats

    def validate_logconv_stats(self, expected: dict, logconv_stats: dict, test_name=None):
        """
        The comparison is loose, it passes once the actual counts are greater than or equal 
        to the expected count.

        Args:
            expected: dict of expected exact counts
            logconv_stats: dict of key value stats from logconv
            test_name: string for logging
        """
        IGNORE_KEYS = {"highest_fd_taken", "concurrent_conns"}

        errors = []
        for key, existing_val in logconv_stats.items():
            if key in IGNORE_KEYS:
                continue
            expected_val = expected.get(key, 0)
            if existing_val != expected_val:
                errors.append(f"{test_name} - {key}: expected {expected_val}, got {existing_val}")

        if errors:
            print("\n".join(errors))
            return False

        return True

    def test_add(self):
        """Validate add operation stats reported by logconv.

        :id: 7747b0e5-9dac-471a-bfb1-7b4dded6afa0
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Create 3 users
            3. Delete 3 user
            4. Run logconv
            5. Compare actual stats with expected
        :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Success
            5. Actual stats match expected
        """
        self.truncate_logs()

        users = UserAccounts(self.inst, DEFAULT_SUFFIX)
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

        expected = {
            "adds": 3,
            "deletes": 3,
            "searches": 13,
            "operations": 19,
            "results": 19
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_add")

    def test_modify(self):
        """Validate modify operation stats reported by logconv.

        :id: d5c37b3f-be79-47da-8ce3-051724f21e09
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Create a user
            3. Modify the userPassword attribute
            4. Delete the user
            5. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Success
            5. Actual stats match expected
        """
        self.truncate_logs()

        users = UserAccounts(self.inst, DEFAULT_SUFFIX)
        user = users.create_test_user(uid=1, gid=1)
        user.set("userPassword", "newpass")
        user.delete()

        expected = {
            "adds": 1,
            "mods": 1,
            "deletes": 1,
            "searches": 2,
            "operations": 5,
            "results": 5
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_modify")

    def test_modrdn(self):
        """Validate MODRDN operation stats reported by logconv.

        :id: ded94158-d92a-4696-9d49-2fb5437cae26
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Create a user
            3. Rename the user's DN
            4. Delete the user
            5. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Success
            5. Actual stats match expected
        """
        self.truncate_logs()

        users = UserAccounts(self.inst, DEFAULT_SUFFIX)
        user = users.create_test_user(uid=1, gid=1)
        user.rename('uid=user_modrdn_renamed')
        user.delete()

        expected = {
            "adds": 1,
            "deletes": 1,
            "modrdns": 1,
            "searches": 3,
            "operations": 6,
            "results": 6
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_modrdn")


    def test_compare(self):
        """Validate compare operation stats reported by logconv.

        :id: 0c874c9a-21bb-41a9-a6dd-e4ae77a24bfa
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Create a user
            3. Perform compare on 'cn' attribute
            4. Delete the user
            5. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Success
            5. Actual stats match expected
        """
        self.truncate_logs()

        users = UserAccounts(self.inst, DEFAULT_SUFFIX)
        user = users.create_test_user(uid=1, gid=1)
        self.inst.compare_ext_s(user.dn, 'cn', b'compare')
        user.delete()

        expected = {
            "adds": 1,
            "deletes": 1,
            "compares": 1,
            "searches": 2,
            "operations": 5,
            "results": 5
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_compare")

    def test_search(self):
        """Validate basic search operation stats reported by logconv.

        :id: e81e2b0b-0c67-4ba0-8dff-2371a3f91546
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Perform subtree search for non existent uid
            3. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Actual stats match expected
        """
        self.truncate_logs()

        try:
            self.inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=findmeifucan)')
        except ldap.LDAPError as e:
            log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
            raise e

        expected = {
            "searches": 1,
            "operations": 1,
            "results": 1
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_search")

    def test_internal_ops(self):
        """Validate internal operation stats reported by logconv.

        :id: 631ba514-349d-49f6-961e-a6fca3071cf3
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Modify access log config, resulting in internal ops
            3. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Actual stats match expected
        """
        self.truncate_logs()

        default_log_level = self.inst.config.get_attr_val_utf8(LOG_ACCESS_LEVEL)
        access_log_level = '4'
        self.inst.config.set(LOG_ACCESS_LEVEL, access_log_level)
        self.inst.config.set('nsslapd-plugin-logging', 'on')
        self.inst.config.set(LOG_ACCESS_LEVEL, default_log_level)
        self.inst.config.set('nsslapd-plugin-logging', 'off')

        expected = {
            "internal_ops": 2,
            "mods": 2,
            "searches": 3,
            "operations": 5,
            "results": 5,
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_internal_ops")

    def test_base_search(self):
        """Validate base search operation stats reported by logconv.

        :id: d7557ef2-582c-42bb-a9ee-ded0680ba707
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Run a base search
            3. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Actual stats match expected
        """
        self.truncate_logs()

        try:
            self.inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'])
        except ldap.LDAPError as e:
                log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
                raise e

        expected = {
            "searches": 1,
            "operations": 1,
            "entire_search_base_queries": 1,
            "results": 1
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_base_search")

    def test_paged_search(self):
        """Validate paged search operation stats reported by logconv.

        :id: 729cb831-bcbe-4f88-8f42-12be5c48b5e6
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Run a paged search
            3. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Actual stats match expected
        """

        self.truncate_logs()

        req_ctrl = SimplePagedResultsControl(True, size=3, cookie='')
        try:
            self.inst.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "sn=user_*", ['cn'], serverctrls=[req_ctrl])
        except ldap.LDAPError as e:
                log.error('Search failed on {}'.format(DEFAULT_SUFFIX))
                raise e

        # Need to wait a little here
        time.sleep(2)

        expected = {
            "searches": 1,
            "operations": 1,
            "paged_searches": 1,
            "results": 1
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_paged_search")

    def test_persistant_search(self):
        """Validate persistant search operation stats reported by logconv.

        :id: a0552c17-d70a-4fe2-b4af-9292d4862da6
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Run a persistant search
            3. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Actual stats match expected
        """
        self.truncate_logs()

        psc = PersistentSearchControl()

        msgid = self.inst.search_ext(
            base=DEFAULT_SUFFIX,
            scope=ldap.SCOPE_ONELEVEL,
            filterstr='(objectClass=person)',
            attrlist=['uid'],
            serverctrls=[psc]
        )

        expected = {
            "searches": 1,
            "persistent_searches": 1,
            "operations": 1,
        }

        time.sleep(2)
        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_persistant_search")


    def test_simple_bind(self):
        """Validate simple bind operation stats reported by logconv.

        :id: 20d5244a-3cda-434f-9957-062aaa88e66f
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Create a user and set password
            3. Perform a simple bind and unbind
            4. Delete user
            5. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Success
            5. Actual stats match expected
        """
        self.truncate_logs()

        users = UserAccounts(self.inst, DEFAULT_SUFFIX)
        user = users.create_test_user(uid=1, gid=1)
        user.set("userPassword", "password")

        conn = ldap.initialize("ldap://%s:%s" % (HOST_STANDALONE, PORT_STANDALONE))
        conn.simple_bind_s(user.dn, "password")
        conn.unbind_s()
        user.delete()

        expected = {
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

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_simple_bind")

    def test_anon_bind(self):
        """Validate anonymous bind operation stats reported by logconv.

        :id: 893b8388-4a6b-4434-89ce-f93e518eff9d
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Perform an anonymous bind and unbind
            3. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Actual stats match expected
        """
        self.truncate_logs()

        conn = Anonymous(self.inst).bind()
        conn.unbind()

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

        time.sleep(2)
        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_anon_bind")

    def test_ldapi(self):
        """Validate ldapi operation stats reported by logconv.

        :id: 169545bc-cd1e-477a-bd73-3d593cb1ff98
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Configure ldapi
            3. Restart instance for config to take effect
            4. Run an ldapi search
            5. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Success
            5. Actual stats match expected
        """
        self.truncate_logs()

        self.inst.config.set('nsslapd-ldapilisten', 'on')
        self.inst.config.set('nsslapd-ldapiautobind', 'on')
        self.inst.config.set('nsslapd-ldapifilepath', f'/var/run/slapd-{self.inst.serverid}.socket')
        self.inst.restart()
        ldapi_socket = self.inst.config.get_attr_val_utf8('nsslapd-ldapifilepath').replace('/', '%2F')
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

        time.sleep(2)
        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_ldapi")

    def test_restart(self):
        """Validate restart stats reported by logconv.

        :id: 1c98b6da-d37b-48f5-ae36-72c62ddccdb1
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Restart the instance twice
            3. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. SUccess
            3. Actual stats match expected
        """
        self.truncate_logs()

        self.inst.restart()
        self.inst.restart()

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

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_restart")

    def test_invalid_filter(self):
        """Validate invalid filter search stats reported by logconv.

        :id: c2d9c082-eaf5-4eba-a977-c28d3f02d51f
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Run a search with a filter that doesnt exist
            3. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. SUccess
            3. Actual stats match expected
        """
        self.truncate_logs()

        self.inst.search_ext_s(
            base=DEFAULT_SUFFIX,
            scope=ldap.SCOPE_SUBTREE,
            filterstr='(idontexist=*)',
        )

        expected = {
            "invalid_attribute_filters": 1,
            "searches": 1,
            "operations": 1,
            "results": 1
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_invalid_filter")

    def test_partially_unindexed_vlv_sort(self):
        """Validate an unindexed VLV search + sort stats reported by logconv.

        :id: 88af0fcc-d351-4ad2-8be9-8adcb0bd6bf0
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Configure VLV and SSS request controls
            3. Run an unindexed search
            4. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. SUccess
            3. SUccess
            4. Actual stats match expected
        """
        self.truncate_logs()

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

        self.inst.search_ext_s(
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

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_partially_unindexed_vlv_sort")

    def test_fully_unindexed_search(self):
        """Validate an unindexed VLV search stats reported by logconv.

        :id: 93d5a42e-ed78-4257-ba8c-f54a14d95b94
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Lower idlistscanlimit to force unindexed search
            3. Create 10 test users
            4. Perform a search triggering unindexed search
            5. Restore idlistscanlimit and delete users
            6. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Success
            5. Success
            6. Actual stats match expected
        """
        self.truncate_logs()

        # Force an unindexed search
        db_cfg = DatabaseConfig(self.inst)
        default_idlistscanlimit = db_cfg.get_attr_vals_utf8('nsslapd-idlistscanlimit')
        db_cfg.set([('nsslapd-idlistscanlimit', '100')])
        users = UserAccounts(self.inst, DEFAULT_SUFFIX)
        for i in range(10):
            users.create_test_user(uid=i)
        raw_objects = DSLdapObjects(self.inst, basedn=DEFAULT_SUFFIX)
        raw_objects.filter("(description=test*)")
        db_cfg.set([('nsslapd-idlistscanlimit', default_idlistscanlimit)])

        for dn in users.list():
            try:
                dn.delete()
            except Exception as e:
                log.warning(f"Failed to delete user: {e}")

        expected = {
            "mods": 2,
            "adds": 10,
            "deletes": 10,
            "searches": 44,
            "unindexed_components": 1,
            "operations": 66,
            "results": 66,
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_fully_unindexed_search")

    def test_starttls(self):
        """Validate an starttls operation stats reported by logconv.

        :id: 3e76cd43-0a20-4a7b-854c-5292571be8a6
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Run a search with starttls flag enabled
            3. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Actual stats match expected
        """
        self.truncate_logs()

        cmd = [
            "ldapsearch",
            "-x",
            "-H", f"ldap://localhost:{self.inst.port}",
            "-Z",
            "-D", "cn=Directory Manager",
            "-w", "password",
            "-b", DEFAULT_SUFFIX,
            "(objectclass=*)"
        ]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        expected = {
            "binds": 1,
            "unbinds": 1,
            "searches": 1,
            "entire_search_base_queries": 1,
            "unindexed_components": 1,
            "starttls_conns": 1,
            "extended_ops": 1,
            "operations": 3,
            "results": 3,
            "ldap_conns": 1,
            "total_connections": 1,
            "fds_taken": 1,
            "fds_returned": 1,
        }

        time.sleep(4)
        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_starttls")

    def test_missing_access_log(self):
        """Validate behavior when access log path is invalid.

        :id: 2ea0230b-6a12-4a41-ac7f-6c42862c9095
        :setup: Standalone Instance
        :steps:
            1. Set access log path to an invalid location
            2. Run logconv
        :expectedresults:
            1. Success
            2. RuntimeError is raised
        """
        orig_access_log_path = self.access_log_path
        self.access_log_path = "somewhereovertherainbow"
        try:
            with pytest.raises(RuntimeError):
                self.run_logconv()

        finally:
            self.access_log_path = orig_access_log_path

    def test_invalid_access_log_format(self):
        """Validate logconv handles malformed or mixed format logs.

        :id: a7fae8d1-0400-44f6-ba61-fbd04c0f0bca
        :setup: Standalone Instance
        :steps:
            1. Truncate access log
            2. Inject invalid and mixed format entries
            3. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. SUccess
            3. Actual stats match expected
        """
        self.truncate_logs()

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

        with open(self.access_log_path, "w") as f:
            for line in lines:
                f.write(line)

        expected = {
            "searches": 2,
            "adds": 1,
            "modrdns": 1,
            "operations": 4,
            "results": 4,
            "invalid_attribute_filters": 2
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_invalid_access_log_format")

    def test_ldaps(self):
        """Validate LDAPS and certificate-based bind stats reported by logconv.

        :id: e14e5b57-87fe-4b72-b787-b987a62b8ee9
        :setup: Standalone Instance with TLS enabled
        :steps:
            1. Truncate access log
            2. Enable TLS and configure certificate-based binds
            3. Perform SASL EXTERNAL bind with correct and incorrect certs
            4. Run logconv and compare stats
        :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Actual stats match expected
        """
        self.truncate_logs()

        RDN_TEST_USER = 'testuser'
        RDN_TEST_USER_WRONG = 'testuser_wrong'
        self.inst.enable_tls()
        self.inst.restart()

        users = UserAccounts(self.inst, DEFAULT_SUFFIX)
        user = users.create(properties={
            'uid': RDN_TEST_USER,
            'cn': RDN_TEST_USER,
            'sn': RDN_TEST_USER,
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': f'/home/{RDN_TEST_USER}'
        })

        ssca_dir = self.inst.get_ssca_dir()
        ssca = NssSsl(dbpath=ssca_dir)
        ssca.create_rsa_user(RDN_TEST_USER)
        ssca.create_rsa_user(RDN_TEST_USER_WRONG)

        # Get the details of where the key and crt are.
        tls_locs = ssca.get_rsa_user(RDN_TEST_USER)
        tls_locs_wrong = ssca.get_rsa_user(RDN_TEST_USER_WRONG)

        user.enroll_certificate(tls_locs['crt_der_path'])

        # Turn on the certmap.
        cm = CertmapLegacy(self.inst)
        certmaps = cm.list()
        certmaps['default']['DNComps'] = ''
        certmaps['default']['FilterComps'] = ['cn']
        certmaps['default']['VerifyCert'] = 'off'
        cm.set(certmaps)

        # Check that EXTERNAL is listed in supported mechns.
        assert (self.inst.rootdse.supports_sasl_external())

        # Restart to allow certmaps to be re-read: Note, we CAN NOT use post_open
        # here, it breaks on auth. see lib389/__init__.py
        self.inst.restart(post_open=False)

        # Attempt a bind with TLS external
        self.inst.open(saslmethod='EXTERNAL', connOnly=True, certdir=ssca_dir,
                userkey=tls_locs['key'], usercert=tls_locs['crt'])

        self.inst.restart()

        # Check for failed certmap error
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            self.inst.open(saslmethod='EXTERNAL', connOnly=True, certdir=ssca_dir,
                    userkey=tls_locs_wrong['key'], usercert=tls_locs_wrong['crt'])

        expected = {
            "mods": 2,
            "adds": 1,
            "binds": 5,
            "sasl_binds": 2,
            "searches": 6,
            "operations": 14,
            "results": 14,
            "unbinds": 1,
            "ldaps_conns": 5,
            "ssl_client_binds": 1,
            "ssl_client_bind_failed": 1,
            "total_connections": 5,
            "fds_taken": 5,
            "fds_returned": 5,
            "restarts": 4
        }

        output = self.run_logconv()
        logconv_stats = self.extract_logconv_stats(output)
        assert self.validate_logconv_stats(expected, logconv_stats, "test_ldaps")

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

