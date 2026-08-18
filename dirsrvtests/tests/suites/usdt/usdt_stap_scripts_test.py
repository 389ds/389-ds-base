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
    collect_usdt_diagnostics, run_workload_with_diagnostics,
    terminate_process_group,
)

DEBUGGING = os.getenv("DEBUGGING", default=False)
log = logging.getLogger(__name__)
log.setLevel(logging.DEBUG if DEBUGGING else logging.INFO)


_USDT_LIVE_ACK = os.environ.get('USDT_LIVE_ACK', '').lower() in ('1', 'true', 'yes')

pytestmark = [
    pytest.mark.tier2,
    pytest.mark.skipif(not _USDT_LIVE_ACK,
                       reason="set USDT_LIVE_ACK=1 to run live stap tests"),
    pytest.mark.skipif(not shutil.which("stap"),
                       reason="systemtap (stap) is not installed"),
    pytest.mark.skipif(not shutil.which("readelf"),
                       reason="readelf (binutils) is required"),
    pytest.mark.skipif(os.geteuid() != 0,
                       reason="stap requires root"),
]

PROFILING_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__),
                 "..", "..", "..", "..", "profiling", "stap")
)

_STAP_READY_MARKER = "Pass 5: starting run"

# Matches samples=N (work-queue) and for N samples (latency scripts).
_SAMPLES_RE = re.compile(r'samples=(\d+)|for\s+(\d+)\s+samples?\b')


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


class _StapStderrReader(_StderrReader):
    """Drain stap stderr; signal once the pass-5 marker is seen."""

    def __init__(self, proc):
        super().__init__(proc, _STAP_READY_MARKER)


def _run_stap_script(script_path, args, drive_load, inst=None,
                     ready_timeout=60.0, drain_wait=1.5, exit_timeout=30.0,
                     workload_timeout=None):
    """Spawn stap, drive load after pass 5, SIGINT, return (stdout, stderr, rc)."""

    cmd = ["stap", "-v", script_path, *args]
    label = os.path.basename(script_path)
    target_pid = inst.get_pid() if inst is not None else None
    log.info("running: %s", " ".join(cmd))
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, bufsize=1, start_new_session=True,
    )
    log.info("started stap pid=%s target_pid=%s script=%s",
             proc.pid, target_pid, label)
    reader = _StapStderrReader(proc)
    reader.start()

    try:
        if not reader.ready.wait(timeout=ready_timeout):
            diagnostics = collect_usdt_diagnostics(
                f"{label}: stap did not reach pass 5",
                inst=inst, target_pid=target_pid, tracer_pid=proc.pid,
                tracer_stderr=reader.stderr_text,
            )
            terminate_process_group(proc)
            stdout, _ = proc.communicate()
            reader.join(timeout=2)
            pytest.fail(
                f"stap did not reach pass 5 within {ready_timeout}s.\n"
                f"stderr tail:\n{_tail(reader.stderr_text, 40)}\n"
                f"stdout:\n{stdout}\n{diagnostics}"
            )
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            reader.join(timeout=2)
            diagnostics = collect_usdt_diagnostics(
                f"{label}: stap exited at pass 5",
                inst=inst, target_pid=target_pid, tracer_pid=proc.pid,
                tracer_stderr=reader.stderr_text,
                extra=f"returncode={proc.returncode}",
            )
            pytest.fail(
                f"stap exited at pass 5 with code {proc.returncode}.\n"
                f"stderr tail:\n{_tail(reader.stderr_text, 40)}\n"
                f"stdout:\n{stdout}\n{diagnostics}"
            )

        log.info("stap reached pass 5; driving workload script=%s", label)
        run_workload_with_diagnostics(
            drive_load, f"stap {label}",
            inst=inst, target_pid=target_pid, tracer_pid=proc.pid,
            tracer_stderr_fn=lambda: reader.stderr_text,
            timeout=workload_timeout,
        )
        log.info("stap workload finished script=%s", label)
        time.sleep(drain_wait)

        log.info("sending SIGINT to stap pid=%s script=%s", proc.pid, label)
        if proc.poll() is None:
            try:
                proc.send_signal(signal.SIGINT)
            except ProcessLookupError:
                pass
        stdout, _ = proc.communicate(timeout=exit_timeout)
    except subprocess.TimeoutExpired:
        diagnostics = collect_usdt_diagnostics(
            f"{label}: stap did not exit after SIGINT",
            inst=inst, target_pid=target_pid, tracer_pid=proc.pid,
            tracer_stderr=reader.stderr_text,
        )
        terminate_process_group(proc)
        stdout, _ = proc.communicate()
        reader.join(timeout=2)
        pytest.fail(
            f"stap did not exit within {exit_timeout}s after SIGINT.\n"
            f"stderr tail:\n{_tail(reader.stderr_text, 40)}\n"
            f"stdout:\n{stdout}\n{diagnostics}"
        )
    except BaseException:
        terminate_process_group(proc)
        reader.join(timeout=2)
        raise

    reader.join(timeout=5)
    log.info("stap exited rc=%s script=%s", proc.returncode, label)
    return stdout, reader.stderr_text, proc.returncode


def _sample_count_for_label(stdout, label_substr):
    """Sample count from the first line containing label_substr, or None."""

    for line in stdout.splitlines():
        if label_substr not in line:
            continue
        m = _SAMPLES_RE.search(line)
        if m:
            return int(m.group(1) or m.group(2))
        return None
    return None


