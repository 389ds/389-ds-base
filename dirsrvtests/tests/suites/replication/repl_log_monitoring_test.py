# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import time
import shutil
import json
import pytest
import logging
import tempfile
from datetime import datetime, timezone

from lib389.tasks import *
from lib389.utils import *
from lib389.backend import Backends
from lib389.topologies import topology_m4 as topo_m4
from lib389.idm.user import UserAccount
from lib389.replica import ReplicationManager
from lib389.repltools import ReplicationLogAnalyzer
from lib389._constants import *

try:
    import plotly
    import matplotlib
    HTML_PNG_REPORTS_AVAILABLE = True
except ImportError:
    HTML_PNG_REPORTS_AVAILABLE = False

pytestmark = pytest.mark.tier0

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _generate_test_data(supplier, suffix, count, user_prefix="test_user"):
    """Generate test users and modifications"""
    test_users = []
    for i in range(count):
        user_dn = f'uid={user_prefix}_{i},{suffix}'
        test_user = UserAccount(supplier, user_dn)
        test_user.create(properties={
            'uid': f'{user_prefix}_{i}',
            'cn': f'Test User {i}',
            'sn': f'User{i}',
            'userPassword': 'password',
            'uidNumber': str(1000 + i),
            'gidNumber': '2000',
            'homeDirectory': f'/home/{user_prefix}_{i}'
        })

        # Generate modifications
        for j in range(3):
            test_user.add('description', f'Description {j}')
        test_user.replace('cn', f'Modified User {test_user.get_attr_val("uid")}')
        for j in range(3):
            try:
                test_user.remove('description', f'Description {j}')
            except Exception:
                pass

        test_users.append(test_user)

    return test_users


def _cleanup_test_data(test_users, tmp_dir):
    """Clean up test users and temporary directory"""
    for user in test_users:
        try:
            if user.exists():
                user.delete()
        except Exception as e:
            log.warning(f"Error cleaning up test user: {e}")

    try:
        shutil.rmtree(tmp_dir, ignore_errors=True)
    except Exception as e:
        log.error(f"Error cleaning up temporary directory: {e}")


def _cleanup_multi_suffix_test(test_users_by_suffix, tmp_dir, suppliers, extra_suffixes):
    """Clean up multi-suffix test data"""
    for users in test_users_by_suffix.values():
        for user in users:
            try:
                if user.exists():
                    user.delete()
            except Exception as e:
                log.warning(f"Error cleaning up test user: {e}")

    # Remove extra backends
    for suffix in extra_suffixes:
        for supplier in suppliers:
            try:
                backends = Backends(supplier)
                backends.get(suffix).delete()
            except Exception as e:
                log.warning(f"Error removing backend for {suffix}: {e}")

    try:
        shutil.rmtree(tmp_dir, ignore_errors=True)
    except Exception as e:
        log.error(f"Error cleaning up temporary directory: {e}")


