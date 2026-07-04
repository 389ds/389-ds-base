# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import errno
import json
import mmap
import os
import re
import stat
import struct
import time

import psutil

from lib389._constants import DN_CONFIG
from lib389.cli_base import CustomHelpFormatter
from lib389.dseldif import DSEldif


# File format constants; they mirror ldap/servers/slapd/threadpool_stats.h
# and must stay in sync with it.
TP_STATS_MAGIC = 0x54504f4f4c535431  # "TPOOLST1"
TP_STATS_VER_MAJOR = 1
TP_STATS_HEADER_SIZE = 4096
TP_STATS_WORKER_SLOT_SIZE = 64

# Byte-for-byte mirror of tp_stats_header_t. Each entry is
# (field name, struct format char); "4x" skips the C struct's pad0 field.
HEADER_FIELDS = [
    ("magic", "Q"),
    ("ver_major", "H"),
    ("ver_minor", "H"),
    ("header_size", "I"),
    ("worker_slot_size", "I"),
    ("max_workers", "I"),
    ("server_pid", "Q"),
    ("start_wall_sec", "Q"),
    ("heartbeat_mono_ns", "Q"),
    ("heartbeat_wall_sec", "Q"),
    ("shutdown_clean", "I"),
    (None, "4x"),
    ("cur_work_queue", "Q"),
    ("max_work_queue", "Q"),
    ("cur_busy_workers", "Q"),
    ("max_busy_workers", "Q"),
    ("ops_initiated", "Q"),
    ("ops_completed", "Q"),
    ("cur_connections", "Q"),
]
HEADER_FORMAT = "@" + "".join(fmt for _, fmt in HEADER_FIELDS)
HEADER_NAMES = [name for name, _ in HEADER_FIELDS if name]

# Byte-for-byte mirror of tp_worker_slot_t; the slot is padded to
# TP_STATS_WORKER_SLOT_SIZE by its alignment.
WORKER_FIELDS = [
    ("state", "I"),
    ("op_tag", "I"),
    ("conn_id", "Q"),
    ("op_id", "Q"),
    ("start_ns", "Q"),
]
WORKER_FORMAT = "@" + "".join(fmt for _, fmt in WORKER_FIELDS)

# Python counterpart of the _Static_asserts in threadpool_stats.h
assert struct.calcsize(HEADER_FORMAT) <= TP_STATS_HEADER_SIZE
assert struct.calcsize(WORKER_FORMAT) <= TP_STATS_WORKER_SLOT_SIZE

# Reader-side staleness heuristics, not part of the file format
NS_PER_SEC = 1_000_000_000
STALE_HEARTBEAT_NS = 30 * NS_PER_SEC
IMPLAUSIBLE_HEARTBEAT_NS = 365 * 24 * 3600 * NS_PER_SEC

# tp_worker_state_t values
STATE_NAMES = {
    0: "unused",
    1: "idle",
    2: "busy",
    3: "exited",
}

# LDAP protocol request tags (LDAP_REQ_* in ldap.h)
OP_NAMES = {
    0x60: "bind",
    0x42: "unbind",
    0x63: "search",
    0x66: "modify",
    0x68: "add",
    0x4A: "delete",
    0x6C: "modrdn",
    0x6E: "compare",
    0x50: "abandon",
    0x77: "extended",
}


def _server_file_prefix(serverid):
    if serverid.startswith("slapd-"):
        return serverid
    return f"slapd-{serverid}"


def _crash_archives(path):
    """Crash archives preserved for the status file at path, oldest first"""
    dirname, base = os.path.split(path)
    pattern = re.compile(re.escape(base) + r"\.\d{8}-\d{6}$")
    try:
        names = sorted(name for name in os.listdir(dirname) if pattern.match(name))
    except OSError:
        return []
    return [os.path.join(dirname, name) for name in names]


def _config_threadnumber(dse):
    value = dse.get(DN_CONFIG, "nsslapd-threadnumber", single=True, lower=True)
    if value is None:
        return None
    try:
        parsed = int(value)
    except ValueError:
        return None
    return parsed if parsed > 0 else None


