# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import json
import logging
import os
import re
import signal
import stat
import struct
import subprocess
import threading
import time

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX, DN_CONFIG, DN_DM, PW_DM
from lib389.cli_ctl.threadpool import (HEADER_FORMAT, TP_STATS_HEADER_SIZE,
                                       TP_STATS_MAGIC, TP_STATS_WORKER_SLOT_SIZE)
from lib389.dseldif import DSEldif
from lib389.idm.account import Anonymous
from lib389.idm.user import UserAccounts
from lib389.monitor import Monitor
from test389.topologies import topology_st as topo


pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _threadpool_path(inst):
    dse = DSEldif(inst)
    rundir = dse.get(DN_CONFIG, "nsslapd-rundir", single=True, lower=True)
    if rundir is None:
        rundir = inst.ds_paths.run_dir
    prefix = inst.serverid if inst.serverid.startswith("slapd-") else f"slapd-{inst.serverid}"
    return os.path.join(rundir, f"{prefix}.threadpool")


def _wait_threadpool_file(inst, timeout=5):
    path = _threadpool_path(inst)
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return path
        time.sleep(0.1)
    raise AssertionError(f"{path} was not created within {timeout}s")


def _archive_paths(inst):
    path = _threadpool_path(inst)
    dirname, base = os.path.split(path)
    pattern = re.compile(re.escape(base) + r"\.\d{8}-\d{6}$")
    try:
        names = sorted(name for name in os.listdir(dirname) if pattern.match(name))
    except OSError:
        return []
    return [os.path.join(dirname, name) for name in names]


def _purge_archives(inst):
    for archive in _archive_paths(inst):
        try:
            os.unlink(archive)
        except OSError:
            pass


def _run_dsctl_threadpool(inst, json_output=False, timeout=10, extra_args=None):
    cmd = ["dsctl"]
    if json_output:
        cmd.append("-j")
    cmd.extend([inst.serverid, "thread-pool", "status"])
    if extra_args:
        cmd.extend(extra_args)
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def _cmd_output(result):
    return f"{result.stdout}\n{result.stderr}"


def _safe_unbind(conn):
    try:
        conn.unbind_s()
    except ldap.LDAPError:
        pass


def _json_result(result):
    assert result.returncode == 0, _cmd_output(result)
    return json.loads(result.stdout)


def _assert_monitor_values_sanitized(values):
    assert values
    pattern = re.compile(r"^worker=\d+ state=\w+ op=\w* duration_ns=\d+$")
    for value in values:
        assert pattern.match(value)
        assert "conn=" not in value
        assert "op_id=" not in value


def test_file_created_mode(topo):
    """The thread-pool mmap file is created with the expected mode and owner

    :id: 2bcb2478-b0e7-40e9-acdb-9a94f6039d00
    :setup: Standalone instance
    :steps:
        1. Resolve the thread-pool mmap path from dse.ldif.
        2. Inspect the file metadata.
        3. Compare mode and owner with the runtime directory.
    :expectedresults:
        1. The file exists.
        2. The mode is 0640.
        3. The file owner matches the runtime directory owner.
    """
    inst = topo.standalone
    path = _wait_threadpool_file(inst)

    st = os.stat(path)
    rundir_st = os.stat(os.path.dirname(path))
    assert stat.S_IMODE(st.st_mode) == 0o640
    assert st.st_uid == rundir_st.st_uid


def test_file_unlinked_on_stop(topo):
    """The thread-pool mmap file is removed during clean shutdown

    :id: 7ffa8107-2403-4e97-ac83-cf448cb01463
    :setup: Standalone instance
    :steps:
        1. Resolve the thread-pool mmap path.
        2. Stop the instance.
        3. Run dsctl thread-pool status.
        4. Start the instance again.
    :expectedresults:
        1. The file exists while running.
        2. The file is absent after stop.
        3. dsctl reports that the instance is not running.
        4. The instance is restored for later tests.
    """
    inst = topo.standalone
    path = _wait_threadpool_file(inst)

    inst.stop()
    try:
        assert not os.path.exists(path)
        result = _run_dsctl_threadpool(inst)
        assert result.returncode != 0
        assert "instance is not running" in _cmd_output(result).lower()
    finally:
        inst.start()


