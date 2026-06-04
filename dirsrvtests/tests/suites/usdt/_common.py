# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import collections
import concurrent.futures
import glob
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

import ldap

from lib389._constants import DEFAULT_SUFFIX, DN_DM, PW_DM


# bpftrace >= 0.16 prints "Attached N probes"; older versions print
# "Attaching N probes...". Match both, on either stderr or stdout.
BPFTRACE_READY_MARKER = re.compile(r'Attach(?:ing|ed)\s+\d+\s+probe')


def ns_slapd_path(topo):
    return os.path.join(topo.standalone.ds_paths.sbin_dir, "ns-slapd")


def libslapd_path(topo):
    candidates = []
    for stem in ("libslapd.so", "libslapd.so.*"):
        candidates.extend(
            glob.glob(os.path.join(topo.standalone.ds_paths.lib_dir, "dirsrv", stem))
        )
        candidates.extend(
            glob.glob(os.path.join(topo.standalone.ds_paths.lib_dir, stem))
        )
    concrete = [p for p in candidates if not os.path.islink(p)]
    return (concrete or candidates or [None])[0]


def binary_has_sdt_notes(binary):
    """True if `binary` contains stapsdt notes. Returns False when `readelf`
    is not on PATH so callers can use this in a fixture without crashing
    on hosts without binutils; gate the test module with a separate
    `shutil.which('readelf')` skipif for a clear skip reason.
    """

    try:
        out = subprocess.run(
            ["readelf", "-n", binary],
            capture_output=True, text=True, check=True,
        ).stdout
    except FileNotFoundError:
        return False
    return "stapsdt" in out


def _tail(text, n):
    return "\n".join(text.splitlines()[-n:])


def _read_float_env(name, default):
    value = os.environ.get(name)
    if not value:
        return default
    try:
        return float(value)
    except ValueError:
        return default


def _read_bool_env(name):
    return os.environ.get(name, "").lower() in ("1", "true", "yes", "on")


USDT_DIAG_TIMEOUT = _read_float_env("USDT_DIAG_TIMEOUT", 120.0)
USDT_DIAG_DEEP = _read_bool_env("USDT_DIAG_DEEP")
USDT_DIAG_LOGS = _read_bool_env("USDT_DIAG_LOGS")


def _clip(text, max_chars=12000):
    if text is None:
        return ""
    if len(text) <= max_chars:
        return text
    omitted = len(text) - max_chars
    return f"{text[:max_chars]}\n...[truncated {omitted} chars]\n"


def _run_diag_cmd(cmd, timeout=8, max_chars=12000):
    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
            timeout=timeout,
        )
    except FileNotFoundError as e:
        return f"{cmd[0]} not found: {e}\n"
    except subprocess.TimeoutExpired as e:
        stdout = e.stdout or ""
        stderr = e.stderr or ""
        return _clip(
            f"timed out after {timeout}s\nstdout:\n{stdout}\nstderr:\n{stderr}",
            max_chars,
        )
    except Exception as e:
        return f"failed to run {cmd!r}: {e}\n"

    out = ""
    if proc.stdout:
        out += proc.stdout
    if proc.stderr:
        out += "\nSTDERR:\n" + proc.stderr
    if proc.returncode != 0:
        out += f"\n(exit code {proc.returncode})\n"
    return _clip(out, max_chars)


def _read_file(path, max_chars=12000):
    if not path:
        return "path unavailable\n"
    try:
        with open(path, errors="replace") as fh:
            return _clip(fh.read(), max_chars)
    except FileNotFoundError:
        return f"{path} not found\n"
    except PermissionError as e:
        return f"permission denied reading {path}: {e}\n"
    except OSError as e:
        return f"failed reading {path}: {e}\n"


def _tail_file(path, lines=80, max_chars=12000):
    if not path:
        return "path unavailable\n"
    try:
        with open(path, errors="replace") as fh:
            tail = collections.deque(fh, maxlen=lines)
        return _clip("".join(tail), max_chars)
    except FileNotFoundError:
        return f"{path} not found\n"
    except PermissionError as e:
        return f"permission denied reading {path}: {e}\n"
    except OSError as e:
        return f"failed reading {path}: {e}\n"


def _tracefs_file(name):
    for base in ("/sys/kernel/tracing", "/sys/kernel/debug/tracing"):
        path = os.path.join(base, name)
        if os.path.exists(path):
            return path
    return None


def _proc_text(pid, name, max_chars=12000):
    return _read_file(os.path.join("/proc", str(pid), name), max_chars=max_chars)


