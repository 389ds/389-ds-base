# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import glob
import os
import logging
import time
import pytest
from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.tasks import ImportTask
from lib389.idm.user import UserAccounts
from lib389.idm.domain import Domain
from lib389.idm.directorymanager import DirectoryManager
from lib389.topologies import topology_st as topo


log = logging.getLogger(__name__)


def generate_heavy_load(inst, suffix, iterations=50):
    """
    Generate heavy LDAP load to fill access log quickly.
    Performs multiple operations: searches, modifies, binds to populate logs.
    """
    for i in range(iterations):
        suffix.replace('description', f'iteration_{i}')
        suffix.get_attr_val('description')


def count_access_logs(log_dir, compressed_only=False):
    """
    Count access log files in the log directory.
    Returns count of rotated access logs (not including the active 'access' file).
    """
    if compressed_only:
        pattern = f'{log_dir}/access.*.gz'
    else:
        pattern = f'{log_dir}/access.2*'
    log_files = glob.glob(pattern)
    return len(log_files)


def test_log_pileup_with_compression(topo):
    """Test that log rotation properly deletes old logs when compression is enabled.

    :id: 772980f9-dc0b-4f65-be9a-19af6ffb2d8e
    :setup: Standalone Instance
    :steps:
        1. Enable access log compression
        2. Set strict log limits (small maxlogsperdir)
        3. Disable log expiration to test count-based deletion
        4. Generate heavy load to create many log rotations
        5. Verify log count does not exceed maxlogsperdir limit
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Log count should be at or below maxlogsperdir + small buffer
    """

    inst = topo.standalone
    suffix = Domain(inst, DEFAULT_SUFFIX)
    log_dir = inst.get_log_dir()

    max_logs = 5
    inst.config.set('nsslapd-accesslog-compress', 'on')
    inst.config.set('nsslapd-accesslog-maxlogsperdir', str(max_logs))
    inst.config.set('nsslapd-accesslog-maxlogsize', '1')  # 1MB to trigger rotation
    inst.config.set('nsslapd-accesslog-logrotationsync-enabled', 'off')
    inst.config.set('nsslapd-accesslog-logbuffering', 'off')

    inst.config.set('nsslapd-accesslog-logexpirationtime', '-1')

    inst.config.set('nsslapd-accesslog-logminfreediskspace', '5')

    inst.restart()
    time.sleep(2)

    target_logs = max_logs * 3
    for i in range(target_logs):
        log.info(f"Generating load for log rotation {i+1}/{target_logs}")
        generate_heavy_load(inst, suffix, iterations=150)
        time.sleep(1)  # Wait for rotation

    time.sleep(3)

    logs_on_disk = count_access_logs(log_dir)
    log.info(f"Configured maxlogsperdir: {max_logs}")
    log.info(f"Actual rotated logs on disk: {logs_on_disk}")

    all_access_logs = glob.glob(f'{log_dir}/access*')
    log.info(f"All access log files: {all_access_logs}")

    max_allowed = max_logs + 2
    assert logs_on_disk <= max_allowed, (
        f"Log rotation failed to delete old files! "
        f"Expected at most {max_allowed} rotated logs (maxlogsperdir={max_logs} + 2 buffer), "
        f"but found {logs_on_disk}. The server has lost track of the file list."
    )