def test_dsctl_basic_output(topo):
    """dsctl thread-pool status reports text and JSON status

    :id: 3ba509a3-b347-4e81-a92c-04a1aa6ed17e
    :setup: Standalone instance
    :steps:
        1. Run dsctl thread-pool status.
        2. Run dsctl -j thread-pool status.
        3. Parse the JSON output.
    :expectedresults:
        1. Text output includes pool gauges and a worker table.
        2. JSON output is valid.
        3. JSON output includes pool, worker, and warning fields.
    """
    inst = topo.standalone
    _wait_threadpool_file(inst)

    result = _run_dsctl_threadpool(inst)
    assert result.returncode == 0, _cmd_output(result)
    output = result.stdout
    for expected in ("Instance:", "PID:", "Heartbeat age:", "Workers:", "Queue:", "Operations:", "IDX"):
        assert expected in output

    data = _json_result(_run_dsctl_threadpool(inst, json_output=True))
    assert data["type"] == "result"
    assert data["instance"] == inst.serverid
    assert data["pool"]["max_workers"] >= 1
    assert isinstance(data["workers"], list)
    assert isinstance(data["warnings"], list)


def test_dsctl_under_saturation(topo):
    """dsctl remains available while the worker pool is busy

    :id: 04f806c0-4a4f-4cce-ac58-a84297d5b04c
    :setup: Standalone instance
    :steps:
        1. Restart the instance with two worker threads.
        2. Add enough test users to make subtree searches non-trivial.
        3. Run concurrent searches: authenticated persistent-connection
           loops plus fresh anonymous connections whose search is the
           first operation (op 0) on each connection.
        4. Poll dsctl -j thread-pool status while searches are active.
        5. Restore the original thread count.
    :expectedresults:
        1. The instance restarts.
        2. Test users are created.
        3. Searches run concurrently.
        4. dsctl reports a busy worker with operation detail, including
           a worker running a first operation with op_id 0 and a duration.
        5. The instance is restored for later tests.
    """
    inst = topo.standalone
    original_threadnumber = inst.config.get_attr_val_utf8("nsslapd-threadnumber")
    created = []
    stop_event = threading.Event()
    search_threads = []
    search_errors = []

    def _new_conn():
        conn = ldap.initialize(inst.ldapuri)
        # Restart syscalls interrupted by stray signals (subprocess
        # reaping, harness timers) instead of failing with SERVER_DOWN
        conn.set_option(ldap.OPT_RESTART, ldap.OPT_ON)
        return conn

    def search_worker():
        conn = None
        try:
            conn = _new_conn()
            conn.simple_bind_s(DN_DM, PW_DM)
            while not stop_event.is_set():
                conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(objectclass=*)", ["1.1"])
        except ldap.LDAPError as e:
            if not stop_event.is_set():
                search_errors.append(str(e))
        finally:
            if conn is not None:
                _safe_unbind(conn)

    def first_op_search_worker():
        # No bind: the search is the first operation (op 0) on each connection
        try:
            while not stop_event.is_set():
                conn = _new_conn()
                try:
                    conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(objectclass=*)", ["1.1"])
                finally:
                    _safe_unbind(conn)
        except ldap.LDAPError as e:
            if not stop_event.is_set():
                search_errors.append(str(e))

    try:
        inst.config.replace("nsslapd-threadnumber", "2")
        inst.restart()
        _wait_threadpool_file(inst)

        users = UserAccounts(inst, DEFAULT_SUFFIX)
        base_uid = 700000 + (int(time.time()) % 100000)
        for uid in range(base_uid, base_uid + 50):
            created.append(users.create_test_user(uid=uid, gid=uid))

        for target in [search_worker] * 4 + [first_op_search_worker] * 4:
            thread = threading.Thread(target=target)
            thread.daemon = True
            thread.start()
            search_threads.append(thread)

        busy_worker = None
        op0_worker = None
        last_data = None
        # Generous deadline: each poll pays a full dsctl python startup,
        # which can take seconds on slow or sanitizer builds.
        deadline = time.time() + 30
        while time.time() < deadline and (busy_worker is None or op0_worker is None):
            last_data = _json_result(_run_dsctl_threadpool(inst, json_output=True, timeout=15))
            for worker in last_data["workers"]:
                if worker["state"] != "busy" or worker["op_id"] is None:
                    continue
                if not worker["op"] or not worker["conn"]:
                    continue
                if busy_worker is None:
                    busy_worker = worker
                if op0_worker is None and worker["op_id"] == 0:
                    op0_worker = worker
            time.sleep(0.25)

        assert not search_errors, search_errors
        assert busy_worker is not None, last_data
        assert busy_worker["duration_ns"] >= 0
        assert op0_worker is not None, last_data
        assert op0_worker["op"]
        assert op0_worker["duration_ns"] >= 0
    finally:
        stop_event.set()
        for thread in search_threads:
            thread.join(timeout=2)
        for user in created:
            try:
                user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass
        inst.config.replace("nsslapd-threadnumber", original_threadnumber)
        inst.restart()