def _thread_state(pid, max_threads=32, include_stack=False):
    task_dir = os.path.join("/proc", str(pid), "task")
    try:
        tids = sorted(os.listdir(task_dir), key=int)
    except OSError as e:
        return f"failed reading {task_dir}: {e}\n"

    out = []
    for tid in tids[:max_threads]:
        out.append(f"--- tid {tid} status ---")
        out.append(_read_file(os.path.join(task_dir, tid, "status"), max_chars=4000))
        out.append(f"--- tid {tid} wchan ---")
        out.append(_read_file(os.path.join(task_dir, tid, "wchan"), max_chars=1000))
        if include_stack:
            out.append(f"--- tid {tid} kernel stack ---")
            out.append(_read_file(os.path.join(task_dir, tid, "stack"), max_chars=4000))
    if len(tids) > max_threads:
        out.append(f"...skipped {len(tids) - max_threads} more threads")
    return _clip("\n".join(out), 40000)


def _gdb_backtrace(pid, timeout=20):
    if not pid:
        return "pid unavailable\n"
    if not shutil.which("gdb"):
        return "gdb not installed\n"
    return _run_diag_cmd(
        [
            "gdb", "-q", "-batch",
            "-ex", "set pagination off",
            "-ex", "thread apply all bt",
            "-p", str(pid),
        ],
        timeout=timeout,
        max_chars=50000,
    )


def collect_usdt_diagnostics(label, inst=None, target_pid=None, tracer_pid=None,
                             tracer_stderr="", extra=None):
    """Return a bounded diagnostic bundle for live USDT tracing failures.

    Defaults stay read-only and fairly small. Set USDT_DIAG_DEEP=1 for kernel
    stacks/gdb and USDT_DIAG_LOGS=1 for access/error log tails.
    """

    if target_pid is None and inst is not None:
        try:
            target_pid = inst.get_pid()
        except Exception as e:
            target_pid = None
            extra = f"{extra or ''}\ninst.get_pid() failed: {e}"

    sections = []

    def add(name, text):
        sections.append(f"\n===== {name} =====\n{_clip(text)}")

    add("label", label)
    if extra:
        add("extra", str(extra))
    add("tracer stderr tail", _tail(tracer_stderr or "", 80))
    add("uname", _run_diag_cmd(["uname", "-a"], timeout=3))
    add("versions", _run_diag_cmd([
        "rpm", "-q",
        "kernel-core",
        f"kernel-devel-{os.uname().release}",
        "systemtap", "systemtap-runtime", "bpftrace",
        "389-ds-base", "python3-lib389",
    ], timeout=8))
    add("stap version", _run_diag_cmd(["stap", "-V"], timeout=5))
    add("bpftrace version", _run_diag_cmd(["bpftrace", "--version"], timeout=5))
    add("bpftrace info", _run_diag_cmd(["bpftrace", "--info"], timeout=10))
    add("interesting processes", _run_diag_cmd([
        "sh", "-c",
        "ps -efL | grep -E '[p]ytest|ns-slapd|stap|stapio|bpftrace'",
    ], timeout=5, max_chars=30000))
    if tracer_pid:
        add("tracer process tree", _run_diag_cmd([
            "sh", "-c",
            f"pstree -ap {tracer_pid} 2>/dev/null || ps -efL | awk '$3 == {tracer_pid} || $2 == {tracer_pid}'",
        ], timeout=5))
    else:
        add("tracer process tree", "tracer pid unavailable\n")
    add("loaded tracing modules", _run_diag_cmd([
        "sh", "-c",
        "lsmod | grep -E '^(stap_|bpf|uprobe|trace)'",
    ], timeout=5))
    add("uprobe events", _read_file(_tracefs_file("uprobe_events"), max_chars=30000))
    add("kprobe events", _read_file(_tracefs_file("kprobe_events"), max_chars=20000))
    add("dmesg tail", _run_diag_cmd(["sh", "-c", "dmesg | tail -200"], timeout=8,
                                   max_chars=30000))

    if tracer_pid:
        add(f"tracer {tracer_pid} status", _proc_text(tracer_pid, "status"))
        add(f"tracer {tracer_pid} wchan", _proc_text(tracer_pid, "wchan"))
        if USDT_DIAG_DEEP:
            add(f"tracer {tracer_pid} kernel stack", _proc_text(tracer_pid, "stack"))

    if target_pid:
        add(f"ns-slapd {target_pid} status", _proc_text(target_pid, "status"))
        add(f"ns-slapd {target_pid} wchan", _proc_text(target_pid, "wchan"))
        if USDT_DIAG_DEEP:
            add(f"ns-slapd {target_pid} threads", _thread_state(
                target_pid, include_stack=True))
            add(f"ns-slapd {target_pid} gdb backtrace", _gdb_backtrace(target_pid))

    if inst is not None and USDT_DIAG_LOGS:
        for name, path in (
            ("access log tail", getattr(inst.ds_paths, "access_log", None)),
            ("error log tail", getattr(inst.ds_paths, "error_log", None)),
        ):
            add(name, _tail_file(path))

    return "\n".join(sections)


