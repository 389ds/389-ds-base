# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import subprocess
import re
import time
import ldap
import pytest
import logging
from lib389.tasks import *
from lib389._constants import *
from lib389.utils import ensure_bytes
from lib389.backend import Backends
from lib389.topologies import topology_st as topo
from lib389.paths import *
from lib389.idm.user import UserAccounts

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier2
disk_monitoring_ack = pytest.mark.skipif(not os.environ.get('DISK_MONITORING_ACK', False), reason="Disk monitoring tests may damage system configuration.")

THRESHOLD_BYTES = 30000000


def presetup(inst):
    """Presetup function to mount a tmpfs for log directory to simulate disk space limits."""

    log.info("Setting up tmpfs for disk monitoring tests")
    inst.stop()
    log_dir = inst.ds_paths.log_dir

    if os.path.exists(log_dir):
        log.debug(f"Mounting tmpfs on existing directory: {log_dir}")
        subprocess.call(['mount', '-t', 'tmpfs', '-o', 'size=35M', 'tmpfs', log_dir])
    else:
        log.debug(f"Creating and mounting tmpfs on new directory: {log_dir}")
        os.mkdir(log_dir)
        subprocess.call(['mount', '-t', 'tmpfs', '-o', 'size=35M', 'tmpfs', log_dir])

    subprocess.call(f'chown {DEFAULT_USER}: -R {log_dir}', shell=True)
    subprocess.call(f'chown {DEFAULT_USER}: -R {log_dir}/*', shell=True)
    subprocess.call(f'restorecon -FvvR {log_dir}', shell=True)
    inst.start()
    log.info("tmpfs setup completed")


def setupthesystem(inst):
    """Setup system configuration for disk monitoring tests."""

    log.info("Configuring system for disk monitoring tests")
    inst.start()
    inst.config.set('nsslapd-disk-monitoring-grace-period', '1')
    inst.config.set('nsslapd-accesslog-logbuffering', 'off')
    inst.config.set('nsslapd-disk-monitoring-threshold', ensure_bytes(str(THRESHOLD_BYTES)))
    inst.restart()
    log.info("System configuration completed")


def capture_config(inst):
    """Capture current configuration values for later restoration."""

    log.info("Capturing current configuration values")

    config_attrs = [
        'nsslapd-disk-monitoring',
        'nsslapd-disk-monitoring-threshold',
        'nsslapd-disk-monitoring-grace-period',
        'nsslapd-disk-monitoring-logging-critical',
        'nsslapd-disk-monitoring-readonly-on-threshold',
        'nsslapd-accesslog-logbuffering',
        'nsslapd-errorlog-level',
        'nsslapd-accesslog-logging-enabled',
        'nsslapd-accesslog-maxlogsize',
        'nsslapd-accesslog-logrotationtimeunit',
        'nsslapd-accesslog-level',
        'nsslapd-external-libs-debug-enabled',
        'nsslapd-errorlog-logging-enabled'
    ]

    captured_config = {}
    for config_attr in config_attrs:
        try:
            current_value = inst.config.get_attr_val_utf8(config_attr)
            captured_config[config_attr] = current_value
            log.debug(f"Captured {config_attr}: {current_value}")
        except Exception as e:
            log.debug(f"Could not capture {config_attr}: {e}")
            captured_config[config_attr] = None

    log.info("Configuration capture completed")
    return captured_config


def restore_config(inst, captured_config):
    """Restore configuration values to previously captured state."""

    log.info("Restoring configuration to captured values")

    for config_attr, original_value in captured_config.items():
        if original_value is not None:
            try:
                current_value = inst.config.get_attr_val_utf8(config_attr)
                if current_value != original_value:
                    log.debug(f"Restoring {config_attr} from '{current_value}' to '{original_value}'")
                    inst.config.set(config_attr, ensure_bytes(original_value))
            except Exception as e:
                log.debug(f"Could not restore {config_attr}: {e}")

    log.info("Configuration restoration completed")


@pytest.fixture(scope="function")
def setup(request, topo):
    """Module-level fixture to setup the test environment."""

    log.info("Starting module setup for disk monitoring tests")
    inst = topo.standalone

    # Capture current configuration before making any changes
    original_config = capture_config(inst)

    presetup(inst)
    setupthesystem(inst)

    def fin():
        log.info("Running module cleanup for disk monitoring tests")
        inst.stop()
        subprocess.call(['umount', '-fl', inst.ds_paths.log_dir])
        # Restore configuration to original values
        inst.start()
        restore_config(inst, original_config)
        log.info("Module cleanup completed")

    request.addfinalizer(fin)


def wait_for_condition(inst, condition_str, timeout=30):
    """Wait until the given condition evaluates to False."""

    log.debug(f"Waiting for condition to be False: {condition_str} (timeout: {timeout}s)")
    start_time = time.time()
    while time.time() - start_time < timeout:
        if not eval(condition_str):
            log.debug(f"Condition satisfied after {time.time() - start_time:.2f}s")
            return
        time.sleep(1)
    raise AssertionError(f"Condition '{condition_str}' still True after {timeout} seconds")