def test_stale_file_after_kill(topo):
    """dsctl reports a stale file after an unclean server exit

    :id: 4a865d53-3350-4f4a-8910-d30552f462b6
    :setup: Standalone instance
    :steps:
        1. Read the server pid from dsctl JSON output.
        2. Kill the server process.
        3. Run dsctl thread-pool status against the leftover mmap file.
        4. Restart the instance.
    :expectedresults:
        1. The pid is available.
        2. The server exits without clean mmap unlink.
        3. dsctl returns data with a stale-pid warning.
        4. The instance is restored for later tests.
    """
    inst = topo.standalone
    path = _wait_threadpool_file(inst)
    data = _json_result(_run_dsctl_threadpool(inst, json_output=True))
    pid = data["pid"]

    os.kill(pid, signal.SIGKILL)
    try:
        deadline = time.time() + 10
        while time.time() < deadline and inst.status():
            time.sleep(0.2)

        assert os.path.exists(path)
        result = _run_dsctl_threadpool(inst)
        assert result.returncode == 0, _cmd_output(result)
        assert "not running" in _cmd_output(result).lower()
    finally:
        if not inst.status():
            inst.start()
        _purge_archives(inst)


def test_symlink_rejected(topo):
    """Symlinks at the mmap path are never followed by the writer or the reader

    :id: d833f584-7cbf-460d-8079-0687a00fd483
    :setup: Standalone instance
    :steps:
        1. Stop the instance and place a symlink at the thread-pool mmap path.
        2. Start the instance.
        3. Check the outcome of the startup symlink handling.
        4. Stop the instance and place another symlink at the same path.
        5. Run dsctl thread-pool status.
        6. Clean up and restart the instance.
    :expectedresults:
        1. The symlink is in place before startup.
        2. The instance starts.
        3. Either startup replaced the symlink with a regular mmap file, or
           (with SELinux denying the unlink of a foreign-labeled symlink)
           the feature failed safe: the symlink was not followed, a warning
           was logged, and dsctl refuses the path.
        4. The symlink is in place for the reader.
        5. dsctl refuses the symlink.
        6. The instance is restored for later tests.
    """
    inst = topo.standalone
    path = _threadpool_path(inst)
    rundir = os.path.dirname(path)
    decoy = os.path.join(rundir, "threadpool-decoy")

    inst.stop()
    try:
        with open(decoy, "wb") as decoy_file:
            decoy_file.truncate(4096)

        if os.path.lexists(path):
            os.unlink(path)
        os.symlink(decoy, path)
        inst.start()
        if os.path.islink(path):
            # SELinux may deny ns-slapd unlinking a foreign-labeled symlink
            # (tclass=lnk_file). The server must fail safe: never follow the
            # symlink, disable the feature, and log a warning.
            result = _run_dsctl_threadpool(inst)
            assert result.returncode != 0
            assert "symlink" in _cmd_output(result).lower()
            assert inst.ds_error_log.match(".*Could not remove stale thread-pool status.*")
        else:
            assert stat.S_ISREG(os.stat(path).st_mode)

        inst.stop()
        if os.path.lexists(path):
            os.unlink(path)
        os.symlink(decoy, path)
        result = _run_dsctl_threadpool(inst)
        assert result.returncode != 0
        assert "symlink" in _cmd_output(result).lower()
    finally:
        if os.path.lexists(path):
            os.unlink(path)
        if os.path.exists(decoy):
            os.unlink(decoy)
        # Full restart either way: a failed-safe startup leaves the feature
        # disabled and would leak into the following tests.
        if inst.status():
            inst.restart()
        else:
            inst.start()


def test_monitor_attr_present(topo):
    """cn=monitor exposes sanitized threadpoolworker values

    :id: 5090ac9b-e865-48ee-a185-2f8f047f82ca
    :setup: Standalone instance
    :steps:
        1. Read threadpoolworker through lib389 Monitor.
        2. Validate the key=value format.
    :expectedresults:
        1. At least one value is returned.
        2. Values contain only worker, state, op, and duration_ns tokens.
    """
    values = Monitor(topo.standalone).get_thread_pool_workers()
    _assert_monitor_values_sanitized(values)


def test_monitor_attr_sanitized(topo):
    """Anonymous cn=monitor access exposes no connection or operation ids

    :id: a088e0b6-dcb5-43b0-ac1d-3b41bb393e95
    :setup: Standalone instance
    :steps:
        1. Bind anonymously.
        2. Read threadpoolworker from cn=monitor.
        3. Validate the sanitized format.
    :expectedresults:
        1. Anonymous bind succeeds.
        2. Values are returned.
        3. Values omit conn and op_id tokens.
    """
    anon = Anonymous(topo.standalone).bind()
    try:
        values = Monitor(anon).get_thread_pool_workers()
        _assert_monitor_values_sanitized(values)
    finally:
        anon.close()