def _assert_distributions(stdout, labels):
    missing_lines = [l for l in labels if l not in stdout]
    assert not missing_lines, (
        f"missing report lines for: {missing_lines}\nstdout:\n{stdout}"
    )
    zero_or_unparsed = [
        (l, _sample_count_for_label(stdout, l)) for l in labels
        if (_sample_count_for_label(stdout, l) or 0) <= 0
    ]
    assert not zero_or_unparsed, (
        f"distributions with zero or unparseable sample counts:\n  " +
        "\n  ".join(f"{l}: samples={n}" for l, n in zero_or_unparsed) +
        f"\n\nstdout:\n{stdout}"
    )


def test_probe_work_queue_stp(usdt_topo, workload_users):
    """probe_work_queue.stp populates queue-depth, wait-latency, idle-counts under load.

    :id: ad85076b-2f04-45d1-88f3-910ba44e4662
    :setup: Standalone instance
    :steps:
        1. Run probe_work_queue.stp against ns-slapd
        2. Drive 100 searches over 10 concurrent fresh connections
        3. SIGINT and capture report()
    :expectedresults:
        1. stap exits cleanly
        2. queue-depth and wait-latency distributions have samples > 0
        3. Worker idle counts section appears with per-thread lines
    """

    inst = usdt_topo.standalone
    script = os.path.join(PROFILING_DIR, "probe_work_queue.stp")
    stdout, stderr, rc = _run_stap_script(
        script, [ns_slapd_path(usdt_topo)],
        drive_load=lambda: _drive_searches_concurrent(inst, n=100, parallel=10),
        inst=inst,
    )
    log.debug("stdout:\n%s", stdout)
    assert rc == 0, f"stap exited {rc}\nstderr tail:\n{_tail(stderr, 40)}"

    _assert_distributions(stdout, [
        "Distribution of work-queue depth at enqueue time",
        "Distribution of enqueue-to-dequeue wait latencies",
    ])
    assert "Worker idle counts" in stdout, (
        f"missing 'Worker idle counts' section.\nstdout:\n{stdout}"
    )
    assert re.search(r'thread\s+\d+:\s+\d+\s+idle waits', stdout), (
        f"no per-thread idle counts reported.\nstdout:\n{stdout}"
    )


def test_probe_do_search_detail_stp(usdt_topo, workload_users):
    """probe_do_search_detail.stp aggregates four search-phase latencies.

    :id: f81d77a6-a459-4d73-ad74-7598337dce7b
    :setup: Standalone instance
    :steps:
        1. Run probe_do_search_detail.stp with @1=ns-slapd @2=libslapd.so
        2. Drive 100 searches
        3. SIGINT and parse report()
    :expectedresults:
        1. stap reaches pass 5 (probes armed)
        2. Searches complete and fire probes
        3. All four search-phase distributions report samples > 0; stap exits 0
    """

    inst = usdt_topo.standalone
    script = os.path.join(PROFILING_DIR, "probe_do_search_detail.stp")
    stdout, stderr, rc = _run_stap_script(
        script,
        [ns_slapd_path(usdt_topo), libslapd_path(usdt_topo)],
        drive_load=lambda: _drive_searches(inst, 100),
        inst=inst,
    )
    log.debug("stdout:\n%s", stdout)
    assert rc == 0, f"stap exited {rc}\nstderr tail:\n{_tail(stderr, 40)}"

    _assert_distributions(stdout, [
        "Distribution of do_search_full",
        "Distribution of do_search_prepared",
        "Distribution of do_search_complete",
        "Distribution of do_search_finalise",
    ])


def test_probe_op_shared_search_stp(usdt_topo, workload_users):
    """probe_op_shared_search.stp aggregates four phases of op_shared_search().

    :id: d7a5125a-2ee1-4a57-a6aa-a5da925511ac
    :setup: Standalone instance
    :steps:
        1. Run probe_op_shared_search.stp against libslapd.so
        2. Drive 100 searches
        3. SIGINT and parse report()
    :expectedresults:
        1. stap reaches pass 5 (probes armed)
        2. Searches complete and fire probes
        3. All four phase distributions report samples > 0; stap exits 0
    """

    inst = usdt_topo.standalone
    script = os.path.join(PROFILING_DIR, "probe_op_shared_search.stp")
    stdout, stderr, rc = _run_stap_script(
        script, [libslapd_path(usdt_topo)],
        drive_load=lambda: _drive_searches(inst, 100),
        inst=inst,
    )
    log.debug("stdout:\n%s", stdout)
    assert rc == 0, f"stap exited {rc}\nstderr tail:\n{_tail(stderr, 40)}"

    _assert_distributions(stdout, [
        "Distribution of op_shared_search_full",
        "Distribution of op_shared_search_prepared",
        "Distribution of op_shared_search_complete",
        "Distribution of op_shared_search_finalise",
    ])


def test_probe_log_access_detail_stp(usdt_topo, workload_users):
    """probe_log_access_detail.stp aggregates three access-log write phases.

    :id: 828710ad-8784-43aa-a3db-58fef6a1aba1
    :setup: Standalone instance
    :steps:
        1. Run probe_log_access_detail.stp against libslapd.so
        2. Drive 100 searches
        3. SIGINT and parse report()
    :expectedresults:
        1. stap reaches pass 5 (probes armed)
        2. Searches complete and fire probes
        3. All three log-phase distributions report samples > 0; stap exits 0
    """

    inst = usdt_topo.standalone
    script = os.path.join(PROFILING_DIR, "probe_log_access_detail.stp")
    stdout, stderr, rc = _run_stap_script(
        script, [libslapd_path(usdt_topo)],
        drive_load=lambda: _drive_searches(inst, 100),
        inst=inst,
    )
    log.debug("stdout:\n%s", stdout)
    assert rc == 0, f"stap exited {rc}\nstderr tail:\n{_tail(stderr, 40)}"

    _assert_distributions(stdout, [
        "Distribution of log_access_full",
        "Distribution of log_access_prepared",
        "Distribution of log_access_complete",
    ])


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