def _config_tp_stats_enabled(dse):
    value = dse.get(DN_CONFIG, "nsslapd-thread-pool-stats", single=True, lower=True)
    if value is None:
        return True
    return value.lower() != "off"


def _open_threadpool_file(path, inst, tp_stats_enabled, explicit=False):
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    except FileNotFoundError:
        if explicit:
            raise ValueError(f"thread-pool status file not found: {path}")
        if inst.status():
            if not tp_stats_enabled:
                raise ValueError(
                    "thread-pool status is disabled by nsslapd-thread-pool-stats in cn=config"
                )
            raise ValueError(
                "server is running but the thread-pool status file is missing "
                "(initialization may have failed - check the errors log; "
                "or the server predates this feature, or nsslapd-rundir mismatch)"
            )
        raise ValueError("instance is not running (status file is removed on clean shutdown)")
    except PermissionError:
        raise ValueError("permission denied; run as root or a member of the dirsrv group")
    except OSError as e:
        if e.errno == errno.ELOOP:
            raise ValueError("refusing to read thread-pool status through a symlink")
        raise

    return fd


def _validate_stat(path, st):
    if not stat.S_ISREG(st.st_mode):
        raise ValueError(f"refusing to read non-regular thread-pool status file: {path}")
    if st.st_size < TP_STATS_HEADER_SIZE:
        raise ValueError(
            f"thread-pool status file is too short: {st.st_size} bytes "
            f"(expected at least {TP_STATS_HEADER_SIZE})"
        )


def _unpack_header(mm):
    # _validate_stat checked the fstat size, but the mapping is what we read:
    # guard against the file shrinking between fstat and mmap
    if len(mm) < TP_STATS_HEADER_SIZE:
        raise ValueError("thread-pool status header is truncated")

    header = dict(zip(HEADER_NAMES, struct.unpack_from(HEADER_FORMAT, mm, 0)))

    if header["magic"] != TP_STATS_MAGIC:
        raise ValueError("bad thread-pool status magic; refusing to parse file")
    if header["ver_major"] != TP_STATS_VER_MAJOR:
        raise ValueError(
            f"unsupported thread-pool status version "
            f"{header['ver_major']}.{header['ver_minor']}"
        )
    if header["header_size"] != TP_STATS_HEADER_SIZE:
        raise ValueError(
            f"unsupported thread-pool status header size {header['header_size']}"
        )
    if header["worker_slot_size"] != TP_STATS_WORKER_SLOT_SIZE:
        raise ValueError(
            f"unsupported thread-pool worker slot size {header['worker_slot_size']}"
        )
    if header["max_workers"] < 1 or header["max_workers"] > 65535:
        raise ValueError(f"invalid thread-pool worker count {header['max_workers']}")

    expected_size = header["header_size"] + (header["max_workers"] * header["worker_slot_size"])
    if expected_size > len(mm):
        raise ValueError(
            f"thread-pool status file is truncated: {len(mm)} bytes "
            f"(expected at least {expected_size})"
        )

    return header


def _state_name(state):
    return STATE_NAMES.get(state, f"unknown-{state}")


def _op_name(op_tag):
    if op_tag == 0:
        return ""
    return OP_NAMES.get(op_tag, str(op_tag))


def _duration_ns(now_ns, start_ns):
    if start_ns == 0 or now_ns < start_ns:
        return 0
    return now_ns - start_ns


def _unpack_workers(mm, header, now_ns):
    workers = []
    for idx in range(header["max_workers"]):
        offset = header["header_size"] + (idx * header["worker_slot_size"])
        state, op_tag, conn_id, op_id, start_ns = struct.unpack_from(WORKER_FORMAT, mm, offset)
        if state == 0:
            continue
        # start_ns is the in-flight sentinel; op_id 0 is a valid first op
        in_flight = start_ns != 0
        workers.append({
            "idx": idx + 1,
            "state": _state_name(state),
            "op": _op_name(op_tag),
            "conn": conn_id if conn_id != 0 else None,
            "op_id": op_id if in_flight else None,
            "duration_ns": _duration_ns(now_ns, start_ns),
        })
    return workers