@pytest.mark.skipif(not HTML_PNG_REPORTS_AVAILABLE, reason="HTML/PNG report libraries not available")
def test_replication_log_monitoring_basic(topo_m4):
    """Test basic replication log monitoring functionality

    :id: e62ed58b-1acd-4e7d-9cfd-948ded4cede8
    :setup: Four suppliers replication setup
    :steps:
        1. Create test data with known replication patterns
        2. Configure log monitoring with basic options
        3. Generate and verify reports
        4. Validate report contents
    :expectedresults:
        1. Test data should be properly replicated
        2. Reports should be generated successfully
        3. Report contents should match expected patterns
        4. Reports should contain expected data and statistics
    """
    tmp_dir = tempfile.mkdtemp(prefix='repl_analysis_')
    test_users = []
    suppliers = [topo_m4.ms[f"supplier{i}"] for i in range(1, 5)]

    try:
        # Clear logs and restart servers
        for supplier in suppliers:
            supplier.deleteAccessLogs(restart=True)

        # Generate test data with known patterns
        log.info('Creating test data...')
        test_users = _generate_test_data(suppliers[0], DEFAULT_SUFFIX, 10)

        # Wait for replication
        repl = ReplicationManager(DEFAULT_SUFFIX)
        for s1 in suppliers:
            for s2 in suppliers:
                if s1 != s2:
                    repl.wait_for_replication(s1, s2)

        # Restart to flush logs
        for supplier in suppliers:
            supplier.restart()

        # Configure monitoring
        log_dirs = [s.ds_paths.log_dir for s in suppliers]
        repl_monitor = ReplicationLogAnalyzer(
            log_dirs=log_dirs,
            suffixes=[DEFAULT_SUFFIX],
            anonymous=False,
            only_fully_replicated=True
        )

        # Parse logs and generate reports
        repl_monitor.parse_logs()
        generated_files = repl_monitor.generate_report(
            output_dir=tmp_dir,
            formats=['csv', 'html', 'json'],
            report_name='basic_test'
        )

        # Verify report files exist and have content
        for fmt in ['csv', 'html', 'summary']:
            assert os.path.exists(generated_files[fmt])
            assert os.path.getsize(generated_files[fmt]) > 0

        # Verify CSV content
        with open(generated_files['csv'], 'r') as f:
            csv_content = f.read()
            # Verify headers
            assert 'Timestamp,Server,CSN,Suffix' in csv_content
            # Verify all servers present
            for supplier in suppliers:
                assert supplier.serverid in csv_content
            # Verify suffix
            assert DEFAULT_SUFFIX in csv_content

        # Verify JSON summary
        with open(generated_files['summary'], 'r') as f:
            summary = json.load(f)
            assert 'analysis_summary' in summary
            stats = summary['analysis_summary']

            # Verify basic stats
            assert stats['total_servers'] == len(suppliers)
            assert stats['total_updates'] > 0
            assert stats['updates_by_suffix'][DEFAULT_SUFFIX] > 0
            assert 'average_lag' in stats
            assert 'maximum_lag' in stats

    finally:
        _cleanup_test_data(test_users, tmp_dir)


@pytest.mark.skipif(not HTML_PNG_REPORTS_AVAILABLE, reason="HTML/PNG report libraries not available")
def test_replication_log_monitoring_advanced(topo_m4):
    """Test advanced replication monitoring features

    :id: 5bb8fd9f-c3ed-4118-a2f9-fd5d733230c7
    :setup: Four suppliers replication setup
    :steps:
        1. Test filtering options
        2. Test time range filtering
        3. Test anonymization
        4. Verify lag calculations
    :expectedresults:
        1. Filtering should work as expected
        2. Time range filtering should limit results
        3. Anonymization should hide server names
        4. Lag calculations should be accurate
    """
    tmp_dir = tempfile.mkdtemp(prefix='repl_analysis_')
    test_users = []
    suppliers = [topo_m4.ms[f"supplier{i}"] for i in range(1, 5)]

    try:
        # Clear logs and restart servers
        for supplier in suppliers:
            supplier.deleteAccessLogs(restart=True)

        # Generate test data
        start_time = datetime.now(timezone.utc)
        test_users = _generate_test_data(suppliers[0], DEFAULT_SUFFIX, 20)

        # Force some lag by delaying operations
        time.sleep(2)
        for user in test_users[10:]:
            user.replace('description', 'Modified after delay')

        # Wait for replication
        repl = ReplicationManager(DEFAULT_SUFFIX)
        for s1 in suppliers:
            for s2 in suppliers:
                if s1 != s2:
                    repl.wait_for_replication(s1, s2)

        end_time = datetime.now(timezone.utc)

        # Restart to flush logs
        for supplier in suppliers:
            supplier.restart()

        log_dirs = [s.ds_paths.log_dir for s in suppliers]

        # Test 1: Lag time filtering
        repl_monitor = ReplicationLogAnalyzer(
            log_dirs=log_dirs,
            suffixes=[DEFAULT_SUFFIX],
            lag_time_lowest=1.0
        )
        repl_monitor.parse_logs()
        results1 = repl_monitor.build_result()

        # Verify lag filtering:
        # Only consider dict values, skip the special "__hop_lags__" (if present)
        for csn, server_map in results1['lag'].items():
            t_list = [
                record['logtime']
                for key, record in server_map.items()
                if isinstance(record, dict) and key != '__hop_lags__'
            ]
            if not t_list:
                # If no normal records exist, just skip
                continue

            lag_time = max(t_list) - min(t_list)
            # Must be strictly > 1.0
            assert lag_time > 1.0, f"Expected lag_time > 1.0, got {lag_time}"

        # Test 2: Time range filtering
        repl_monitor = ReplicationLogAnalyzer(
            log_dirs=log_dirs,
            suffixes=[DEFAULT_SUFFIX],
            time_range={'start': start_time, 'end': end_time}
        )
        repl_monitor.parse_logs()
        results2 = repl_monitor.build_result()

        # Verify the 'start-time' in results is within or after our start_time
        utc_start_time = datetime.fromtimestamp(results2['utc-start-time'], timezone.utc)
        assert utc_start_time >= start_time, (
            f"Expected start time >= {start_time}, got {utc_start_time}"
        )

        # Test 3: Anonymization
        repl_monitor = ReplicationLogAnalyzer(
            log_dirs=log_dirs,
            suffixes=[DEFAULT_SUFFIX],
            anonymous=True
        )
        repl_monitor.parse_logs()
        generated_files = repl_monitor.generate_report(
            output_dir=tmp_dir,
            formats=['csv'],
            report_name='anon_test'
        )

        # Verify anonymization
        with open(generated_files['csv'], 'r') as f:
            content = f.read()
            for supplier in suppliers:
                # Original supplier.serverid should NOT appear
                assert supplier.serverid not in content, (
                    f"Found real server name {supplier.serverid} in CSV"
                )
            # Instead, placeholders like 'server_0' should exist
            assert 'server_0' in content, "Expected 'server_0' placeholder not found in CSV"

    finally:
        _cleanup_test_data(test_users, tmp_dir)