def run_workload_with_diagnostics(drive_load, label, inst=None, target_pid=None,
                                  tracer_pid=None, tracer_stderr_fn=None,
                                  timeout=USDT_DIAG_TIMEOUT):
    """Run drive_load synchronously and emit diagnostics if it appears hung.

    The workload stays in the pytest thread so lib389/python-ldap behavior is
    unchanged. The watchdog only prints diagnostics; it does not fail the test
    or terminate the server.
    """

    if timeout is None:
        timeout = USDT_DIAG_TIMEOUT

    done = threading.Event()
    reported = threading.Event()

    def watchdog():
        if timeout <= 0 or done.wait(timeout):
            return
        reported.set()
        tracer_stderr = tracer_stderr_fn() if tracer_stderr_fn else ""
        diagnostics = collect_usdt_diagnostics(
            f"{label}: workload still running after {timeout:g}s",
            inst=inst, target_pid=target_pid, tracer_pid=tracer_pid,
            tracer_stderr=tracer_stderr,
            extra=(
                "diagnostic watchdog only; workload is still executing in "
                "the pytest thread"
            ),
        )
        sys.stderr.write(f"\n{diagnostics}\n")
        sys.stderr.flush()

    thread = threading.Thread(
        target=watchdog, name=f"{label}-diag-watchdog", daemon=True)
    thread.start()
    try:
        drive_load()
    finally:
        done.set()
        thread.join(timeout=1)
    return reported.is_set()


def send_process_group(proc, sig):
    try:
        os.killpg(proc.pid, sig)
    except ProcessLookupError:
        return
    except OSError:
        try:
            proc.send_signal(sig)
        except ProcessLookupError:
            return


def _process_group_exists(pgid):
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def _wait_process_group(proc, timeout):
    deadline = time.monotonic() + timeout
    while True:
        parent_running = proc.poll() is None
        group_exists = _process_group_exists(proc.pid)
        if not parent_running and not group_exists:
            return True

        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False

        if parent_running:
            try:
                proc.wait(timeout=min(0.1, remaining))
                continue
            except subprocess.TimeoutExpired:
                pass
        time.sleep(min(0.1, remaining))


def terminate_process_group(proc, wait_timeout=5):
    send_process_group(proc, signal.SIGTERM)
    if _wait_process_group(proc, wait_timeout):
        return

    send_process_group(proc, signal.SIGKILL)
    _wait_process_group(proc, wait_timeout)


def _drive_searches(inst, n=100):
    for _ in range(n):
        inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(objectClass=*)")


def _drive_searches_concurrent(inst, n=100, parallel=10):
    """Drive n searches over parallel fresh connections; persistent conns enter
    turbo mode and bypass the work queue.
    """

    url = f"ldap://localhost:{inst.port}"

    def one_search(_i):
        c = ldap.initialize(url)
        try:
            c.simple_bind_s(DN_DM, PW_DM)
            c.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(objectClass=*)")
        finally:
            try:
                c.unbind_s()
            except Exception:
                pass

    with concurrent.futures.ThreadPoolExecutor(max_workers=parallel) as ex:
        list(ex.map(one_search, range(n)))


class _StderrReader(threading.Thread):
    """Drain a subprocess's stderr; signal once `ready_marker` is seen.
    `ready_marker` may be a substring or a compiled re.Pattern.
    Callers can read `stderr_text` once the process has exited.
    """

    def __init__(self, proc, ready_marker):
        super().__init__(daemon=True)
        self._proc = proc
        self._ready_marker = ready_marker
        self._lines = []
        self.ready = threading.Event()

    def _matches(self, line):
        m = self._ready_marker
        if isinstance(m, str):
            return m in line
        return m.search(line) is not None

    def run(self):
        for line in iter(self._proc.stderr.readline, ""):
            self._lines.append(line)
            if self._matches(line):
                self.ready.set()
        self.ready.set()

    @property
    def stderr_text(self):
        return "".join(self._lines)