def test_feature_disabled_by_config(topo):
    """nsslapd-thread-pool-stats: off disables the diagnostics after a restart

    :id: 867c1bae-6dd6-49d6-92dd-75804bc84510
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-thread-pool-stats to an invalid value.
        2. Set nsslapd-thread-pool-stats to off and restart.
        3. Check the mmap file, dsctl output, and cn=monitor.
        4. Set nsslapd-thread-pool-stats back to on and restart.
    :expectedresults:
        1. The invalid value is rejected.
        2. The instance restarts with the feature disabled.
        3. The file is absent, dsctl explains why, and threadpoolworker is gone.
        4. The feature is active again.
    """
    inst = topo.standalone
    path = _threadpool_path(inst)

    with pytest.raises(ldap.OPERATIONS_ERROR):
        inst.config.replace("nsslapd-thread-pool-stats", "maybe")

    try:
        inst.config.replace("nsslapd-thread-pool-stats", "off")
        inst.restart()

        assert not os.path.exists(path)
        result = _run_dsctl_threadpool(inst)
        assert result.returncode != 0
        assert "disabled by nsslapd-thread-pool-stats" in _cmd_output(result)
        assert not Monitor(inst).get_thread_pool_workers()
    finally:
        inst.config.replace("nsslapd-thread-pool-stats", "on")
        inst.restart()

    _wait_threadpool_file(inst)
    assert Monitor(inst).get_thread_pool_workers()


def test_invalid_file_rejected(topo):
    """dsctl refuses truncated and corrupted thread-pool status files

    :id: 421d1614-f57a-4775-afd8-45d3587cc923
    :setup: Standalone instance
    :steps:
        1. Stop the instance.
        2. Place a truncated file at the thread-pool mmap path and run dsctl.
        3. Place a file with a corrupted magic and run dsctl.
        4. Clean up and start the instance.
    :expectedresults:
        1. The instance is stopped.
        2. dsctl rejects the truncated file.
        3. dsctl rejects the corrupted magic.
        4. The instance is restored for later tests.
    """
    inst = topo.standalone
    path = _threadpool_path(inst)

    inst.stop()
    try:
        with open(path, "wb") as f:
            f.write(b"\x00" * 100)
        result = _run_dsctl_threadpool(inst)
        assert result.returncode != 0
        assert "too short" in _cmd_output(result).lower()

        with open(path, "wb") as f:
            f.write(b"\xff" * 8192)
        result = _run_dsctl_threadpool(inst)
        assert result.returncode != 0
        assert "magic" in _cmd_output(result).lower()
    finally:
        if os.path.exists(path):
            os.unlink(path)
        inst.start()


def test_stale_heartbeat_warning(topo):
    """dsctl warns when a live server stops updating the heartbeat

    :id: dcc1281d-c63a-4bbb-ab1d-ed5349e92858
    :setup: Standalone instance
    :steps:
        1. Read the server pid from dsctl JSON output.
        2. Stop the process with SIGSTOP and wait past the staleness threshold.
        3. Run dsctl thread-pool status.
        4. Resume the process with SIGCONT.
    :expectedresults:
        1. The pid is available.
        2. The heartbeat stops updating while the process stays alive.
        3. dsctl reports data with a stalled-server warning.
        4. The instance keeps running for later tests.
    """
    inst = topo.standalone
    _wait_threadpool_file(inst)
    data = _json_result(_run_dsctl_threadpool(inst, json_output=True))
    pid = data["pid"]

    os.kill(pid, signal.SIGSTOP)
    try:
        time.sleep(31)
        data = _json_result(_run_dsctl_threadpool(inst, json_output=True))
        assert any("may be stalled" in warning for warning in data["warnings"])
        assert data["heartbeat_age_sec"] > 30
    finally:
        os.kill(pid, signal.SIGCONT)