@pytest.mark.parametrize("compress_enabled", ["on", "off"])
def test_accesslog_list_mismatch(topo, compress_enabled):
    """Test that nsslapd-accesslog-list stays synchronized with actual log files.

    :id: 8bd4e4d2-1aa5-4d23-9d07-1b01f2a3fe84
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Configure log rotation with compression enabled/disabled
        2. Generate activity to trigger multiple rotations
        3. Get the nsslapd-accesslog-list attribute
        4. Compare with actual files on disk
        5. Verify they match (accounting for .gz extension when enabled)
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. The list attribute should match actual files on disk
    """

    inst = topo.standalone
    suffix = Domain(inst, DEFAULT_SUFFIX)
    log_dir = inst.get_log_dir()
    compression_on = compress_enabled == "on"

    inst.config.set('nsslapd-accesslog-compress', compress_enabled)
    inst.config.set('nsslapd-accesslog-maxlogsize', '1')
    inst.config.set('nsslapd-accesslog-maxlogsperdir', '10')
    inst.config.set('nsslapd-accesslog-logrotationsync-enabled', 'off')
    inst.config.set('nsslapd-accesslog-logbuffering', 'off')
    inst.config.set('nsslapd-accesslog-logexpirationtime', '-1')

    inst.restart()
    time.sleep(2)

    for i in range(8):
        suffix_note = "(no compression)" if not compression_on else ""
        log.info(f"Generating load for rotation {i+1}/8 {suffix_note}")
        generate_heavy_load(inst, suffix, iterations=150)
        time.sleep(1)

    time.sleep(3)

    accesslog_list_raw = inst.config.get_attr_val_utf8('nsslapd-accesslog-list')
    if accesslog_list_raw:
        accesslog_list = [f.strip() for f in accesslog_list_raw.split() if f.strip()]
    else:
        accesslog_list = []
    log.info(f"nsslapd-accesslog-list entries (compress={compress_enabled}): {len(accesslog_list)}")
    log.info(f"nsslapd-accesslog-list (compress={compress_enabled}): {accesslog_list}")

    disk_files = glob.glob(f'{log_dir}/access.2*')
    log.info(f"Actual files on disk (compress={compress_enabled}): {len(disk_files)}")
    log.info(f"Disk files (compress={compress_enabled}): {disk_files}")

    disk_files_for_compare = set()
    for fpath in disk_files:
        if compression_on and fpath.endswith('.gz'):
            disk_files_for_compare.add(fpath[:-3])
        else:
            disk_files_for_compare.add(fpath)

    list_files_set = set(accesslog_list)
    missing_from_disk = list_files_set - disk_files_for_compare
    extra_on_disk = disk_files_for_compare - list_files_set

    if missing_from_disk:
        log.error(
            f"[compress={compress_enabled}] Files in list but NOT on disk: {missing_from_disk}"
        )
    if extra_on_disk:
        log.warning(
            f"[compress={compress_enabled}] Files on disk but NOT in list: {extra_on_disk}"
        )

    assert not missing_from_disk, (
        f"nsslapd-accesslog-list mismatch (compress={compress_enabled})! "
        f"Files listed but missing from disk: {missing_from_disk}. "
        f"This indicates the server's internal list is out of sync with actual files."
    )

    if len(extra_on_disk) > 2:
        log.warning(
            f"Potential log tracking issue (compress={compress_enabled}): "
            f"{len(extra_on_disk)} files on disk are not tracked in the accesslog-list: "
            f"{extra_on_disk}"
        )


def test_log_flush_and_rotation_crash(topo):
    """Make sure server does not crash when flushing a buffer and rotating
    the log at the same time

    :id: d4b0af2f-48b2-45f5-ae8b-f06f692c3133
    :setup: Standalone Instance
    :steps:
        1. Enable all logs
        2. Enable log buffering for all logs
        3. Set rotation time unit to 1 minute
        4. Make sure server is still running after 1 minute
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    #NOTE: This test is placed last as it may affect the suffix state.

    inst = topo.standalone

    # Enable logging and buffering
    inst.config.set("nsslapd-auditlog-logging-enabled", "on")
    inst.config.set("nsslapd-accesslog-logbuffering", "on")
    inst.config.set("nsslapd-auditlog-logbuffering", "on")
    inst.config.set("nsslapd-errorlog-logbuffering", "on")
    inst.config.set("nsslapd-securitylog-logbuffering", "on")

    # Set rotation policy to trigger rotation asap
    inst.config.set("nsslapd-accesslog-logrotationtimeunit", "minute")
    inst.config.set("nsslapd-auditlog-logrotationtimeunit", "minute")
    inst.config.set("nsslapd-errorlog-logrotationtimeunit", "minute")
    inst.config.set("nsslapd-securitylog-logrotationtimeunit", "minute")

    #
    # Performs ops to populate all the logs
    #
    # Access & audit log
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user()
    user.set("userPassword", PW_DM)
    # Security log
    user.bind(PW_DM)
    # Error log
    import_task = ImportTask(inst)
    import_task.import_suffix_from_ldif(ldiffile="/not/here",
                                        suffix=DEFAULT_SUFFIX)

    # Wait a minute and make sure the server did not crash
    log.info("Sleep until logs are flushed and rotated")
    time.sleep(61)

    assert inst.status()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

