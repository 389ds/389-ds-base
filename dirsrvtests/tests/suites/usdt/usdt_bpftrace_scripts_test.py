# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import re
import shutil
import signal
import subprocess
import time

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts
from test389.topologies import topology_st as topo

from ._common import (
    ns_slapd_path, libslapd_path, binary_has_sdt_notes,
    _tail, _drive_searches, _drive_searches_concurrent, _StderrReader,
    BPFTRACE_READY_MARKER,
)

DEBUGGING = os.getenv("DEBUGGING", default=False)
log = logging.getLogger(__name__)
log.setLevel(logging.DEBUG if DEBUGGING else logging.INFO)


_USDT_LIVE_ACK = os.environ.get('USDT_LIVE_ACK', '').lower() in ('1', 'true', 'yes')

pytestmark = [
    pytest.mark.tier2,
    pytest.mark.skipif(not _USDT_LIVE_ACK,
                       reason="set USDT_LIVE_ACK=1 to run live bpftrace tests"),
    pytest.mark.skipif(not shutil.which("bpftrace"),
                       reason="bpftrace is not installed"),
    pytest.mark.skipif(not shutil.which("readelf"),
                       reason="readelf (binutils) is required"),
    pytest.mark.skipif(os.geteuid() != 0,
                       reason="bpftrace requires root"),
]

PROFILING_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__),
                 "..", "..", "..", "..", "profiling", "bpftrace")
)

# Histogram bucket line, e.g. "[1, 2)        42 |@@@@@@@@@@@..."
# Also matches single-value bracket form like "[0]   ..." for the lowest bucket.
_HIST_BUCKET_RE = re.compile(r'^\s*\[\S+(?:,\s*\S+)?[\)\]]\s+(\d+)\s+\|')


@pytest.fixture(scope="module")
def usdt_topo(topo):
    binary = ns_slapd_path(topo)
    if not binary_has_sdt_notes(binary):
        pytest.skip("ns-slapd not built with --enable-usdt")
    if not libslapd_path(topo):
        pytest.skip("libslapd.so not located under the instance prefix")
    return topo


@pytest.fixture
def workload_users(usdt_topo):
    inst = usdt_topo.standalone
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    created = []
    for i in range(5):
        try:
            created.append(users.create_test_user(uid=900000 + i))
        except ldap.ALREADY_EXISTS:
            created.append(users.get(f"test_user_{900000 + i}"))
    yield created
    for u in created:
        try:
            u.delete()
        except ldap.NO_SUCH_OBJECT:
            pass