@pytest.mark.skipif(not HTML_PNG_REPORTS_AVAILABLE, reason="HTML/PNG report libraries not available")
def test_replication_log_monitoring_multi_suffix(topo_m4):
    """Test multi-suffix replication monitoring

    :id: 6ef38c42-4961-476f-9e72-488d99211b8b
    :setup: Four suppliers replication setup
    :steps:
        1. Create multiple suffixes with different replication patterns
        2. Generate reports for all suffixes
        3. Verify suffix-specific statistics
    :expectedresults:
        1. All suffixes should be monitored
        2. Reports should show correct per-suffix data
        3. Statistics should be accurate for each suffix
    """
    tmp_dir = tempfile.mkdtemp(prefix='multi_suffix_repl_')
    SUFFIX_2 = "dc=test2"
    SUFFIX_3 = "dc=test3"
    all_suffixes = [DEFAULT_SUFFIX, SUFFIX_2, SUFFIX_3]
    test_users_by_suffix = {suffix: [] for suffix in all_suffixes}
    suppliers = [topo_m4.ms[f"supplier{i}"] for i in range(1, 5)]

    try:


        # Setup additional suffixes
        for suffix in [SUFFIX_2, SUFFIX_3]:
            repl = ReplicationManager(suffix)
            for supplier in suppliers:
                props = {
                    'cn': f'userRoot_{suffix.split(",")[0][3:]}',
                    'nsslapd-suffix': suffix
                }
                backends = Backends(supplier)
                be = backends.create(properties=props)
                be.create_sample_entries('001004002')

                if supplier == suppliers[0]:
                    repl.create_first_supplier(supplier)
                else:
                    repl.join_supplier(suppliers[0], supplier)

        # Create full mesh
        for suffix in all_suffixes:
            repl = ReplicationManager(suffix)
            for i, s1 in enumerate(suppliers):
                for s2 in suppliers[i+1:]:
                    repl.ensure_agreement(s1, s2)
                    repl.ensure_agreement(s2, s1)

        # Wait for all the setup replication to settle, then clear the logs
        for suffix in all_suffixes:
            repl = ReplicationManager(suffix)
            for s1 in suppliers:
                for s2 in suppliers:
                    if s1 != s2:
                        repl.wait_for_replication(s1, s2)
        for supplier in suppliers:
            supplier.deleteAccessLogs(restart=True)

        # Generate different amounts of test data per suffix
        test_users_by_suffix[DEFAULT_SUFFIX] = _generate_test_data(
            suppliers[0], DEFAULT_SUFFIX, 10
        )
        test_users_by_suffix[SUFFIX_2] = _generate_test_data(
            suppliers[0], SUFFIX_2, 5, user_prefix="test2_user"
        )
        test_users_by_suffix[SUFFIX_3] = _generate_test_data(
            suppliers[0], SUFFIX_3, 15, user_prefix="test3_user"
        )

        # Wait for replication
        for suffix in all_suffixes:
            repl = ReplicationManager(suffix)
            for s1 in suppliers:
                for s2 in suppliers:
                    if s1 != s2:
                        repl.wait_for_replication(s1, s2)

        # Restart to flush logs
        for supplier in suppliers:
            supplier.restart()

        # Monitor all suffixes
        log_dirs = [s.ds_paths.log_dir for s in suppliers]
        repl_monitor = ReplicationLogAnalyzer(
            log_dirs=log_dirs,
            suffixes=all_suffixes
        )

        repl_monitor.parse_logs()
        generated_files = repl_monitor.generate_report(
            output_dir=tmp_dir,
            formats=['csv', 'html'],
            report_name='multi_suffix_test'
        )

        # Verify summary statistics
        with open(generated_files['summary'], 'r') as f:
            summary = json.load(f)
            stats = summary['analysis_summary']

            # Verify updates by suffix
            updates = stats['updates_by_suffix']
            assert len(updates) == len(all_suffixes)
            for suffix in all_suffixes:
                assert suffix in updates
                assert updates[suffix] > 0

            # Verify relative amounts
            assert updates[SUFFIX_3] > updates[DEFAULT_SUFFIX]
            assert updates[DEFAULT_SUFFIX] > updates[SUFFIX_2]

    finally:
        _cleanup_multi_suffix_test(
            test_users_by_suffix,
            tmp_dir,
            suppliers,
            [SUFFIX_2, SUFFIX_3]
        )