def _pid_warnings(pid):
    """Return (warnings, pid_alive); pid_alive means a live ns-slapd owns the pid"""
    warnings = []
    if pid == 0:
        warnings.append("status file does not contain a valid server pid")
        return warnings, False

    try:
        name = psutil.Process(pid).name()
    except (psutil.NoSuchProcess, psutil.ZombieProcess):
        warnings.append(f"stale file from a crashed or killed server (pid {pid} is not running)")
        return warnings, False
    except psutil.AccessDenied:
        warnings.append(f"server pid {pid} exists but process name could not be inspected")
        return warnings, True

    if name != "ns-slapd":
        warnings.append(f"stale file: pid {pid} belongs to {name!r}, not 'ns-slapd'")
        return warnings, False
    return warnings, True


def _heartbeat_age(now_ns, heartbeat_ns):
    if heartbeat_ns == 0:
        return None
    return now_ns - heartbeat_ns


def _heartbeat_warnings(pid_alive, age_ns):
    warnings = []
    if age_ns is None:
        warnings.append("thread-pool heartbeat has never been written")
    elif age_ns < 0:
        warnings.append("thread-pool heartbeat is from a different monotonic-clock domain")
    elif age_ns > IMPLAUSIBLE_HEARTBEAT_NS:
        warnings.append("thread-pool heartbeat age is implausible; the file may predate a reboot")
    elif age_ns > STALE_HEARTBEAT_NS:
        if pid_alive:
            warnings.append("server process exists but diagnostics are stale; server may be stalled")
        else:
            warnings.append("thread-pool diagnostics are stale")
    return warnings


def _read_threadpool_status(inst, file_path=None):
    warnings = []
    archives = []
    if file_path is not None:
        path = file_path
        configured_threads = None
        tp_stats_enabled = True
    else:
        dse = DSEldif(inst)
        rundir = dse.get(DN_CONFIG, "nsslapd-rundir", single=True, lower=True)
        if rundir is None:
            rundir = inst.ds_paths.run_dir
            warnings.append("nsslapd-rundir is missing from dse.ldif; using lib389 run_dir fallback")
        path = os.path.join(rundir, f"{_server_file_prefix(inst.serverid)}.threadpool")
        archives = _crash_archives(path)
        configured_threads = _config_threadnumber(dse)
        tp_stats_enabled = _config_tp_stats_enabled(dse)

    try:
        fd = _open_threadpool_file(path, inst, tp_stats_enabled, explicit=file_path is not None)
    except ValueError as e:
        if archives:
            raise ValueError(f"{e}; {len(archives)} crash archive(s) present, newest: {archives[-1]}")
        raise
    try:
        st = os.fstat(fd)
        _validate_stat(path, st)
        with mmap.mmap(fd, 0, access=mmap.ACCESS_READ) as mm:
            header = _unpack_header(mm)
            now_ns = time.monotonic_ns()
            age_ns = _heartbeat_age(now_ns, header["heartbeat_mono_ns"])
            pid_warnings, pid_alive = _pid_warnings(header["server_pid"])
            warnings.extend(pid_warnings)
            warnings.extend(_heartbeat_warnings(pid_alive, age_ns))

            if header["shutdown_clean"] != 0:
                warnings.append("clean shutdown leftover")
            if configured_threads is not None and configured_threads != header["max_workers"]:
                warnings.append(
                    f"dse.ldif nsslapd-threadnumber is {configured_threads}, "
                    f"but status file was sized for {header['max_workers']} workers"
                )

            workers = _unpack_workers(mm, header, now_ns)
    finally:
        os.close(fd)

    if archives:
        warnings.append(
            f"{len(archives)} crash archive(s) in {os.path.dirname(path)}, "
            f"newest: {os.path.basename(archives[-1])} (read with --file)"
        )

    age_sec = None if age_ns is None else age_ns / NS_PER_SEC
    start_wall = header["start_wall_sec"]
    uptime_sec = max(0, int(time.time()) - start_wall) if start_wall else None

    return {
        "type": "result",
        "instance": inst.serverid,
        "path": path,
        "pid": header["server_pid"],
        "version": {
            "major": header["ver_major"],
            "minor": header["ver_minor"],
        },
        "start_wall_sec": start_wall,
        "uptime_sec": uptime_sec,
        "heartbeat_age_sec": age_sec,
        "heartbeat_wall_sec": header["heartbeat_wall_sec"],
        "pool": {
            "max_workers": header["max_workers"],
            "cur_busy_workers": header["cur_busy_workers"],
            "max_busy_workers": header["max_busy_workers"],
            "cur_work_queue": header["cur_work_queue"],
            "max_work_queue": header["max_work_queue"],
            "ops_initiated": header["ops_initiated"],
            "ops_completed": header["ops_completed"],
            "cur_connections": header["cur_connections"],
        },
        "workers": workers,
        "warnings": warnings,
    }


