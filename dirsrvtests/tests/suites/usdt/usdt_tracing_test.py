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
import subprocess
import threading

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts
from test389.topologies import topology_st as topo

from ._common import (
    ns_slapd_path, libslapd_path, binary_has_sdt_notes,
    _tail, _StderrReader, BPFTRACE_READY_MARKER,
    collect_usdt_diagnostics, run_workload_with_diagnostics,
    terminate_process_group,
)

DEBUGGING = os.getenv("DEBUGGING", default=False)
log = logging.getLogger(__name__)
log.setLevel(logging.DEBUG if DEBUGGING else logging.INFO)


_USDT_LIVE_ACK = os.environ.get('USDT_LIVE_ACK', '').lower() in ('1', 'true', 'yes')

pytestmark = [
    pytest.mark.tier2,
    pytest.mark.skipif(
        not _USDT_LIVE_ACK,
        reason="set USDT_LIVE_ACK=1 to run live bpftrace tests",
    ),
    pytest.mark.skipif(not shutil.which("bpftrace"),
                       reason="bpftrace not installed"),
    pytest.mark.skipif(not shutil.which("readelf"),
                       reason="readelf (binutils) is required"),
    pytest.mark.skipif(os.geteuid() != 0,
                       reason="bpftrace requires root (or CAP_PERFMON+CAP_BPF)"),
]


def _run_bpftrace(pid, program, drive_load=None, inst=None, label="inline bpftrace",
                  duration_s=10, ready_timeout=30.0, workload_timeout=None):
    """Attach bpftrace to pid, wait for the attach marker, drive load,
    return stdout. Program must not exit by itself.
    """

    full_program = f"{program} interval:s:{duration_s} {{ exit(); }}"
    log.debug("bpftrace program: %s", full_program)
    proc = subprocess.Popen(
        ["bpftrace", "-p", str(pid), "-e", full_program],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, bufsize=1, start_new_session=True,
    )
    log.info("started bpftrace pid=%s target_pid=%s label=%s",
             proc.pid, pid, label)
    reader = _StderrReader(proc, BPFTRACE_READY_MARKER)
    reader.start()

    try:
        if not reader.ready.wait(timeout=ready_timeout):
            diagnostics = collect_usdt_diagnostics(
                f"{label}: bpftrace did not attach",
                inst=inst, target_pid=pid, tracer_pid=proc.pid,
                tracer_stderr=reader.stderr_text,
            )
            terminate_process_group(proc)
            stdout, _ = proc.communicate()
            reader.join(timeout=2)
            pytest.fail(
                f"bpftrace did not attach within {ready_timeout}s.\n"
                f"stderr tail:\n{_tail(reader.stderr_text, 40)}\n"
                f"stdout:\n{stdout}\n{diagnostics}"
            )
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            reader.join(timeout=2)
            diagnostics = collect_usdt_diagnostics(
                f"{label}: bpftrace exited at attach",
                inst=inst, target_pid=pid, tracer_pid=proc.pid,
                tracer_stderr=reader.stderr_text,
                extra=f"returncode={proc.returncode}",
            )
            pytest.fail(
                f"bpftrace exited at attach with code {proc.returncode}.\n"
                f"stderr tail:\n{_tail(reader.stderr_text, 40)}\n"
                f"stdout:\n{stdout}\n{diagnostics}"
            )

        if drive_load is not None:
            log.info("bpftrace attached; driving workload label=%s", label)
            run_workload_with_diagnostics(
                drive_load, f"bpftrace {label}",
                inst=inst, target_pid=pid, tracer_pid=proc.pid,
                tracer_stderr_fn=lambda: reader.stderr_text,
                timeout=workload_timeout,
            )
            log.info("bpftrace workload finished label=%s", label)
        stdout, _ = proc.communicate(timeout=duration_s + 10)
    except subprocess.TimeoutExpired:
        diagnostics = collect_usdt_diagnostics(
            f"{label}: bpftrace did not exit on interval",
            inst=inst, target_pid=pid, tracer_pid=proc.pid,
            tracer_stderr=reader.stderr_text,
        )
        terminate_process_group(proc)
        stdout, _ = proc.communicate()
        reader.join(timeout=2)
        pytest.fail(
            f"bpftrace did not exit within timeout.\n"
            f"stderr tail:\n{_tail(reader.stderr_text, 40)}\n"
            f"stdout:\n{stdout}\n{diagnostics}"
        )
    except BaseException:
        terminate_process_group(proc)
        reader.join(timeout=2)
        raise

    reader.join(timeout=5)
    stderr = reader.stderr_text
    if stderr:
        log.info("bpftrace stderr:\n%s", stderr)
    if proc.returncode != 0:
        diagnostics = collect_usdt_diagnostics(
            f"{label}: bpftrace exited non-zero",
            inst=inst, target_pid=pid, tracer_pid=proc.pid,
            tracer_stderr=stderr,
            extra=f"returncode={proc.returncode}",
        )
        pytest.fail(
            f"bpftrace exited non-zero ({proc.returncode}). "
            f"stderr:\n{stderr}\nstdout:\n{stdout}\n{diagnostics}"
        )
    log.info("bpftrace exited rc=%s label=%s", proc.returncode, label)
    return stdout