@pytest.mark.skipif(not HTML_PNG_REPORTS_AVAILABLE, reason="HTML/PNG report libraries not available")
def test_replication_log_monitoring_filter_combinations(topo_m4):
    """Test complex combinations of filtering options and interactions

    :id: 103fc0ac-f0b8-48f1-8cdf-1f6ff57f9672
    :setup: Four suppliers replication setup
    :steps:
        1. Test multiple concurrent filters
        2. Test filter interactions
        3. Verify filter precedence
    :expectedresults:
        1. Multiple filters should work together correctly
        2. Filter interactions should be predictable
        3. Results should respect all applied filters
    """
    tmp_dir = tempfile.mkdtemp(prefix='repl_filter_test_')
    test_users = []
    suppliers = [topo_m4.ms[f"supplier{i}"] for i in range(1, 5)]

    try:
        # Clear logs and restart servers
        for supplier in suppliers:
            supplier.deleteAccessLogs(restart=True)

        # Generate varied test data
        start_time = datetime.now(timezone.utc)
        test_users = _generate_test_data(suppliers[0], DEFAULT_SUFFIX, 30)

        # Create different lag patterns
        for i, user in enumerate(test_users):
            if i % 3 == 0:
                time.sleep(0.5)  # Short lag
            elif i % 3 == 1:
                time.sleep(1.5)  # Medium lag
            user.replace('description', f'Modified with lag pattern {i}')

        # Wait for replication
        repl = ReplicationManager(DEFAULT_SUFFIX)
        for s1 in suppliers:
            for s2 in suppliers:
                if s1 != s2:
                    repl.wait_for_replication(s1, s2)

        end_time = datetime.now(timezone.utc)

        # Restart to flush logs
        for supplier in suppliers:
            supplier.restart()

        log_dirs = [s.ds_paths.log_dir for s in suppliers]

        # Test combined filters
        repl_monitor = ReplicationLogAnalyzer(
            log_dirs=log_dirs,
            suffixes=[DEFAULT_SUFFIX],
            lag_time_lowest=1.0,
            etime_lowest=0.1,
            only_fully_replicated=True,
            time_range={'start': start_time, 'end': end_time}
        )

        repl_monitor.parse_logs()
        results = repl_monitor.build_result()

        # Verify filter combinations
        for csn, server_map in results['lag'].items():
            t_list = [
                record['logtime']
                for key, record in server_map.items()
                if isinstance(record, dict) and key != '__hop_lags__'
            ]
            if not t_list:
                continue

            lag_time = max(t_list) - min(t_list)

            # Verify all filters were applied
            assert lag_time > 1.0, "Lag time filter not applied"
            assert len(t_list) == len(suppliers), "Not fully replicated"

            # Verify time range
            for t in t_list:
                dt = datetime.fromtimestamp(t, timezone.utc)
                assert start_time <= dt <= end_time, "Time range filter violated"
    finally:
        _cleanup_test_data(test_users, tmp_dir)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
