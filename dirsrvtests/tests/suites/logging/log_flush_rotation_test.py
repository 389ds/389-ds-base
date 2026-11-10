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
import re
import threading
import time
import uuid
import ldap
import pytest
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PW_DM
from lib389.tasks import ImportTask
from lib389.idm.user import UserAccounts
from lib389.idm.domain import Domain
from lib389.idm.directorymanager import DirectoryManager
from test389.topologies import topology_st as topo


log = logging.getLogger(__name__)


def remove_rotated_access_logs(inst):
    """
    Remove all rotated access log files to start fresh for each test.
    This prevents log files from previous tests affecting current test results.
    """
    log_dir = inst.get_log_dir()
    patterns = [
        f'{log_dir}/access.2*',      # Uncompressed rotated logs
        f'{log_dir}/access.*.gz',    # Compressed rotated logs
    ]
    for pattern in patterns:
        for log_file in glob.glob(pattern):
            try:
                os.remove(log_file)
                log.info(f"Removed old log file: {log_file}")
            except OSError as e:
                log.warning(f"Could not remove {log_file}: {e}")


def reset_access_log_config(inst):
    """
    Reset access log configuration to default values.
    """
    inst.config.set('nsslapd-accesslog-compress', 'off')
    inst.config.set('nsslapd-accesslog-maxlogsize', '100')
    inst.config.set('nsslapd-accesslog-maxlogsperdir', '10')
    inst.config.set('nsslapd-accesslog-logrotationsync-enabled', 'off')
    inst.config.set('nsslapd-accesslog-logbuffering', 'on')
    inst.config.set('nsslapd-accesslog-logexpirationtime', '-1')
    inst.config.set('nsslapd-accesslog-logminfreediskspace', '5')


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


def run_marked_searches(inst, threads, searches):
    """
    Run searches from several connections at once, one connection per thread.
    Each search filter carries a marker naming the run, the thread and the
    search number, so the SRCH lines can be found in the access log.
    Returns the run marker.
    """
    run = uuid.uuid4().hex[:8]
    errors = []

    def worker(t):
        try:
            conn = ldap.initialize(inst.toLDAPURL())
            conn.simple_bind_s(DN_DM, PW_DM)
            for i in range(searches):
                conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                              f'(uid=al-{run}-{t}-{i})', ['1.1'])
            conn.unbind_s()
        except ldap.LDAPError as e:
            errors.append(e)

    workers = [threading.Thread(target=worker, args=(t,)) for t in range(threads)]
    for w in workers:
        w.start()
    for w in workers:
        w.join()
    assert not errors, f"Searches failed: {errors}"
    return run


def read_marked_searches(inst, run):
    """
    Read the access log, rotated logs first, and return the SRCH lines of a
    run of run_marked_searches() in file order, as (conn, op, thread, search)
    tuples.
    """
    log_dir = inst.get_log_dir()
    srch = re.compile(r'conn=(\d+) op=(\d+) SRCH .*filter="\(uid=al-' + run + r'-(\d+)-(\d+)\)"')
    found = []
    rotated = sorted(f for f in glob.glob(f'{log_dir}/access.2*') if not f.endswith('.gz'))
    for log_file in rotated + [f'{log_dir}/access']:
        with open(log_file, errors='replace') as f:
            for line in f:
                m = srch.search(line)
                if m:
                    found.append(tuple(int(g) for g in m.groups()))
    return found


def check_marked_searches(found, threads, searches):
    """
    Check that every search of a run is logged exactly once, and that each
    connection's searches are logged in the order they were sent.
    """
    assert len(found) == threads * searches, (
        f"Expected {threads * searches} SRCH lines, found {len(found)}")
    assert len(set((t, i) for (_, _, t, i) in found)) == threads * searches, \
        "Some SRCH lines are duplicated"
    for t in range(threads):
        mine = [(conn, op, i) for (conn, op, tt, i) in found if tt == t]
        assert len(set(conn for (conn, _, _) in mine)) == 1
        assert [i for (_, _, i) in mine] == list(range(searches)), \
            f"Searches of thread {t} are out of order"
        ops = [op for (_, op, _) in mine]
        assert ops == sorted(ops), f"Operations of thread {t} are out of order"