_AT_SCALAR_RE = re.compile(r'^(@[A-Za-z_][A-Za-z0-9_]*)\s*:\s*(-?\d+)\s*$')


def _parse_at_scalars(stdout):
    """Parse `@name: N` lines into {name: int}."""

    out = {}
    for line in stdout.splitlines():
        m = _AT_SCALAR_RE.match(line.strip())
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


def _parse_at_int_map(stdout, name):
    """Parse `@<name>[k]: v` lines into {k: v}."""

    pattern = re.compile(rf'^@{re.escape(name)}\[(-?\d+)\]\s*:\s*(-?\d+)\s*$')
    out = {}
    for line in stdout.splitlines():
        m = pattern.match(line.strip())
        if m:
            out[int(m.group(1))] = int(m.group(2))
    return out


def _drive_searches(inst, n):
    for _ in range(n):
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(objectClass=*)")


@pytest.fixture(scope="module")
def usdt_topo(topo):
    """Standalone with deterministic logging and global work-queue behavior.

    Turbo mode can dispatch persistent connections without going through
    connection_wait_for_new_work(), so disable it for the work_q__* assertions.
    """

    binary = ns_slapd_path(topo)
    if not binary_has_sdt_notes(binary):
        pytest.skip("ns-slapd not built with --enable-usdt")

    inst = topo.standalone
    original_buffering = inst.config.get_attr_val_utf8('nsslapd-accesslog-logbuffering')
    original_turbo = inst.config.get_attr_val_utf8('nsslapd-enable-turbo-mode')
    inst.config.replace('nsslapd-accesslog-logbuffering', 'off')
    inst.config.replace('nsslapd-enable-turbo-mode', 'off')
    try:
        yield topo
    finally:
        if original_turbo is not None:
            inst.config.replace('nsslapd-enable-turbo-mode', original_turbo)
        if original_buffering is not None:
            inst.config.replace('nsslapd-accesslog-logbuffering', original_buffering)


@pytest.fixture
def workload_users(usdt_topo):
    """Five entries so subtree searches have something to traverse."""

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
        except Exception as e:
            log.warning("cleanup of %s failed: %s", getattr(u, 'dn', '?'), e)


def test_work_queue_probes_fire_under_load(usdt_topo, workload_users):
    """All four new work-queue/worker probes fire under search load.

    The module fixture disables turbo mode because turbo processing can bypass
    connection_wait_for_new_work(), where work_q__dequeue fires.

    :id: e1b9575f-fad2-47c3-8c52-33e265af1a3a
    :setup: Standalone instance
    :steps:
        1. Attach bpftrace counting each new probe
        2. Drive 200 searches
        3. Wait for the bpftrace interval to exit
    :expectedresults:
        1. bpftrace attaches without error
        2. Searches complete and fire probes
        3. work_q__enqueue, work_q__dequeue, worker__busy each fire > 0; worker__idle key present
    """

    inst = usdt_topo.standalone
    binary = ns_slapd_path(usdt_topo)
    program = (
        f'usdt:{binary}:work_q__enqueue {{ @enq = count(); }} '
        f'usdt:{binary}:work_q__dequeue {{ @deq = count(); }} '
        f'usdt:{binary}:worker__busy   {{ @bsy = count(); }} '
        f'usdt:{binary}:worker__idle   {{ @idl = count(); }} '
    )
    stdout = _run_bpftrace(
        inst.get_pid(), program,
        drive_load=lambda: _drive_searches(inst, 200),
        inst=inst,
        label="work_queue_probes_fire_under_load",
    )
    counts = _parse_at_scalars(stdout)
    log.info("Probe fire counts: %s", counts)

    assert counts.get("@enq", 0) > 0, f"work_q__enqueue did not fire: {counts}"
    assert counts.get("@deq", 0) > 0, f"work_q__dequeue did not fire: {counts}"
    assert counts.get("@bsy", 0) > 0, f"worker__busy did not fire: {counts}"
    assert "@idl" in counts, f"worker__idle key not in output: {counts}"