def wait_for_log_entry(inst, message, timeout=30):
    """Wait for a specific message to appear in the error log."""

    log.debug(f"Waiting for log entry: '{message}' (timeout: {timeout}s)")
    start_time = time.time()
    while time.time() - start_time < timeout:
        with open(inst.errlog, 'r') as log_file:
            if message in log_file.read():
                log.debug(f"Found log entry after {time.time() - start_time:.2f}s")
                return
        time.sleep(1)
    raise AssertionError(f"Message '{message}' not found in error log after {timeout} seconds")


def get_avail_bytes(path):
    """Get available bytes on the filesystem at the given path."""

    stat = os.statvfs(path)
    return stat.f_bavail * stat.f_bsize


def fill_to_target_avail(path, target_avail_bytes):
    """Fill the disk to reach the target available bytes by creating a large file."""

    avail = get_avail_bytes(path)
    fill_bytes = avail - target_avail_bytes
    log.debug(f"Current available: {avail}, target: {target_avail_bytes}, will create {fill_bytes} byte file")
    if fill_bytes <= 0:
        raise ValueError("Already below target avail")

    fill_file = os.path.join(path, 'fill.dd')
    bs = 4096
    count = (fill_bytes + bs - 1) // bs  # ceil division to ensure enough
    log.info(f"Creating fill file {fill_file} with {count} blocks of {bs} bytes")
    subprocess.check_call(['dd', 'if=/dev/zero', f'of={fill_file}', f'bs={bs}', f'count={count}'])
    return fill_file


@pytest.fixture(scope="function")
def reset_logs(topo):
    """Function-level fixture to reset the error log before each test."""

    log.debug("Resetting error logs before test")
    topo.standalone.deleteErrorLogs()


def generate_access_log_activity(inst, num_users=10, num_binds=100):
    """Generate access log activity by creating users and performing binds."""

    log.info(f"Generating access log activity with {num_users} users and {num_binds} binds each")
    users = UserAccounts(inst, DEFAULT_SUFFIX)

    # Create test users
    for i in range(num_users):
        user_properties = {
            'uid': f'cn=user{i}',
            'cn': f'cn=user{i}',
            'sn': f'cn=user{i}',
            'userPassword': "Itsme123",
            'uidNumber': f'1{i}',
            'gidNumber': f'2{i}',
            'homeDirectory': f'/home/{i}'
        }
        users.create(properties=user_properties)

    # Perform bind operations
    for j in range(num_binds):
        for user in users.list():
            user.bind('Itsme123')

    log.info("Access log activity generation completed")
    return users