def test_crash_archive_created_after_kill(topo):
    """A crash leftover is preserved as a timestamped archive on the next start

    :id: e7259a2b-6286-4e71-8c05-7bce3c8c9ab2
    :setup: Standalone instance
    :steps:
        1. Remove existing archives and read the server pid from dsctl JSON output.
        2. Kill the server process and start the instance again.
        3. Check the live file, the archive count, and the errors log.
        4. Run dsctl thread-pool status against the running instance.
        5. Read the archive with dsctl thread-pool status --file.
    :expectedresults:
        1. The pid is available.
        2. The instance starts.
        3. The live file is recreated, one archive exists, and the preserved
           message is logged.
        4. The output warns that a crash archive is present.
        5. The archive reports the killed pid with a stale-file warning.
    """
    inst = topo.standalone
    _purge_archives(inst)
    _wait_threadpool_file(inst)
    data = _json_result(_run_dsctl_threadpool(inst, json_output=True))
    pid = data["pid"]

    try:
        os.kill(pid, signal.SIGKILL)
        deadline = time.time() + 10
        while time.time() < deadline and inst.status():
            time.sleep(0.2)
        inst.start()

        _wait_threadpool_file(inst)
        archives = _archive_paths(inst)
        assert len(archives) == 1
        assert inst.ds_error_log.match(".*thread-pool status preserved as.*")

        data = _json_result(_run_dsctl_threadpool(inst, json_output=True))
        assert any("crash archive" in warning for warning in data["warnings"])

        archive_data = _json_result(
            _run_dsctl_threadpool(inst, json_output=True, extra_args=["--file", archives[0]])
        )
        assert archive_data["pid"] == pid
        assert any("stale file" in warning for warning in archive_data["warnings"])
    finally:
        if not inst.status():
            inst.start()
        _purge_archives(inst)


def test_no_archive_after_clean_restart(topo):
    """A clean restart does not create a crash archive

    :id: c2bbb29f-1669-4cc6-9269-6a4e04559658
    :setup: Standalone instance
    :steps:
        1. Remove existing archives.
        2. Restart the instance.
        3. Check for archives.
    :expectedresults:
        1. No archives remain.
        2. The instance restarts.
        3. No archive was created.
    """
    inst = topo.standalone
    _purge_archives(inst)
    inst.restart()
    _wait_threadpool_file(inst)
    assert _archive_paths(inst) == []


def test_archive_pruned_to_five(topo):
    """Startup keeps at most five crash archives

    :id: d0a601d5-13ce-4114-8bb3-776bb20e65ff
    :setup: Standalone instance
    :steps:
        1. Stop the instance and remove existing archives.
        2. Plant seven dummy archives and a fabricated crash leftover at the
           live path, owned by the server user.
        3. Start the instance.
        4. Count the archives.
    :expectedresults:
        1. The instance is stopped.
        2. The files are in place.
        3. The instance starts and archives the leftover.
        4. Five archives remain: the four newest dummies plus the new one.
    """
    inst = topo.standalone
    path = _threadpool_path(inst)
    dummies = [f"{path}.20250101-00000{i}" for i in range(7)]

    inst.stop()
    _purge_archives(inst)
    try:
        rundir_st = os.stat(os.path.dirname(path))
        for dummy in dummies:
            with open(dummy, "wb") as f:
                f.write(b"\x00")

        # A crash leftover the server will archive: valid magic, unclean shutdown
        header = struct.pack(HEADER_FORMAT, TP_STATS_MAGIC, 1, 0,
                             TP_STATS_HEADER_SIZE, TP_STATS_WORKER_SLOT_SIZE, 1,
                             0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0)
        with open(path, "wb") as f:
            f.write(header.ljust(TP_STATS_HEADER_SIZE + TP_STATS_WORKER_SLOT_SIZE, b"\x00"))
        os.chown(path, rundir_st.st_uid, rundir_st.st_gid)

        inst.start()

        archives = [os.path.basename(archive) for archive in _archive_paths(inst)]
        assert len(archives) == 5
        for dummy in dummies[:3]:
            assert os.path.basename(dummy) not in archives
        for dummy in dummies[3:]:
            assert os.path.basename(dummy) in archives
    finally:
        if not inst.status():
            inst.start()
        _purge_archives(inst)


def test_dsctl_file_option(topo):
    """dsctl thread-pool status --file reads an explicit status file path

    :id: c68f4cf1-8e82-487c-84cf-2ca2108ed898
    :setup: Standalone instance
    :steps:
        1. Run dsctl thread-pool status --file with a nonexistent path.
        2. Run it with the live file path of the running instance.
    :expectedresults:
        1. The command fails with a not-found error.
        2. The command succeeds and reports the pool.
    """
    inst = topo.standalone
    path = _wait_threadpool_file(inst)

    result = _run_dsctl_threadpool(inst, extra_args=["--file", "/nonexistent/threadpool"])
    assert result.returncode != 0
    assert "not found" in _cmd_output(result).lower()

    data = _json_result(
        _run_dsctl_threadpool(inst, json_output=True, extra_args=["--file", path])
    )
    assert data["pool"]["max_workers"] >= 1


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