def test_enqueue_dequeue_counts_match(usdt_topo, workload_users):
    """Every enqueued op gets dequeued, within an attach-race tolerance.

    Turbo mode is disabled by the module fixture so this assertion tests the
    global work-queue enqueue/dequeue path.

    :id: 9e143e73-8c5b-42fc-bfda-0a3589aa4bdb
    :setup: Standalone instance
    :steps:
        1. Attach bpftrace counting work_q__enqueue and work_q__dequeue
        2. Drive 200 searches
    :expectedresults:
        1. Both counters fire > 0
        2. abs(enq - deq) <= max(5, 5% of larger)
    """

    inst = usdt_topo.standalone
    binary = ns_slapd_path(usdt_topo)
    program = (
        f'usdt:{binary}:work_q__enqueue {{ @enq = count(); }} '
        f'usdt:{binary}:work_q__dequeue {{ @deq = count(); }} '
    )
    stdout = _run_bpftrace(
        inst.get_pid(), program,
        drive_load=lambda: _drive_searches(inst, 200),
        inst=inst,
        label="enqueue_dequeue_counts_match",
    )
    counts = _parse_at_scalars(stdout)
    enq, deq = counts.get("@enq", 0), counts.get("@deq", 0)
    log.info("enqueue=%d dequeue=%d", enq, deq)

    assert enq > 0 and deq > 0, f"probes did not fire: {counts}"
    delta = abs(enq - deq)
    tolerance = max(5, int(0.05 * max(enq, deq)))
    assert delta <= tolerance, (
        f"enqueue/dequeue counts diverge: enq={enq} deq={deq} "
        f"delta={delta} tolerance={tolerance}"
    )


def test_worker_busy_count_tracks_dequeue(usdt_topo, workload_users):
    """worker__busy fires once per dispatched op.

    worker__busy is broader than work_q__dequeue when turbo mode is enabled;
    this test disables turbo so the two counters can be compared.

    :id: cfd89199-5ca3-4346-b448-96ff89da9eb3
    :setup: Standalone instance
    :steps:
        1. Attach bpftrace counting work_q__dequeue and worker__busy
        2. Drive 200 searches
    :expectedresults:
        1. Both counters fire > 0
        2. abs(deq - bsy) <= max(5, 10% of larger)
    """

    inst = usdt_topo.standalone
    binary = ns_slapd_path(usdt_topo)
    program = (
        f'usdt:{binary}:work_q__dequeue {{ @deq = count(); }} '
        f'usdt:{binary}:worker__busy   {{ @bsy = count(); }} '
    )
    stdout = _run_bpftrace(
        inst.get_pid(), program,
        drive_load=lambda: _drive_searches(inst, 200),
        inst=inst,
        label="worker_busy_count_tracks_dequeue",
    )
    counts = _parse_at_scalars(stdout)
    deq, bsy = counts.get("@deq", 0), counts.get("@bsy", 0)
    log.info("dequeue=%d busy=%d", deq, bsy)

    assert deq > 0 and bsy > 0, f"probes did not fire: {counts}"
    tolerance = max(5, int(0.10 * max(deq, bsy)))
    assert abs(deq - bsy) <= tolerance, (
        f"dequeue/busy counts diverge: deq={deq} bsy={bsy}"
    )