def _format_seconds(value):
    if value is None:
        return "unknown"
    return f"{value:.3f}s"


def _format_duration_ns(duration_ns):
    if duration_ns == 0:
        return "-"
    seconds = duration_ns / NS_PER_SEC
    if seconds < 1:
        return f"{seconds * 1000:.1f}ms"
    return f"{seconds:.3f}s"


def _format_optional(value):
    return "-" if value is None else str(value)


def _emit_text(log, status):
    pool = status["pool"]
    log.info(f"Instance: {status['instance']}")
    log.info(f"Path: {status['path']}")
    log.info(f"PID: {status['pid']}")
    log.info(f"Uptime: {_format_seconds(status['uptime_sec'])}")
    log.info(f"Heartbeat age: {_format_seconds(status['heartbeat_age_sec'])}")
    log.info(
        "Workers: "
        f"{pool['cur_busy_workers']}/{pool['max_workers']} busy "
        f"(max {pool['max_busy_workers']})"
    )
    log.info(
        "Queue: "
        f"{pool['cur_work_queue']} current "
        f"(max {pool['max_work_queue']})"
    )
    log.info(
        "Operations: "
        f"{pool['ops_initiated']} initiated, "
        f"{pool['ops_completed']} completed"
    )
    log.info(f"Current connections: {pool['cur_connections']}")

    if status["warnings"]:
        log.info("Warnings:")
        for warning in status["warnings"]:
            log.info(f"  - {warning}")

    log.info("")
    log.info(f"{'IDX':>5} {'STATE':<8} {'OP':<10} {'CONN':>12} {'OP-ID':>12} {'DURATION':>12}")
    for worker in status["workers"]:
        op = worker["op"].upper() if worker["op"] else "-"
        log.info(
            f"{worker['idx']:>5} "
            f"{worker['state'].upper():<8} "
            f"{op:<10} "
            f"{_format_optional(worker['conn']):>12} "
            f"{_format_optional(worker['op_id']):>12} "
            f"{_format_duration_ns(worker['duration_ns']):>12}"
        )


def thread_pool_status(inst, log, args):
    status = _read_threadpool_status(inst, file_path=args.file)
    if args.json:
        log.info(json.dumps(status, indent=4))
    else:
        _emit_text(log, status)


def create_parser(subparsers):
    thread_pool_parser = subparsers.add_parser(
        "thread-pool",
        help="Offline thread pool diagnostics read from the local mmap status file",
        formatter_class=CustomHelpFormatter,
    )
    subcommands = thread_pool_parser.add_subparsers(help="action")

    status_parser = subcommands.add_parser(
        "status",
        help="Display pool gauges and per-worker activity without an LDAP connection",
        formatter_class=CustomHelpFormatter,
    )
    status_parser.add_argument(
        "--file", default=None,
        help="Read this thread-pool status file instead of the instance's live file "
             "(e.g. a crash file preserved as <file>.YYYYMMDD-HHMMSS)",
    )
    status_parser.set_defaults(func=thread_pool_status)