def test_log_pileup_with_compression(topo):
    """Test that log rotation properly deletes old logs when compression is enabled.

    :id: fa1bfce8-b6d3-4520-a0a8-bead14fa5838
    :setup: Standalone Instance
    :steps:
        1. Clean up existing rotated logs and reset configuration
        2. Enable access log compression
        3. Set strict log limits (small maxlogsperdir)
        4. Disable log expiration to test count-based deletion
        5. Generate heavy load to create many log rotations
        6. Verify log count does not exceed maxlogsperdir limit
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Log count should be at or below maxlogsperdir + small buffer
    """

    inst = topo.standalone
    suffix = Domain(inst, DEFAULT_SUFFIX)
    log_dir = inst.get_log_dir()

    # Clean up before test
    remove_rotated_access_logs(inst)
    reset_access_log_config(inst)
    inst.restart()

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

    :id: 0a8a46a6-cae7-43bd-8b64-5e3481480cd3
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Clean up existing rotated logs and reset configuration
        2. Configure log rotation with compression enabled/disabled
        3. Generate activity to trigger multiple rotations
        4. Get the nsslapd-accesslog-list attribute
        5. Compare with actual files on disk
        6. Verify they match (accounting for .gz extension when enabled)
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. The list attribute should match actual files on disk
    """

    inst = topo.standalone
    suffix = Domain(inst, DEFAULT_SUFFIX)
    log_dir = inst.get_log_dir()
    compression_on = compress_enabled == "on"

    # Clean up before test
    remove_rotated_access_logs(inst)
    reset_access_log_config(inst)
    inst.restart()

    inst.config.set('nsslapd-accesslog-compress', compress_enabled)
    inst.config.set('nsslapd-accesslog-maxlogsize', '1')
    inst.config.set('nsslapd-accesslog-maxlogsperdir', '10')
    inst.config.set('nsslapd-accesslog-logrotationsync-enabled', 'off')
    inst.config.set('nsslapd-accesslog-logbuffering', 'off')
    inst.config.set('nsslapd-accesslog-logexpirationtime', '-1')

    inst.restart()
    time.sleep(2)

    for i in range(15):
        suffix_note = "(no compression)" if not compression_on else ""
        log.info(f"Generating load for rotation {i+1}/15 {suffix_note}")
        generate_heavy_load(inst, suffix, iterations=150)
        time.sleep(1)

    time.sleep(3)

    accesslog_list = inst.config.get_attr_vals_utf8('nsslapd-accesslog-list')
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


def test_accesslog_list_mixed_compression(topo):
    """Test that nsslapd-accesslog-list correctly tracks both compressed and uncompressed logs.

    :id: 11b088cd-23be-407d-ad16-4ce2e12da09e
    :setup: Standalone Instance
    :steps:
        1. Clean up existing rotated logs and reset configuration
        2. Create rotated logs with compression OFF
        3. Enable compression and create more rotated logs
        4. Get the nsslapd-accesslog-list attribute
        5. Compare with actual files on disk
        6. Verify all files are correctly tracked (uncompressed and compressed)
    :expectedresults:
        1. Success
        2. Success - uncompressed rotated logs created
        3. Success - compressed rotated logs created
        4. Success
        5. Success
        6. The list should contain base filenames (without .gz) that
           correspond to files on disk (either as-is or with .gz suffix)
    """

    inst = topo.standalone
    suffix = Domain(inst, DEFAULT_SUFFIX)
    log_dir = inst.get_log_dir()

    # Clean up before test
    remove_rotated_access_logs(inst)
    reset_access_log_config(inst)
    inst.restart()

    inst.config.set('nsslapd-accesslog-compress', 'off')
    inst.config.set('nsslapd-accesslog-maxlogsize', '1')
    inst.config.set('nsslapd-accesslog-maxlogsperdir', '20')
    inst.config.set('nsslapd-accesslog-logrotationsync-enabled', 'off')
    inst.config.set('nsslapd-accesslog-logbuffering', 'off')
    inst.config.set('nsslapd-accesslog-logexpirationtime', '-1')

    inst.restart()
    time.sleep(2)

    for i in range(15):
        log.info(f"Generating load for uncompressed rotation {i+1}/15")
        generate_heavy_load(inst, suffix, iterations=150)
        time.sleep(1)

    time.sleep(2)

    # Check what we have so far
    uncompressed_files = glob.glob(f'{log_dir}/access.2*')
    log.info(f"Files on disk after uncompressed phase: {uncompressed_files}")

    inst.config.set('nsslapd-accesslog-compress', 'on')
    inst.restart()
    time.sleep(2)

    for i in range(15):
        log.info(f"Generating load for compressed rotation {i+1}/15")
        generate_heavy_load(inst, suffix, iterations=150)
        time.sleep(1)

    time.sleep(3)

    accesslog_list = inst.config.get_attr_vals_utf8('nsslapd-accesslog-list')

    disk_files = glob.glob(f'{log_dir}/access.2*')

    log.info(f"nsslapd-accesslog-list entries: {len(accesslog_list)}")
    log.info(f"nsslapd-accesslog-list: {sorted(accesslog_list)}")
    log.info(f"Actual files on disk: {len(disk_files)}")
    log.info(f"Disk files: {sorted(disk_files)}")

    compressed_on_disk = [f for f in disk_files if f.endswith('.gz')]
    uncompressed_on_disk = [f for f in disk_files if not f.endswith('.gz')]
    log.info(f"Compressed files on disk: {compressed_on_disk}")
    log.info(f"Uncompressed files on disk: {uncompressed_on_disk}")

    list_files_set = set(accesslog_list)

    disk_files_base = set()
    for fpath in disk_files:
        if fpath.endswith('.gz'):
            disk_files_base.add(fpath[:-3])  # Strip .gz
        else:
            disk_files_base.add(fpath)

    missing_from_disk = list_files_set - disk_files_base

    extra_on_disk = disk_files_base - list_files_set

    if missing_from_disk:
        log.error(f"Files in list but NOT on disk: {missing_from_disk}")
    if extra_on_disk:
        log.warning(f"Files on disk but NOT in list: {extra_on_disk}")

    assert not missing_from_disk, (
        f"nsslapd-accesslog-list contains stale entries! "
        f"Files in list but not on disk (as base or .gz): {missing_from_disk}"
    )

    for list_file in accesslog_list:
        exists_uncompressed = os.path.exists(list_file)
        exists_compressed = os.path.exists(list_file + '.gz')
        assert exists_uncompressed or exists_compressed, (
            f"File in accesslog-list does not exist on disk: {list_file} "
            f"(checked both {list_file} and {list_file}.gz)"
        )
        if exists_compressed and not exists_uncompressed:
            log.info(f"  {list_file} -> exists as .gz (compressed)")
        elif exists_uncompressed:
            log.info(f"  {list_file} -> exists (uncompressed)")

    if len(extra_on_disk) > 1:
        log.warning(
            f"Some files on disk are not tracked in accesslog-list: {extra_on_disk}"
        )

    log.info("Mixed compression test completed successfully")


def test_accesslog_buffer_written_at_shutdown(topo):
    """Check that every buffered access log line is written when the server stops

    :id: a0e62477-4391-440d-bbe9-cc45512a38a4
    :setup: Standalone Instance
    :steps:
        1. Reset the access log configuration, with log buffering on
        2. Run searches from several connections at once
        3. Stop the server right away
        4. Check that every search is in the access log exactly once
        5. Start the server
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    inst = topo.standalone
    threads = 4
    searches = 500

    remove_rotated_access_logs(inst)
    reset_access_log_config(inst)
    inst.restart()

    run = run_marked_searches(inst, threads, searches)
    inst.stop()
    try:
        found = read_marked_searches(inst, run)
        check_marked_searches(found, threads, searches)
    finally:
        inst.start()