def test_probes_fire_for_all_op_types(usdt_topo):
    """work_q__enqueue fires for search/add/modify/delete operations.

    :id: 767dcd4c-a8a8-4a40-95a5-6d3b69a3758a
    :setup: Standalone instance
    :steps:
        1. Attach bpftrace counting work_q__enqueue
        2. Run 1 search + 1 add + 2 modifies + 1 delete (= 5 ops)
    :expectedresults:
        1. bpftrace attaches without error
        2. Counter is >= 5
    """

    inst = usdt_topo.standalone
    binary = ns_slapd_path(usdt_topo)

    def drive_mixed():
        users = UserAccounts(inst, DEFAULT_SUFFIX)
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE, "(objectClass=*)")
        try:
            u = users.create_test_user(uid=910001)
        except ldap.ALREADY_EXISTS:
            users.get("test_user_910001").delete()
            u = users.create_test_user(uid=910001)
        try:
            u.set("description", "usdt op-coverage test")
            u.replace("description", "usdt op-coverage test 2")
        finally:
            u.delete()

    program = f'usdt:{binary}:work_q__enqueue {{ @enq = count(); }} '
    stdout = _run_bpftrace(
        inst.get_pid(), program,
        drive_load=drive_mixed, inst=inst,
        label="probes_fire_for_all_op_types", duration_s=8,
    )
    counts = _parse_at_scalars(stdout)
    log.info("mixed-op enqueue count: %s", counts)
    assert counts.get("@enq", 0) >= 5, (
        f"work_q__enqueue should fire for every op type "
        f"(1 search + 1 add + 2 mod + 1 del = 5): {counts}"
    )


def test_worker_idle_thread_idx_in_range(usdt_topo, workload_users):
    """worker__idle thread_idx values fall within [1, nsslapd-threadnumber].

    :id: 740bb665-99a0-4f58-97de-f4f67a2d71a9
    :setup: Standalone instance
    :steps:
        1. Attach bpftrace recording per-thread-idx idle counts
        2. Drive 50 searches
    :expectedresults:
        1. At least one worker__idle event recorded
        2. All thread_idx values are in [1, nsslapd-threadnumber]
    """

    inst = usdt_topo.standalone
    threadnumber = int(inst.config.get_attr_val_utf8('nsslapd-threadnumber'))
    binary = ns_slapd_path(usdt_topo)

    program = f'usdt:{binary}:worker__idle {{ @idx[arg0] = count(); }} '
    stdout = _run_bpftrace(
        inst.get_pid(), program,
        drive_load=lambda: _drive_searches(inst, 50),
        inst=inst,
        label="worker_idle_thread_idx_in_range",
    )
    idx_counts = _parse_at_int_map(stdout, "idx")
    log.info("worker__idle thread_idx histogram: %s", idx_counts)

    assert idx_counts, "no worker__idle events recorded"
    out_of_range = sorted(i for i in idx_counts if i < 1 or i > threadnumber)
    assert not out_of_range, (
        f"worker__idle reported out-of-range thread_idx values: "
        f"{out_of_range} (expected 1..{threadnumber})"
    )