def _run_bpftrace_script(script_path, binary_args, drive_load,
                         ready_timeout=60.0, drain_wait=1.5, exit_timeout=30.0):
    """Spawn bpftrace, drive load after attach, SIGINT, return (stdout, stderr, rc)."""

    cmd = ["bpftrace", script_path, *binary_args]
    log.info("running: %s", " ".join(cmd))
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, bufsize=1,
    )
    reader = _StderrReader(proc, BPFTRACE_READY_MARKER)
    reader.start()

    try:
        if not reader.ready.wait(timeout=ready_timeout):
            proc.kill()
            stdout, _ = proc.communicate()
            reader.join(timeout=2)
            pytest.fail(
                f"bpftrace did not attach within {ready_timeout}s.\n"
                f"stderr tail:\n{_tail(reader.stderr_text, 40)}"
            )
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            reader.join(timeout=2)
            pytest.fail(
                f"bpftrace exited at attach with code {proc.returncode}.\n"
                f"stderr tail:\n{_tail(reader.stderr_text, 40)}\n"
                f"stdout:\n{stdout}"
            )

        log.debug("bpftrace attached; driving workload")
        drive_load()
        time.sleep(drain_wait)

        log.debug("sending SIGINT to flush bpftrace maps")
        proc.send_signal(signal.SIGINT)
        stdout, _ = proc.communicate(timeout=exit_timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, _ = proc.communicate()
        reader.join(timeout=2)
        pytest.fail(
            f"bpftrace did not exit within {exit_timeout}s after SIGINT.\n"
            f"stderr tail:\n{_tail(reader.stderr_text, 40)}"
        )
    except BaseException:
        proc.kill()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
        reader.join(timeout=2)
        raise

    reader.join(timeout=5)
    return stdout, reader.stderr_text, proc.returncode


def _nonzero_buckets_for_hist(stdout, hist_name):
    """Count buckets with count > 0 under `@<hist_name>:` header. Scan until
    the next blank line or next `@<name>:` header.
    """

    header = f"@{hist_name}:"
    in_block = False
    count = 0
    for line in stdout.splitlines():
        stripped = line.rstrip()
        if not in_block:
            if stripped == header or stripped.startswith(header):
                in_block = True
            continue
        if not stripped:
            break
        # next map starts a new block; stop counting.
        if stripped.startswith("@") and ":" in stripped:
            break
        m = _HIST_BUCKET_RE.match(line)
        if m and int(m.group(1)) > 0:
            count += 1
    return count


def _nonzero_keyed_map_entries(stdout, name):
    """Count `@<name>[k]: v` lines where v > 0."""
    pattern = re.compile(rf'^@{re.escape(name)}\[[^\]]+\]:\s+(\d+)\s*$')
    return sum(
        1 for line in stdout.splitlines()
        if (m := pattern.match(line.strip())) and int(m.group(1)) > 0
    )


def _assert_hist_present(stdout, hist_names):
    """Each name must have a `@<name>:` header in stdout with at least one
    non-zero bucket below it.
    """

    missing = [n for n in hist_names if f"@{n}:" not in stdout]
    assert not missing, (
        f"missing histogram header(s): {missing}\nstdout:\n{stdout}"
    )
    empty = [(n, _nonzero_buckets_for_hist(stdout, n)) for n in hist_names]
    bad = [(n, c) for n, c in empty if c < 1]
    assert not bad, (
        "histograms with no non-zero buckets:\n  " +
        "\n  ".join(f"@{n}: nonzero_buckets={c}" for n, c in bad) +
        f"\n\nstdout:\n{stdout}"
    )


def test_probe_work_queue_bt(usdt_topo, workload_users):
    """probe_work_queue.bt produces queue-depth, wait-latency, idle-counts maps under load.

    :id: 937d039a-95b0-4fe5-bebf-ee4d703f509a
    :setup: Standalone instance
    :steps:
        1. Run probe_work_queue.bt against ns-slapd
        2. Drive 100 searches over 10 concurrent fresh connections
        3. SIGINT and capture default-print
    :expectedresults:
        1. bpftrace exits 0
        2. @queue_depth and @wait_us histograms have at least one non-zero bucket
        3. @idle_counts has at least one keyed entry with count > 0
    """

    inst = usdt_topo.standalone
    script = os.path.join(PROFILING_DIR, "probe_work_queue.bt")
    stdout, stderr, rc = _run_bpftrace_script(
        script, [ns_slapd_path(usdt_topo)],
        drive_load=lambda: _drive_searches_concurrent(inst, n=100, parallel=10),
    )
    log.debug("stdout:\n%s", stdout)
    assert rc == 0, f"bpftrace exited {rc}\nstderr tail:\n{_tail(stderr, 40)}"

    _assert_hist_present(stdout, ["queue_depth", "wait_us"])

    idle = _nonzero_keyed_map_entries(stdout, "idle_counts")
    assert idle >= 1, (
        f"@idle_counts has no non-zero keyed entries.\nstdout:\n{stdout}"
    )


def test_probe_do_search_detail_bt(usdt_topo, workload_users):
    """probe_do_search_detail.bt aggregates four search-phase latencies.

    :id: 22f219c9-a4a9-4312-aa31-359878da7bb4
    :setup: Standalone instance
    :steps:
        1. Run probe_do_search_detail.bt with $1=ns-slapd $2=libslapd.so
        2. Drive 100 searches
        3. SIGINT and capture default-print
    :expectedresults:
        1. bpftrace exits 0
        2. All four @do_search_<phase> histograms have at least one non-zero bucket
    """

    inst = usdt_topo.standalone
    script = os.path.join(PROFILING_DIR, "probe_do_search_detail.bt")
    stdout, stderr, rc = _run_bpftrace_script(
        script,
        [ns_slapd_path(usdt_topo), libslapd_path(usdt_topo)],
        drive_load=lambda: _drive_searches(inst, 100),
    )
    log.debug("stdout:\n%s", stdout)
    assert rc == 0, f"bpftrace exited {rc}\nstderr tail:\n{_tail(stderr, 40)}"

    _assert_hist_present(stdout, [
        "do_search_full",
        "do_search_prepared",
        "do_search_complete",
        "do_search_finalise",
    ])


def test_probe_op_shared_search_bt(usdt_topo, workload_users):
    """probe_op_shared_search.bt aggregates four phases of op_shared_search().

    :id: 3ad8af11-190b-4c06-bfab-e15daacef432
    :setup: Standalone instance
    :steps:
        1. Run probe_op_shared_search.bt against libslapd.so
        2. Drive 100 searches
        3. SIGINT and capture default-print
    :expectedresults:
        1. bpftrace exits 0
        2. All four @op_shared_search_<phase> histograms have at least one non-zero bucket
    """

    inst = usdt_topo.standalone
    script = os.path.join(PROFILING_DIR, "probe_op_shared_search.bt")
    stdout, stderr, rc = _run_bpftrace_script(
        script, [libslapd_path(usdt_topo)],
        drive_load=lambda: _drive_searches(inst, 100),
    )
    log.debug("stdout:\n%s", stdout)
    assert rc == 0, f"bpftrace exited {rc}\nstderr tail:\n{_tail(stderr, 40)}"

    _assert_hist_present(stdout, [
        "op_shared_search_full",
        "op_shared_search_prepared",
        "op_shared_search_complete",
        "op_shared_search_finalise",
    ])


def test_probe_log_access_detail_bt(usdt_topo, workload_users):
    """probe_log_access_detail.bt aggregates three access-log write phases.

    :id: 5af6d170-2674-4480-b4f5-a60802a07c08
    :setup: Standalone instance
    :steps:
        1. Run probe_log_access_detail.bt against libslapd.so
        2. Drive 100 searches
        3. SIGINT and capture default-print
    :expectedresults:
        1. bpftrace exits 0
        2. All three @log_access_<phase> histograms have at least one non-zero bucket
    """

    inst = usdt_topo.standalone
    script = os.path.join(PROFILING_DIR, "probe_log_access_detail.bt")
    stdout, stderr, rc = _run_bpftrace_script(
        script, [libslapd_path(usdt_topo)],
        drive_load=lambda: _drive_searches(inst, 100),
    )
    log.debug("stdout:\n%s", stdout)
    assert rc == 0, f"bpftrace exited {rc}\nstderr tail:\n{_tail(stderr, 40)}"

    _assert_hist_present(stdout, [
        "log_access_full",
        "log_access_prepared",
        "log_access_complete",
    ])


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