@disk_monitoring_ack
def test_verify_operation_when_disk_monitoring_is_off(topo, setup, reset_logs):
    """Verify operation when Disk monitoring is off.

    :id: 73a97536-fe9e-11e8-ba9f-8c16451d917b
    :setup: Standalone
    :steps:
        1. Turn off disk monitoring
        2. Go below the threshold
        3. Check DS is up and not entering shutdown mode
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    log.info("Starting test_verify_operation_when_disk_monitoring_is_off")
    inst = topo.standalone
    fill_file = None

    try:
        log.info("Disabling disk monitoring")
        inst.config.set('nsslapd-disk-monitoring', 'off')
        inst.restart()

        log.info(f"Filling disk to go below threshold ({THRESHOLD_BYTES} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES - 1)

        log.info("Verifying server stays up despite being below threshold")
        wait_for_condition(inst, 'inst.status() != True', 11)

        # Check DS is up and not entering shutdown mode
        assert inst.status() == True
        log.info("Verified: server remains operational when disk monitoring is disabled")

    finally:
        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_enable_external_libs_debug_log(topo, setup, reset_logs):
    """Check that OpenLDAP logs are successfully enabled and disabled when disk threshold is reached.

    :id: 121b2b24-ecba-48e2-9ee2-312d929dc8c6
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-external-libs-debug-enabled to "on"
        2. Go straight below 1/2 of the threshold
        3. Verify that the external libs debug setting is disabled
        4. Go back above 1/2 of the threshold
        5. Verify that the external libs debug setting is enabled back
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    log.info("Starting test_enable_external_libs_debug_log")
    inst = topo.standalone
    fill_file = None

    try:
        log.info("Configuring disk monitoring and external libs debug")
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
        inst.config.set('nsslapd-external-libs-debug-enabled', 'on')
        inst.config.set('nsslapd-errorlog-level', '8')
        inst.restart()

        log.info(f"Filling disk to go below half threshold ({THRESHOLD_BYTES // 2} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES // 2 - 1)

        log.info("Verifying external libs debug is automatically disabled")
        wait_for_condition(inst, "inst.config.get_attr_val_utf8('nsslapd-external-libs-debug-enabled') != 'off'", 31)

    finally:
        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

        log.info("Verifying external libs debug is re-enabled after freeing space")
        wait_for_condition(inst, "inst.config.get_attr_val_utf8('nsslapd-external-libs-debug-enabled') != 'on'", 31)
        inst.config.set('nsslapd-external-libs-debug-enabled', 'off')

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_free_up_the_disk_space_and_change_ds_config(topo, setup, reset_logs):
    """Free up the disk space and change DS config.

    :id: 7be4d560-fe9e-11e8-a307-8c16451d917b
    :setup: Standalone
    :steps:
        1. Enable Disk Monitoring plugin and set disk monitoring logging to critical
        2. Verify no message about loglevel is present in the error log
        3. Verify no message about disabling logging is present in the error log
        4. Verify no message about removing rotated logs is present in the error log
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    log.info("Starting test_free_up_the_disk_space_and_change_ds_config")
    inst = topo.standalone

    log.info("Enabling disk monitoring with critical logging")
    inst.config.set('nsslapd-disk-monitoring', 'on')
    inst.config.set('nsslapd-disk-monitoring-logging-critical', 'on')
    inst.config.set('nsslapd-errorlog-level', '8')
    inst.restart()

    log.info("Verifying no premature disk monitoring messages in error log")
    with open(inst.errlog, 'r') as err_log:
        content = err_log.read()

    assert 'temporarily setting error loglevel to zero' not in content
    assert 'disabling access and audit logging' not in content
    assert 'deleting rotated logs' not in content

    log.info("Verified: no unexpected disk monitoring messages found")
    log.info("Test completed successfully")


@disk_monitoring_ack
def test_verify_operation_with_nsslapd_disk_monitoring_logging_critical_off(topo, setup, reset_logs):
    """Verify operation with "nsslapd-disk-monitoring-logging-critical: off".

    :id: 82363bca-fe9e-11e8-9ae7-8c16451d917b
    :setup: Standalone
    :steps:
        1. Verify that verbose logging was set to default level
        2. Verify that logging is disabled
        3. Verify that rotated logs were not removed
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    log.info("Starting test_verify_operation_with_nsslapd_disk_monitoring_logging_critical_off")
    inst = topo.standalone
    fill_file = None

    try:
        log.info("Configuring disk monitoring with critical logging disabled")
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
        inst.config.set('nsslapd-errorlog-level', '8')
        inst.restart()

        log.info(f"Filling disk to go below threshold ({THRESHOLD_BYTES} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES - 1)

        log.info("Waiting for loglevel to be set to default")
        wait_for_log_entry(inst, 'temporarily setting error loglevel to the default level', 11)

        log.info("Verifying error log level was set to default")
        config_entry = inst.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectclass=*)', ['nsslapd-errorlog-level'])
        current_level = int(re.findall(r'nsslapd-errorlog-level: \d+', str(config_entry))[0].split(' ')[1])
        assert LOG_DEFAULT == current_level

        log.info("Verifying access logging is disabled")
        wait_for_condition(inst, "inst.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') != 'off'", 11)
        assert inst.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') == 'off'

        log.info("Verifying expected disk monitoring messages")
        with open(inst.errlog, 'r') as err_log:
            content = err_log.read()

        assert 'disabling access and audit logging' in content
        wait_for_log_entry(inst, 'deleting rotated logs', 11)
        assert f"Unable to remove file: {inst.ds_paths.log_dir}" not in content
        assert 'is too far below the threshold' not in content

        log.info("All verifications passed")

    finally:
        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_operation_with_nsslapd_disk_monitoring_logging_critical_on_below_half_of_the_threshold(topo, setup, reset_logs):
    """Verify operation with "nsslapd-disk-monitoring-logging-critical: on" below 1/2 of the threshold.
    Verify recovery.

    :id: 8940c502-fe9e-11e8-bcc0-8c16451d917b
    :setup: Standalone
    :steps:
        1. Verify that DS goes into shutdown mode
        2. Verify that DS exited shutdown mode
    :expectedresults:
        1. Success
        2. Success
    """
    log.info("Starting test_operation_with_nsslapd_disk_monitoring_logging_critical_on_below_half_of_the_threshold")
    inst = topo.standalone
    fill_file = None

    try:
        log.info("Configuring disk monitoring with critical logging enabled")
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.config.set('nsslapd-disk-monitoring-logging-critical', 'on')
        inst.restart()

        log.info(f"Filling disk to go below half threshold ({THRESHOLD_BYTES // 2} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES // 2 - 1)

        log.info("Waiting for shutdown mode message")
        wait_for_log_entry(inst, 'is too far below the threshold', 100)

        log.info("Freeing up disk space")
        os.remove(fill_file)
        fill_file = None

        log.info("Waiting for recovery message")
        wait_for_log_entry(inst, 'Available disk space is now acceptable', 25)

        log.info("Verified: server entered and exited shutdown mode correctly")

    finally:
        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_setting_nsslapd_disk_monitoring_logging_critical_to_off(topo, setup, reset_logs):
    """Setting nsslapd-disk-monitoring-logging-critical to "off".

    :id: 93265ec4-fe9e-11e8-af93-8c16451d917b
    :setup: Standalone
    :steps:
        1. Set nsslapd-disk-monitoring-logging-critical to "off"
    :expectedresults:
        1. Success
    """
    log.info("Starting test_setting_nsslapd_disk_monitoring_logging_critical_to_off")
    inst = topo.standalone

    log.info("Setting disk monitoring configuration")
    inst.config.set('nsslapd-disk-monitoring', 'on')
    inst.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
    inst.config.set('nsslapd-errorlog-level', '8')
    inst.restart()

    log.info("Verifying server is running normally")
    assert inst.status() == True

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_operation_with_nsslapd_disk_monitoring_logging_critical_off(topo, setup, reset_logs):
    """Verify operation with nsslapd-disk-monitoring-logging-critical: off.

    :id: 97985a52-fe9e-11e8-9914-8c16451d917b
    :setup: Standalone
    :steps:
        1. Generate access log activity to create rotated logs
        2. Go below threshold to trigger disk monitoring
        3. Verify that verbose logging was set to default level
        4. Verify that logging is disabled
        5. Verify that rotated logs were removed
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    log.info("Starting test_operation_with_nsslapd_disk_monitoring_logging_critical_off")
    inst = topo.standalone
    fill_file = None
    users = None

    try:
        log.info("Configuring disk monitoring and access log settings")
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
        inst.config.set('nsslapd-errorlog-level', '8')
        inst.config.set('nsslapd-accesslog-maxlogsize', '1')
        inst.config.set('nsslapd-accesslog-logrotationtimeunit', 'minute')
        inst.config.set('nsslapd-accesslog-level', '772')
        inst.restart()

        log.info("Generating access log activity to create rotated logs")
        users = generate_access_log_activity(inst, num_users=10, num_binds=100)

        inst.bind_s(DN_DM, PW_DM)

        log.info("Resetting access log settings")
        inst.config.set('nsslapd-accesslog-maxlogsize', '100')
        inst.config.set('nsslapd-accesslog-logrotationtimeunit', 'day')
        inst.config.set('nsslapd-accesslog-level', '256')
        inst.restart()

        log.info(f"Filling disk to go below threshold ({THRESHOLD_BYTES} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES - 1)

        log.info("Waiting for loglevel to be set to default")
        wait_for_log_entry(inst, 'temporarily setting error loglevel to the default level', 11)

        log.info("Verifying error log level was set to default")
        config_level = None
        for _ in range(10):
            time.sleep(1)
            config_entry = inst.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectclass=*)', ['nsslapd-errorlog-level'])
            config_level = int(re.findall(r'nsslapd-errorlog-level: \d+', str(config_entry))[0].split(' ')[1])
            if LOG_DEFAULT == config_level:
                break
        assert LOG_DEFAULT == config_level

        log.info("Verifying access logging is disabled")
        wait_for_condition(inst, "inst.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') == 'off'", 20)

        with open(inst.errlog, 'r') as err_log:
            content = err_log.read()
        assert 'disabling access and audit logging' in content

        log.info("Verifying rotated logs are removed")
        wait_for_log_entry(inst, 'deleting rotated logs', 20)

        rotated_logs = re.findall(r'access.\d+-\d+', str(os.listdir(inst.ds_paths.log_dir)))
        assert not rotated_logs, f"Found unexpected rotated logs: {rotated_logs}"

        with open(inst.errlog, 'r') as err_log:
            content = err_log.read()
        assert 'Unable to remove file:' not in content
        assert 'is too far below the threshold' not in content

        log.info("All verifications passed")

    finally:
        # Clean up users
        if users:
            log.debug("Cleaning up test users")
            for user in users.list():
                try:
                    user.delete()
                except ldap.ALREADY_EXISTS:
                    pass

        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_operation_with_nsslapd_disk_monitoring_logging_critical_off_below_half_of_the_threshold(topo, setup, reset_logs):
    """Verify operation with nsslapd-disk-monitoring-logging-critical: off below 1/2 of the threshold.
    Verify shutdown and recovery.

    :id: 9d4c7d48-fe9e-11e8-b5d6-8c16451d917b
    :setup: Standalone
    :steps:
        1. Go below half threshold to trigger shutdown
        2. Verify DS shutdown after grace period
        3. Free space and restart
        4. Verify logging is re-enabled
        5. Create rotated logs and enable verbose logging
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    log.info("Starting test_operation_with_nsslapd_disk_monitoring_logging_critical_off_below_half_of_the_threshold")
    inst = topo.standalone
    fill_file = None
    users = None

    try:
        log.info("Configuring disk monitoring with critical logging disabled")
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
        inst.restart()

        log.info(f"Filling disk to go below half threshold ({THRESHOLD_BYTES // 2} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES // 2 - 1)

        log.info("Waiting for shutdown messages")
        wait_for_log_entry(inst, 'is too far below the threshold', 100)
        wait_for_log_entry(inst, 'Signaling slapd for shutdown', 90)

        log.info("Verifying server shutdown within grace period")
        for i in range(60):
            time.sleep(1)
            if not inst.status():
                log.info(f"Server shut down after {i+1} seconds")
                break
        assert inst.status() == False

        log.info("Freeing disk space and cleaning logs")
        os.remove(fill_file)
        fill_file = None
        open(f'{inst.ds_paths.log_dir}/errors', 'w').close()

        log.info("Starting server after freeing space")
        inst.start()

        log.info("Verifying logging is re-enabled")
        assert inst.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') == 'on'
        assert inst.config.get_attr_val_utf8('nsslapd-errorlog-logging-enabled') == 'on'

        with open(inst.errlog, 'r') as err_log:
            content = err_log.read()
        assert 'disabling access and audit logging' not in content

        log.info("Setting up access log rotation for testing")
        inst.config.set('nsslapd-accesslog-maxlogsize', '1')
        inst.config.set('nsslapd-accesslog-logrotationtimeunit', 'minute')
        inst.config.set('nsslapd-accesslog-level', '772')
        inst.restart()

        log.info("Creating rotated log files through user activity")
        users = generate_access_log_activity(inst, num_users=10, num_binds=100)

        log.info("Waiting for log rotation to occur")
        for i in range(61):
            time.sleep(1)
            rotated_logs = re.findall(r'access.\d+-\d+', str(os.listdir(inst.ds_paths.log_dir)))
            if rotated_logs:
                log.info(f"Log rotation detected after {i+1} seconds")
                break
        assert rotated_logs, "No rotated logs found after waiting"

        inst.bind_s(DN_DM, PW_DM)

        log.info("Enabling verbose logging")
        inst.config.set('nsslapd-accesslog-maxlogsize', '100')
        inst.config.set('nsslapd-accesslog-logrotationtimeunit', 'day')
        inst.config.set('nsslapd-accesslog-level', '256')
        inst.config.set('nsslapd-errorlog-level', '8')
        inst.restart()

        log.info("Recovery and setup verification completed")

    finally:
        # Clean up users
        if users:
            log.debug("Cleaning up test users")
            for user in users.list():
                try:
                    user.delete()
                except ldap.ALREADY_EXISTS:
                    pass

        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_go_straight_below_half_of_the_threshold(topo, setup, reset_logs):
    """Go straight below 1/2 of the threshold and verify recovery.

    :id: a2a0664c-fe9e-11e8-b220-8c16451d917b
    :setup: Standalone
    :steps:
        1. Go straight below 1/2 of the threshold
        2. Verify that verbose logging was set to default level
        3. Verify that logging is disabled
        4. Verify DS is in shutdown mode
        5. Verify DS has recovered from shutdown
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    log.info("Starting test_go_straight_below_half_of_the_threshold")
    inst = topo.standalone
    fill_file = None

    try:
        log.info("Configuring disk monitoring with critical logging disabled")
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
        inst.config.set('nsslapd-errorlog-level', '8')
        inst.restart()

        # Go straight below half threshold
        log.info(f"Filling disk to go below half threshold ({THRESHOLD_BYTES // 2} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES // 2 - 1)

        # Verify that verbose logging was set to default level
        log.info("Waiting for loglevel to be set to default")
        wait_for_log_entry(inst, 'temporarily setting error loglevel to the default level', 11)

        log.info("Verifying error log level was set to default")
        config_entry = inst.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectclass=*)', ['nsslapd-errorlog-level'])
        current_level = int(re.findall(r'nsslapd-errorlog-level: \d+', str(config_entry))[0].split(' ')[1])
        assert LOG_DEFAULT == current_level

        log.info("Verifying access logging is disabled")
        wait_for_condition(inst, "inst.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') != 'off'", 11)

        log.info("Verifying expected disk monitoring messages")
        wait_for_log_entry(inst, 'disabling access and audit logging', 2)
        wait_for_log_entry(inst, 'deleting rotated logs', 11)

        with open(inst.errlog, 'r') as err_log:
            content = err_log.read()
        assert 'Unable to remove file:' not in content

        log.info("Verifying server enters shutdown mode")
        wait_for_condition(inst, 'inst.status() != False', 90)
        wait_for_log_entry(inst, 'is too far below the threshold', 2)

        log.info("Freeing disk space and restarting server")
        os.remove(fill_file)
        fill_file = None
        open(f'{inst.ds_paths.log_dir}/errors', 'w').close()
        inst.start()

        log.info("Verifying server recovery")
        wait_for_condition(inst, "inst.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') != 'on'", 20)

        with open(inst.errlog, 'r') as err_log:
            content = err_log.read()
        assert 'disabling access and audit logging' not in content

        log.info("Recovery verification completed")

    finally:
        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_readonly_on_threshold(topo, setup, reset_logs):
    """Verify that nsslapd-disk-monitoring-readonly-on-threshold switches the server to read-only mode.

    :id: 06814c19-ef3c-4800-93c9-c7c6e76fcbb9
    :customerscenario: True
    :setup: Standalone
    :steps:
        1. Configure readonly on threshold
        2. Go below threshold and verify backend is read-only
        3. Go back above threshold and verify backend is read-write
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    log.info("Starting test_readonly_on_threshold")
    inst = topo.standalone
    fill_file = None
    test_user = None

    try:
        backends = Backends(inst)
        backend_name = backends.list()[0].rdn
        log.info(f"Testing with backend: {backend_name}")

        log.info("Configuring disk monitoring with readonly on threshold")
        inst.deleteErrorLogs()
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.config.set('nsslapd-disk-monitoring-readonly-on-threshold', 'on')
        inst.restart()

        log.info(f"Filling disk to go below threshold ({THRESHOLD_BYTES} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES - 1)

        log.info("Waiting for backend to enter read-only mode")
        wait_for_log_entry(inst, f"Putting the backend '{backend_name}' to read-only mode", 11)

        log.info("Verifying backend is in read-only mode")
        users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
        try:
            test_user = users.create_test_user()
            test_user.delete()
            assert False, "Expected UNWILLING_TO_PERFORM error for read-only mode"
        except ldap.UNWILLING_TO_PERFORM as e:
            if 'database is read-only' not in str(e):
                raise
            log.info("Confirmed: backend correctly rejects writes in read-only mode")

        log.info("Freeing disk space")
        os.remove(fill_file)
        fill_file = None

        log.info("Waiting for backend to return to read-write mode")
        wait_for_log_entry(inst, f"Putting the backend '{backend_name}' back to read-write mode", 11)

        log.info("Verifying backend is in read-write mode")
        test_user = users.create_test_user()
        assert test_user.exists()
        test_user.delete()
        test_user = None

        log.info("Confirmed: backend correctly accepts writes in read-write mode")

    finally:
        if test_user:
            try:
                test_user.delete()
            except:
                pass

        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_readonly_on_threshold_below_half_of_the_threshold(topo, setup, reset_logs):
    """Go below 1/2 of the threshold when readonly on threshold is enabled.

    :id: 10262663-b41f-420e-a2d0-9532dd54fa7c
    :customerscenario: True
    :setup: Standalone
    :steps:
        1. Configure readonly on threshold
        2. Go below half threshold
        3. Verify backend is read-only and shutdown messages appear
        4. Free space and verify backend returns to read-write
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    log.info("Starting test_readonly_on_threshold_below_half_of_the_threshold")
    inst = topo.standalone
    fill_file = None
    test_user = None

    try:
        backends = Backends(inst)
        backend_name = backends.list()[0].rdn
        log.info(f"Testing with backend: {backend_name}")

        log.info("Configuring disk monitoring with readonly on threshold")
        inst.deleteErrorLogs()
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.config.set('nsslapd-disk-monitoring-readonly-on-threshold', 'on')
        inst.restart()

        log.info(f"Filling disk to go below half threshold ({THRESHOLD_BYTES // 2} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES // 2 - 1)

        log.info("Waiting for backend to enter read-only mode")
        wait_for_log_entry(inst, f"Putting the backend '{backend_name}' to read-only mode", 11)

        log.info("Verifying backend is in read-only mode")
        users = UserAccounts(inst, DEFAULT_SUFFIX)
        try:
            test_user = users.create_test_user()
            test_user.delete()
            assert False, "Expected UNWILLING_TO_PERFORM error for read-only mode"
        except ldap.UNWILLING_TO_PERFORM as e:
            if 'database is read-only' not in str(e):
                raise
            log.info("Confirmed: backend correctly rejects writes in read-only mode")

        log.info("Waiting for shutdown threshold message")
        wait_for_log_entry(inst, 'is too far below the threshold', 51)

        log.info("Freeing disk space")
        os.remove(fill_file)
        fill_file = None

        log.info("Waiting for backend to return to read-write mode")
        wait_for_log_entry(inst, f"Putting the backend '{backend_name}' back to read-write mode", 51)

        log.info("Verifying backend is in read-write mode")
        test_user = users.create_test_user()
        assert test_user.exists()
        test_user.delete()
        test_user = None

        log.info("Confirmed: backend correctly accepts writes in read-write mode")

    finally:
        if test_user:
            try:
                test_user.delete()
            except:
                pass

        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_below_half_of_the_threshold_not_starting_after_shutdown(topo, setup, reset_logs):
    """Test that the instance won't start if we are below 1/2 of the threshold.

    :id: cceeaefd-9fa4-45c5-9ac6-9887a0671ef8
    :customerscenario: True
    :setup: Standalone
    :steps:
        1. Go below half threshold and wait for shutdown
        2. Try to start the instance and verify it fails
        3. Free space and verify instance starts successfully
    :expectedresults:
        1. Success
        2. Startup fails as expected
        3. Success
    """
    log.info("Starting test_below_half_of_the_threshold_not_starting_after_shutdown")
    inst = topo.standalone
    fill_file = None

    try:
        log.info("Configuring disk monitoring")
        inst.deleteErrorLogs()
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.restart()

        log.info(f"Filling disk to go below half threshold ({THRESHOLD_BYTES // 2} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES // 2 - 1)

        log.info("Waiting for server to shut down due to disk space")
        wait_for_condition(inst, 'inst.status() == True', 120)

        log.info("Attempting to start instance (should fail)")
        try:
            inst.start()
            assert False, "Instance startup should have failed due to low disk space"
        except (ValueError, subprocess.CalledProcessError):
            log.info("Instance startup failed as expected due to low disk space")

        wait_for_log_entry(inst, f'is too far below the threshold({THRESHOLD_BYTES} bytes). Exiting now', 2)

        log.info("Freeing disk space")
        os.remove(fill_file)
        fill_file = None

        log.info("Starting instance after freeing space")
        inst.start()
        assert inst.status() == True
        log.info("Instance started successfully after freeing space")

    finally:
        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_go_straight_below_4kb(topo, setup, reset_logs):
    """Go straight below 4KB and verify behavior.

    :id: a855115a-fe9e-11e8-8e91-8c16451d917b
    :setup: Standalone
    :steps:
        1. Go straight below 4KB
        2. Verify server behavior
        3. Clean space and restart
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    log.info("Starting test_go_straight_below_4kb")
    inst = topo.standalone
    fill_file = None

    try:
        log.info("Configuring disk monitoring")
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.restart()

        log.info("Filling disk to go below 4KB")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, 4000)

        log.info("Waiting for server shutdown due to extreme low disk space")
        wait_for_condition(inst, 'inst.status() != False', 11)

        log.info("Freeing disk space and restarting")
        os.remove(fill_file)
        fill_file = None
        inst.start()

        assert inst.status() == True
        log.info("Server restarted successfully after freeing space")

    finally:
        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
def test_threshold_to_overflow_value(topo, setup, reset_logs):
    """Test overflow in nsslapd-disk-monitoring-threshold.

    :id: ad60ab3c-fe9e-11e8-88dc-8c16451d917b
    :setup: Standalone
    :steps:
        1. Set nsslapd-disk-monitoring-threshold to overflow value
        2. Verify the value is set correctly
    :expectedresults:
        1. Success
        2. Success
    """
    log.info("Starting test_threshold_to_overflow_value")
    inst = topo.standalone

    overflow_value = '3000000000'
    log.info(f"Setting threshold to overflow value: {overflow_value}")

    inst.config.set('nsslapd-disk-monitoring-threshold', ensure_bytes(overflow_value))

    config_entry = inst.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectclass=*)', ['nsslapd-disk-monitoring-threshold'])
    current_value = re.findall(r'nsslapd-disk-monitoring-threshold: \d+', str(config_entry))[0].split(' ')[1]
    assert overflow_value == current_value

    log.info(f"Verified: threshold value set to {current_value}")
    log.info("Test completed successfully")


@disk_monitoring_ack
def test_threshold_is_reached_to_half(topo, setup, reset_logs):
    """Verify RHDS not shutting down when disk monitoring threshold is reached to half.

    :id: b2d3665e-fe9e-11e8-b9c0-8c16451d917b
    :setup: Standalone
    :steps:
        1. Configure disk monitoring with critical logging
        2. Go below threshold
        3. Verify there is no endless loop of error messages
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    log.info("Starting test_threshold_is_reached_to_half")
    inst = topo.standalone
    fill_file = None

    try:
        log.info("Configuring disk monitoring with critical logging enabled")
        inst.config.set('nsslapd-disk-monitoring', 'on')
        inst.config.set('nsslapd-disk-monitoring-logging-critical', 'on')
        inst.config.set('nsslapd-errorlog-level', '8')
        inst.config.set('nsslapd-disk-monitoring-threshold', ensure_bytes(str(THRESHOLD_BYTES)))
        inst.restart()

        log.info(f"Filling disk to go below threshold ({THRESHOLD_BYTES} bytes)")
        fill_file = fill_to_target_avail(inst.ds_paths.log_dir, THRESHOLD_BYTES // 2 - 1)

        log.info("Waiting for loglevel message and verifying it's not repeated")
        wait_for_log_entry(inst, "temporarily setting error loglevel to the default level", 11)

        with open(inst.errlog, 'r') as err_log:
            content = err_log.read()

        message_count = len(re.findall("temporarily setting error loglevel to the default level", content))
        assert message_count == 1, f"Expected 1 occurrence of message, found {message_count}"

        log.info("Verified: no endless loop of error messages")

    finally:
        if fill_file and os.path.exists(fill_file):
            log.debug(f"Cleaning up fill file: {fill_file}")
            os.remove(fill_file)

    log.info("Test completed successfully")


@disk_monitoring_ack
@pytest.mark.parametrize("test_input,expected", [
    ("nsslapd-disk-monitoring-threshold", '-2'),
    ("nsslapd-disk-monitoring-threshold", '9223372036854775808'),
    ("nsslapd-disk-monitoring-threshold", '2047'),
    ("nsslapd-disk-monitoring-threshold", '0'),
    ("nsslapd-disk-monitoring-threshold", '-1294967296'),
    ("nsslapd-disk-monitoring-threshold", 'invalid'),
    ("nsslapd-disk-monitoring", 'invalid'),
    ("nsslapd-disk-monitoring", '1'),
    ("nsslapd-disk-monitoring-grace-period", '0'),
    ("nsslapd-disk-monitoring-grace-period", '525 948'),
    ("nsslapd-disk-monitoring-grace-period", '-1'),
    ("nsslapd-disk-monitoring-logging-critical", 'oninvalid'),
    ("nsslapd-disk-monitoring-grace-period", '-1'),
    ("nsslapd-disk-monitoring-grace-period", '0'),
])
def test_negagtive_parameterize(topo, setup, reset_logs, test_input, expected):
    """Verify that invalid operations are not permitted.

    :id: b88efbf8-fe9e-11e8-8499-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Try to set invalid configuration values
    :expectedresults:
        1. Configuration change should fail
    """
    log.info(f"Starting test_negagtive_parameterize for {test_input}={expected}")
    inst = topo.standalone

    log.info(f"Attempting to set invalid value: {test_input}={expected}")
    with pytest.raises(Exception):
        inst.config.set(test_input, ensure_bytes(expected))

    log.info("Verified: invalid configuration value was rejected")
    log.info("Test completed successfully")


@disk_monitoring_ack
def test_valid_operations_are_permitted(topo, setup, reset_logs):
    """Verify that valid operations are permitted.

    :id: bd4f83f6-fe9e-11e8-88f4-8c16451d917b
    :setup: Standalone
    :steps:
        1. Perform various valid configuration operations
    :expectedresults:
        1. All operations should succeed
    """
    log.info("Starting test_valid_operations_are_permitted")
    inst = topo.standalone

    log.info("Setting initial disk monitoring configuration")
    inst.config.set('nsslapd-disk-monitoring', 'on')
    inst.config.set('nsslapd-disk-monitoring-logging-critical', 'on')
    inst.config.set('nsslapd-errorlog-level', '8')
    inst.restart()

    log.info("Testing deletion of nsslapd-disk-monitoring-threshold")
    inst.modify_s('cn=config', [(ldap.MOD_DELETE, 'nsslapd-disk-monitoring-threshold', '')])

    log.info("Testing addition of nsslapd-disk-monitoring-threshold value")
    inst.config.add('nsslapd-disk-monitoring-threshold', '2000001')

    log.info("Testing deletion of nsslapd-disk-monitoring")
    config_entry = inst.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectclass=*)', ['nsslapd-disk-monitoring'])
    current_value = str(config_entry[0]).split(' ')[2].split('\n\n')[0]
    inst.modify_s('cn=config', [(ldap.MOD_DELETE, 'nsslapd-disk-monitoring', ensure_bytes(current_value))])

    log.info("Testing addition of nsslapd-disk-monitoring value")
    inst.config.add('nsslapd-disk-monitoring', 'off')

    log.info("Testing deletion of nsslapd-disk-monitoring-grace-period")
    inst.modify_s('cn=config', [(ldap.MOD_DELETE, 'nsslapd-disk-monitoring-grace-period', '')])

    log.info("Testing addition of nsslapd-disk-monitoring-grace-period value")
    inst.config.add('nsslapd-disk-monitoring-grace-period', '61')

    log.info("Testing deletion of nsslapd-disk-monitoring-logging-critical")
    config_entry = inst.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectclass=*)', ['nsslapd-disk-monitoring-logging-critical'])
    current_value = str(config_entry[0]).split(' ')[2].split('\n\n')[0]
    inst.modify_s('cn=config', [(ldap.MOD_DELETE, 'nsslapd-disk-monitoring-logging-critical', ensure_bytes(current_value))])

    log.info("Testing addition of nsslapd-disk-monitoring-logging-critical value")
    inst.config.set('nsslapd-disk-monitoring-logging-critical', 'on')

    log.info("All valid operations completed successfully")
    log.info("Test completed successfully")


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