def test_accesslog_rotation_under_load(topo):
    """Check that no access log line is lost or duplicated when the log
    rotates while several connections are logging

    :id: 4fdd09e1-8a22-4cda-8383-651dd2668d81
    :setup: Standalone Instance
    :steps:
        1. Reset the access log configuration, with log buffering on
        2. Set a 1MB maximum log size so the log rotates under load
        3. Run searches from several connections at once
        4. Stop the server
        5. Check that the log rotated
        6. Check that every search is in the access logs exactly once,
           and in order for each connection
        7. Reset the access log configuration
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    inst = topo.standalone
    threads = 8
    searches = 2500

    remove_rotated_access_logs(inst)
    reset_access_log_config(inst)
    inst.config.set('nsslapd-accesslog-maxlogsize', '1')
    inst.config.set('nsslapd-accesslog-maxlogsperdir', '50')
    inst.restart()

    run = run_marked_searches(inst, threads, searches)
    inst.stop()
    try:
        rotated = count_access_logs(inst.get_log_dir())
        log.info(f"Rotated access logs: {rotated}")
        assert rotated >= 2
        found = read_marked_searches(inst, run)
        check_marked_searches(found, threads, searches)
    finally:
        inst.start()
        remove_rotated_access_logs(inst)
        reset_access_log_config(inst)


@pytest.mark.parametrize("buffering", ["on", "off"])
def test_accesslog_connection_order(topo, buffering):
    """Check that the operations of each connection are logged in order

    :id: 6917dd4b-cdf0-4a22-a57a-a0a20c160138
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Reset the access log configuration, with log buffering on or off
        2. Run searches from several connections at once
        3. Check that every search is in the access log exactly once,
           and that each connection's operations are in order
        4. Reset the access log configuration
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    inst = topo.standalone
    threads = 8
    searches = 300

    remove_rotated_access_logs(inst)
    reset_access_log_config(inst)
    inst.config.set('nsslapd-accesslog-logbuffering', buffering)

    try:
        run = run_marked_searches(inst, threads, searches)
        if buffering == "on":
            # Stopping the server flushes the buffered lines
            inst.stop()
            inst.start()
        found = read_marked_searches(inst, run)
        check_marked_searches(found, threads, searches)
    finally:
        reset_access_log_config(inst)


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
    # NOTE: This test is placed last as it may affect the suffix state.

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