def test_enqueue_depth_argument_recorded(usdt_topo, workload_users):
    """work_q__enqueue queue-depth argument is captured and non-negative.

    :id: f89eadef-fe08-4a9b-8e16-b0ce12df2f3e
    :setup: Standalone instance
    :steps:
        1. Attach bpftrace recording max/min queue depth from work_q__enqueue
        2. Run 8 concurrent threads each driving 30 searches
    :expectedresults:
        1. @max captured and >= 1
        2. @min captured and >= 0
    """

    inst = usdt_topo.standalone
    binary = ns_slapd_path(usdt_topo)

    def drive_burst():
        errors = []

        def one_thread():
            try:
                _drive_searches(inst, 30)
            except BaseException as e:
                errors.append(e)

        threads = [threading.Thread(target=one_thread) for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        if errors:
            raise errors[0]

    program = (
        f'usdt:{binary}:work_q__enqueue '
        f'{{ @max = max(arg2); @min = min(arg2); }} '
    )
    stdout = _run_bpftrace(
        inst.get_pid(), program,
        drive_load=drive_burst,
        inst=inst,
        label="enqueue_depth_argument_recorded", duration_s=8,
    )
    log.info("burst depth output:\n%s", stdout)

    vals = _parse_at_scalars(stdout)
    assert "@max" in vals, f"depth max not recorded: {stdout}"
    assert vals["@max"] >= 1, f"max queue depth implausibly low: {vals}"
    assert vals.get("@min", 0) >= 0, (
        f"min queue depth negative (signed/unsigned bug?): {vals}"
    )


def test_existing_probes_still_fire(usdt_topo, workload_users):
    """Regression: do_search, op_shared_search, vslapd_log_access still fire.

    :id: b8377e4c-a9fb-4a4d-9293-9c4f8be9db95
    :setup: Standalone instance
    :steps:
        1. Attach bpftrace to do_search__entry, op_shared_search__entry, vslapd_log_access__entry
        2. Drive 50 searches
    :expectedresults:
        1. bpftrace attaches to all three probes
        2. All three counters fire > 0
    """

    inst = usdt_topo.standalone
    binary = ns_slapd_path(usdt_topo)
    libslapd = libslapd_path(usdt_topo)
    if not libslapd:
        pytest.skip("libslapd.so not located")

    program = (
        f'usdt:{binary}:do_search__entry           {{ @ds  = count(); }} '
        f'usdt:{libslapd}:op_shared_search__entry  {{ @oss = count(); }} '
        f'usdt:{libslapd}:vslapd_log_access__entry {{ @log = count(); }} '
    )
    stdout = _run_bpftrace(
        inst.get_pid(), program,
        drive_load=lambda: _drive_searches(inst, 50),
        inst=inst,
        label="existing_probes_still_fire",
    )
    counts = _parse_at_scalars(stdout)
    log.info("existing-probe counts: %s", counts)

    assert counts.get("@ds", 0) > 0,  f"do_search__entry did not fire: {counts}"
    assert counts.get("@oss", 0) > 0, f"op_shared_search__entry did not fire: {counts}"
    assert counts.get("@log", 0) > 0, f"vslapd_log_access__entry did not fire: {counts}"


def test_probe_connid_matches_access_log(usdt_topo, workload_users):
    """work_q__enqueue (connid, opid) pairs match the access log.

    :id: 20915cf0-3591-4008-a7fc-8a68cc7a0536
    :setup: Standalone instance
    :steps:
        1. Attach bpftrace printing (connid, opid) for work_q__enqueue
        2. Drive 5 searches
        3. Read the access log
        4. Cross-check probe pairs against conn=N op=M entries
    :expectedresults:
        1. bpftrace attaches without error
        2. Probe prints at least one (connid, opid) pair
        3. Access log contains conn=N op=M entries
        4. At least one probe pair matches an access-log entry
    """

    inst = usdt_topo.standalone
    binary = ns_slapd_path(usdt_topo)

    program = (
        f'usdt:{binary}:work_q__enqueue '
        f'{{ printf("ENQ %d %d\\n", arg0, arg1); }} '
    )
    stdout = _run_bpftrace(
        inst.get_pid(), program,
        drive_load=lambda: _drive_searches(inst, 5),
        inst=inst,
        label="probe_connid_matches_access_log",
        duration_s=5,
    )

    probe_pairs = set()
    for line in stdout.splitlines():
        parts = line.strip().split()
        if len(parts) == 3 and parts[0] == "ENQ":
            try:
                probe_pairs.add((int(parts[1]), int(parts[2])))
            except ValueError:
                continue
    log.info("captured %d (connid, opid) pairs from probe", len(probe_pairs))
    assert probe_pairs, "no probe pairs captured"

    log_path = inst.ds_paths.access_log
    with open(log_path, encoding="utf-8") as fh:
        access_lines = fh.readlines()

    pattern = re.compile(r'\bconn=(\d+)\s+op=(\d+)')
    log_pairs = set()
    for line in access_lines:
        m = pattern.search(line)
        if m:
            log_pairs.add((int(m.group(1)), int(m.group(2))))

    overlap = probe_pairs & log_pairs
    log.info("overlap with access log: %d/%d probe pairs",
             len(overlap), len(probe_pairs))
    assert overlap, (
        "no probe-emitted (connid, opid) pair matches the access log. "
        f"probe sample: {sorted(probe_pairs)[:5]} "
        f"log sample: {sorted(log_pairs)[-5:]}"
    )


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
