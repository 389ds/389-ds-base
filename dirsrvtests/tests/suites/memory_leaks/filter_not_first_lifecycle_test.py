# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Full-lifecycle ASan coverage for NOT-first candidate ownership."""

import fnmatch
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from contextlib import contextmanager

import ldap
import pytest

from ldap.controls import SimplePagedResultsControl
from lib389._constants import DEFAULT_SUFFIX, ReplicaRole, TASK_WAIT
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.user import UserAccounts
from lib389.index import VLVIndex, VLVSearch
from lib389.paths import Paths
from lib389.tasks import Tasks
from lib389.utils import ensure_str, get_default_db_lib
from test389.topologies import create_topology

from ..filter.filter_large_filter_support import (
    NOT_FIRST_EMPTY_RANGE_FILTER,
    OR_LOOKUP_ATTR,
    OWNERSHIP_TEST_BASE,
    ownership_workload,
    search_dns,
)


pytestmark = [
    pytest.mark.tier2,
    pytest.mark.skipif(get_default_db_lib() != "mdb",
                       reason="The ownership matrix requires MDB"),
    pytest.mark.skipif(not Paths().asan_enabled,
                       reason="The ownership matrix requires compile-time ASan"),
]

ASAN_DROPIN = "/etc/systemd/system/dirsrv@.service.d/zz-filter-ownership-asan.conf"
ASAN_OPTIONS = (
    "detect_leaks=1:halt_on_error=1:disable_coredump=0:"
    "exitcode=0:symbolize=0:fast_unwind_on_malloc=0:"
    "malloc_context_size=64:print_suppressions=0:"
    "log_path=/run/dirsrv/ns-slapd-%i.asan"
)
REPORT_SUFFIXES = ("asan", "lsan", "msan", "tsan", "ubsan")
CORE_PATTERNS = ("core", "core.*", "core-*", "*.core")
TARGET_STACK = (
    "idl_alloc",
    "index_read_ext_allids",
    "filter_candidates_ext",
)
GENERIC_TLS_STACK = (
    "dblayer_push_pvt_txn",
    "vlv_rebuild_scope_filter",
)
NATIVE_TLS_STACK = (
    "get_mdbtxnanchor",
    "dbmdb_start_txn",
)
TLS_CLASSIFICATIONS = (
    "generic-dblayer-tls",
    "native-mdb-tls",
)
VLV_CONFIG_BASE = (
    "cn=userRoot,cn=ldbm database,cn=plugins,cn=config"
)
VLV_SEARCH_CN = "asanOwnershipVlvSearch"
VLV_INDEX_CN = "asanOwnershipVlvIndex"
VLV_SEARCH_DN = f"cn={VLV_SEARCH_CN},{VLV_CONFIG_BASE}"
VLV_INDEX_DN = f"cn={VLV_INDEX_CN},{VLV_SEARCH_DN}"
OR_ENTRY_COUNT = 60
OR_PAGE_SIZE = 10
LEAK_BLOCK_RE = re.compile(
    r"(?ms)^(?P<kind>Direct|Indirect) leak of (?P<bytes>\d+) byte\(s\) "
    r"in (?P<objects>\d+) object\(s\).*?"
    r"(?=^(?:Direct|Indirect) leak of |^SUMMARY:|\Z)"
)
INVALID_PATTERNS = (
    "ERROR: AddressSanitizer:",
    "AddressSanitizer:DEADLYSIGNAL",
    "double-free",
    "heap-use-after-free",
    "stack-use-after-return",
    "global-buffer-overflow",
    "heap-buffer-overflow",
    "LeakSanitizer has encountered a fatal error",
    "runtime error:",
)
DEBUG_IDENTITY = None


def _output(argv):
    return subprocess.check_output(
        argv, stderr=subprocess.STDOUT, text=True).strip()


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as artifact:
        for block in iter(lambda: artifact.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _build_id(path):
    notes = _output(["readelf", "-n", path])
    match = re.search(r"Build ID:\s*([0-9a-fA-F]+)", notes)
    assert match is not None, notes
    return match.group(1).lower()


def _elf_identity(path):
    realpath = os.path.realpath(path)
    linkage = _output(["ldd", realpath])
    assert "not found" not in linkage.lower()
    return {
        "path": realpath,
        "sha256": _sha256(realpath),
        "build_id": _build_id(realpath),
        "package": _output([
            "rpm", "-qf", "--queryformat",
            "%{NAME} %{EPOCHNUM}:%{VERSION}-%{RELEASE}.%{ARCH}", realpath,
        ]),
        "linkage": linkage,
    }


def _rpm_evra(path=None, package=None):
    query = "%{EPOCHNUM}:%{VERSION}-%{RELEASE}.%{ARCH}"
    if path is not None:
        return _output(["rpm", "-qp", "--queryformat", query, path])
    return _output(["rpm", "-q", "--queryformat", query, package])


def _matching_rpm(package, evra):
    matches = []
    for path in glob.glob("/workspace/dist/rpms/*.rpm"):
        name = _output(["rpm", "-qp", "--queryformat", "%{NAME}", path])
        if name == package and _rpm_evra(path=path) == evra:
            matches.append(path)
    assert len(matches) == 1, (package, evra, matches)
    return {
        "path": matches[0],
        "sha256": _sha256(matches[0]),
        "evra": evra,
    }


def _symbol_address(debug_path, symbol):
    pattern = re.compile(
        r"^([0-9a-fA-F]+)\s+[tT]\s+" + re.escape(symbol) + r"$"
    )
    for line in _output(["nm", "-an", debug_path]).splitlines():
        match = pattern.match(line)
        if match is not None:
            return "0x%s" % match.group(1)
    raise AssertionError("missing debug symbol %s in %s" %
                         (symbol, debug_path))


def _symbolizer():
    path = shutil.which("eu-addr2line")
    assert path is not None
    return path


def _debug_object(path):
    build_id = _build_id(path)
    debug_path = "/usr/lib/debug/.build-id/%s/%s.debug" % (
        build_id[:2], build_id[2:])
    return debug_path if os.path.isfile(debug_path) else path


def _symbolize_address(path, address):
    try:
        debug_object = _debug_object(path)
    except (AssertionError, OSError, subprocess.CalledProcessError) as error:
        return "%s+%s\n%r" % (path, address, error)
    result = subprocess.run([
        _symbolizer(), "-f", "-C", "-i", "-e", debug_object, address,
    ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
       check=False)
    rendered = result.stdout.strip()
    if result.returncode != 0:
        return "%s+%s\n%s" % (path, address, rendered)
    return rendered


def _install_matching_debug_rpms():
    base_evra = _rpm_evra(package="389-ds-base")
    selected = []
    for package in (
            "389-ds-base-debuginfo",
            "389-ds-base-libs-debuginfo",
            "389-ds-base-debugsource"):
        rpm_identity = _matching_rpm(package, base_evra)
        selected.append(rpm_identity)

    subprocess.check_call([
        "dnf", "install", "-y", "--disablerepo=*",
        "--setopt=install_weak_deps=False",
        *(item["path"] for item in selected),
    ])
    for package in ("389-ds-base-debuginfo",
                    "389-ds-base-libs-debuginfo",
                    "389-ds-base-debugsource"):
        assert _rpm_evra(package=package) == base_evra

    plugin = os.path.join(Paths().plugin_dir, "libback-ldbm.so")
    plugin_id = _build_id(plugin)
    debug_path = "/usr/lib/debug/.build-id/%s/%s.debug" % (
        plugin_id[:2], plugin_id[2:])
    assert os.path.isfile(debug_path)
    assert _build_id(debug_path) == plugin_id
    preflight = {}
    for symbol in ("list_candidates", "dbmdb_start_txn"):
        address = _symbol_address(debug_path, symbol)
        rendered = _symbolize_address(plugin, address)
        assert symbol in rendered, rendered
        preflight[symbol] = {"address": address, "rendered": rendered}

    return {
        "base_evra": base_evra,
        "rpms": selected,
        "plugin_build_id": plugin_id,
        "plugin_debug_path": debug_path,
        "symbol_preflight": preflight,
    }


@pytest.fixture(scope="module", autouse=True)
def compile_time_asan_runtime(request):
    """Install matching symbols and apply one controlled ASan environment."""
    global DEBUG_IDENTITY

    assert request.config.getoption("--sanitizer") is None
    DEBUG_IDENTITY = _install_matching_debug_rpms()
    os.makedirs(Paths().run_dir, exist_ok=True)
    os.makedirs(os.path.dirname(ASAN_DROPIN), exist_ok=True)
    previous_dropin = None
    if os.path.exists(ASAN_DROPIN):
        with open(ASAN_DROPIN, "rb") as dropin:
            previous_dropin = dropin.read()
    previous_suid = _output(["sysctl", "-n", "fs.suid_dumpable"])
    previous_ptrace = _output([
        "sysctl", "-n", "kernel.yama.ptrace_scope"
    ])

    try:
        with open(ASAN_DROPIN, "w") as dropin:
            dropin.write("[Service]\n")
            dropin.write("UnsetEnvironment=LD_PRELOAD\n")
            dropin.write("Environment=ASAN_OPTIONS=%s\n" % ASAN_OPTIONS)
            dropin.write("LimitCORE=infinity\n")
            dropin.write("TimeoutStopSec=600\n")
        if previous_suid != "1":
            subprocess.check_call(["sysctl", "-w", "fs.suid_dumpable=1"])
        if previous_ptrace != "0":
            subprocess.check_call([
                "sysctl", "-w", "kernel.yama.ptrace_scope=0"
            ])
        subprocess.check_call(["systemctl", "daemon-reload"])
        yield
    finally:
        cleanup_errors = []
        try:
            if previous_dropin is None:
                if os.path.exists(ASAN_DROPIN):
                    os.unlink(ASAN_DROPIN)
            else:
                with open(ASAN_DROPIN, "wb") as dropin:
                    dropin.write(previous_dropin)
        except OSError as error:
            cleanup_errors.append(repr(error))
        try:
            if previous_suid != "1":
                subprocess.check_call([
                    "sysctl", "-w", "fs.suid_dumpable=%s" % previous_suid
                ])
            if previous_ptrace != "0":
                subprocess.check_call([
                    "sysctl", "-w",
                    "kernel.yama.ptrace_scope=%s" % previous_ptrace,
                ])
            subprocess.check_call(["systemctl", "daemon-reload"])
        except subprocess.CalledProcessError as error:
            cleanup_errors.append(repr(error))
        if cleanup_errors and sys.exc_info()[0] is None:
            raise AssertionError("ASan fixture cleanup failed: %s" %
                                 cleanup_errors)


def _artifact_kind(name):
    if any((".%s" % suffix) in name for suffix in REPORT_SUFFIXES):
        return "report"
    if any(fnmatch.fnmatch(name, pattern) for pattern in CORE_PATTERNS):
        return "core"
    return None


def _artifact_roots():
    core_pattern = _output(["sysctl", "-n", "kernel.core_pattern"])
    assert core_pattern.startswith("/workspace/assets/cores/"), core_pattern
    return {
        "run-dirsrv": "/run/dirsrv",
        "workspace-cores": "/workspace/assets/cores",
    }, core_pattern


def _artifact_snapshot():
    roots, core_pattern = _artifact_roots()
    snapshot = {}
    for root_name, root in roots.items():
        assert os.path.isdir(root), root
        walk_errors = []

        def onerror(error):
            walk_errors.append(repr(error))

        for directory, _subdirs, names in os.walk(root, onerror=onerror):
            for name in names:
                kind = _artifact_kind(name)
                if kind is None:
                    continue
                path = os.path.join(directory, name)
                stat = os.stat(path)
                snapshot[path] = {
                    "kind": kind,
                    "root": root_name,
                    "relative": os.path.relpath(path, root),
                    "device": stat.st_dev,
                    "inode": stat.st_ino,
                    "size": stat.st_size,
                    "mtime_ns": stat.st_mtime_ns,
                    "sha256": _sha256(path),
                }
        assert not walk_errors, walk_errors
    return snapshot, core_pattern


def _new_artifacts(before, after):
    return {
        path: metadata for path, metadata in after.items()
        if before.get(path) != metadata
    }


FRAME_RE = re.compile(
    r"\((?P<module>/[^()]+)\+0x(?P<offset>[0-9a-fA-F]+)\)"
)


def _symbolize_report(raw):
    symbolized = []
    cache = {}
    for line in raw.splitlines():
        symbolized.append(line)
        match = FRAME_RE.search(line)
        if match is None:
            continue
        key = (match.group("module"), match.group("offset"))
        if key not in cache:
            cache[key] = _symbolize_address(key[0], "0x%s" % key[1])
        symbolized.extend("    %s" % item
                          for item in cache[key].splitlines())
    return "\n".join(symbolized) + "\n"


def _ordered_tokens(text, tokens):
    offset = 0
    for token in tokens:
        offset = text.find(token, offset)
        if offset < 0:
            return False
        offset += len(token)
    return True


def _classify_reports(artifacts):
    reports = []
    report_artifacts = []
    totals = {
        "not-first-idl": {"bytes": 0, "objects": 0},
        "generic-dblayer-tls": {"bytes": 0, "objects": 0},
        "native-mdb-tls": {"bytes": 0, "objects": 0},
    }
    invalid_access = []
    unknown_reports = []
    unknown_blocks = []

    for path, metadata in sorted(artifacts.items()):
        if metadata["kind"] != "report":
            continue
        report_artifacts.append({
            "path": path,
            "size": metadata["size"],
            "sha256": metadata["sha256"],
        })
        if metadata["size"] == 0:
            continue
        with open(path, "r", errors="replace") as report_file:
            raw = report_file.read()
        symbolized = _symbolize_report(raw)
        blocks = []
        for match in LEAK_BLOCK_RE.finditer(symbolized):
            block = match.group(0)
            classification = "other"
            if _ordered_tokens(block, TARGET_STACK):
                classification = "not-first-idl"
            elif _ordered_tokens(block, GENERIC_TLS_STACK):
                classification = "generic-dblayer-tls"
            elif _ordered_tokens(block, NATIVE_TLS_STACK):
                classification = "native-mdb-tls"
            block_data = {
                "bytes": int(match.group("bytes")),
                "objects": int(match.group("objects")),
                "classification": classification,
                "stack": block,
            }
            blocks.append(block_data)
            if classification == "other":
                unknown_blocks.append({"path": path, **block_data})
            else:
                totals[classification]["bytes"] += block_data["bytes"]
                totals[classification]["objects"] += block_data["objects"]

        errors = [pattern for pattern in INVALID_PATTERNS
                  if pattern in symbolized]
        if errors:
            invalid_access.append({"path": path, "patterns": errors})
        if raw.strip() and not blocks and not errors:
            unknown_reports.append(path)
        reports.append({
            "path": path,
            "size": metadata["size"],
            "sha256": metadata["sha256"],
            "blocks": blocks,
            "symbolized": symbolized,
        })

    return {
        "reports": reports,
        "report_artifacts": report_artifacts,
        "totals": totals,
        "invalid_access": invalid_access,
        "unknown_reports": unknown_reports,
        "unknown_blocks": unknown_blocks,
    }


def _source_identity():
    expected = os.environ.get("DS_EXPECTED_SOURCE_SHA", "")
    assert re.fullmatch(r"[0-9a-f]{40}", expected), expected
    head = _output(["git", "-C", "/workspace", "rev-parse", "HEAD"])
    assert head == expected
    return {
        "expected_head": expected,
        "head": head,
        "short_head": _output([
            "git", "-C", "/workspace", "rev-parse", "--short", "HEAD"
        ]),
        "status": _output([
            "git", "-C", "/workspace", "status", "--short",
            "--untracked-files=all",
        ]),
    }


def _runtime_identity(inst):
    source = _source_identity()
    sanitizer_sysctls = {
        "fs.suid_dumpable": _output([
            "sysctl", "-n", "fs.suid_dumpable"
        ]),
        "kernel.yama.ptrace_scope": _output([
            "sysctl", "-n", "kernel.yama.ptrace_scope"
        ]),
    }
    assert sanitizer_sysctls == {
        "fs.suid_dumpable": "1",
        "kernel.yama.ptrace_scope": "0",
    }
    installed = {
        package: _rpm_evra(package=package)
        for package in ("389-ds-base", "389-ds-base-libs", "python3-lib389")
    }
    local_rpms = {
        package: _matching_rpm(package, evra)
        for package, evra in installed.items()
    }
    executable = os.path.join(inst.get_sbin_dir(), "ns-slapd")
    plugin = os.path.join(inst.get_plugin_dir(), "libback-ldbm.so")
    executable_identity = _elf_identity(executable)
    plugin_identity = _elf_identity(plugin)
    pid = inst.get_pid()
    environ = {}
    with open("/proc/%s/environ" % pid, "rb") as proc_environ:
        for item in proc_environ.read().split(b"\0"):
            key, separator, value = item.partition(b"=")
            if separator:
                environ[key.decode(errors="replace")] = value.decode(
                    errors="replace")
    with open("/proc/%s/maps" % pid, "r") as proc_maps:
        maps = proc_maps.read()
    with open("/proc/%s/limits" % pid, "r") as proc_limits:
        limits = proc_limits.read()

    expected_options = ASAN_OPTIONS.replace("%i", inst.serverid)
    assert environ.get("ASAN_OPTIONS") == expected_options
    assert "LD_PRELOAD" not in environ
    assert "libasan.so" in maps
    assert "libjemalloc" not in maps
    assert "liblsan.so" not in maps
    assert os.path.realpath(plugin) in maps
    assert "libasan.so" in executable_identity["linkage"]
    assert "libasan.so" in plugin_identity["linkage"]
    core_limits = [line.split() for line in limits.splitlines()
                   if line.startswith("Max core file size")]
    assert len(core_limits) == 1, limits
    assert core_limits[0][-3:-1] == ["unlimited", "unlimited"], \
        core_limits[0]
    assert inst.has_asan()
    assert "asan" in installed["389-ds-base"]
    assert source["short_head"] in installed["389-ds-base"]

    return {
        "source": source,
        "installed": installed,
        "local_rpms": local_rpms,
        "executable": executable_identity,
        "plugin": plugin_identity,
        "environment": {
            key: environ[key] for key in ("ASAN_OPTIONS", "LD_PRELOAD")
            if key in environ
        },
        "maps": maps,
        "limits": limits,
        "sysctls": sanitizer_sysctls,
        "pid": pid,
        "serverid": inst.serverid,
        "debug": DEBUG_IDENTITY,
    }


@contextmanager
def _error_log_window(path):
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_CLOEXEC", 0))
    window = {"text": None}
    try:
        initial = os.fstat(fd)
        identity = (initial.st_dev, initial.st_ino)
        begin = initial.st_size
        guard_at = max(0, begin - 4096)
        guard = os.pread(fd, begin - guard_at, guard_at)
        yield window

        active = os.stat(path)
        assert (active.st_dev, active.st_ino) == identity
        assert active.st_size >= begin
        assert os.pread(fd, begin - guard_at, guard_at) == guard
        end = active.st_size
        chunks = []
        offset = begin
        while offset < end:
            chunk = os.pread(fd, end - offset, offset)
            assert chunk
            chunks.append(chunk)
            offset += len(chunk)
        final_fd = os.fstat(fd)
        final_active = os.stat(path)
        assert (final_fd.st_dev, final_fd.st_ino) == identity
        assert (final_active.st_dev, final_active.st_ino) == identity
        assert final_fd.st_size >= end and final_active.st_size >= end
        assert os.pread(fd, begin - guard_at, guard_at) == guard
        window["text"] = b"".join(chunks).decode(
            "utf-8", errors="replace")
    finally:
        os.close(fd)


DIAGNOSTIC_PATTERNS = (
    re.compile(r"list_candidates\s+-\s+NOT filter", re.IGNORECASE),
    re.compile(r"ava_candidates\s+-\s+uid=lf-ownership-absent",
               re.IGNORECASE),
    re.compile(r"ava_candidates\s+-\s+objectClass=person", re.IGNORECASE),
    re.compile(r"filter_candidates_ext\s+-\s+GE\b", re.IGNORECASE),
    re.compile(r"range_candidates\s+-\s+=> attr=lfEmptyRange",
               re.IGNORECASE),
    re.compile(r"range_candidates\s+-\s+<= 0\b", re.IGNORECASE),
    re.compile(r"list_candidates\s+-\s+<=\s+NULL 2", re.IGNORECASE),
)


def _assert_diagnostics(text, operation_count):
    matches = [list(pattern.finditer(text)) for pattern in DIAGNOSTIC_PATTERNS]
    assert all(len(found) == operation_count for found in matches), [
        len(found) for found in matches
    ]
    for operation_index in range(operation_count):
        positions = [found[operation_index].start() for found in matches]
        assert positions == sorted(positions), positions


def _systemd_stop_state(inst):
    properties = _output([
        "systemctl", "show", "dirsrv@%s.service" % inst.serverid,
        "--property=ActiveState,SubState,Result,ExecMainCode,ExecMainStatus",
    ])
    return dict(line.split("=", 1) for line in properties.splitlines())


def _archive(label, artifacts, identity, summary, inst):
    evidence_root = os.environ.get("DS_ASAN_EVIDENCE_DIR", "")
    assert os.path.isabs(evidence_root), evidence_root
    destination = os.path.join(evidence_root, label)
    assert not os.path.exists(destination), destination
    os.makedirs(destination)
    manifest = []

    for path, metadata in artifacts.items():
        target = os.path.join(
            destination, "artifacts", metadata["root"], metadata["relative"])
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(path, target)
        assert _sha256(target) == metadata["sha256"]
        manifest.append({"source": path, "copy": target, **metadata})

    for report in summary["reports"]:
        target = os.path.join(
            destination, "symbolized",
            os.path.basename(report["path"]) + ".symbolized")
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w") as output:
            output.write(report["symbolized"])

    for name, path in (("errors", inst.ds_paths.error_log),
                       ("access", inst.ds_paths.access_log)):
        if os.path.isfile(path):
            target = os.path.join(destination, "logs", name)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy2(path, target)

    for path in (
            "/workspace/dirsrvtests/tests/suites/filter/filter_large_filter_support.py",
            "/workspace/ldap/servers/slapd/back-ldbm/filterindex.c",
            "/workspace/ldap/servers/slapd/back-ldbm/idl_set.c",
            "/workspace/ldap/servers/slapd/back-ldbm/dblayer.c",
            "/workspace/ldap/servers/slapd/back-ldbm/db-mdb/mdb_txn.c",
            ASAN_DROPIN,
            os.path.realpath(__file__)):
        target = os.path.join(destination, "sources", os.path.basename(path))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(path, target)
        manifest.append({
            "source": path, "copy": target, "sha256": _sha256(target)
        })

    summary["archive"] = destination
    with open(os.path.join(destination, "identity.json"), "w") as output:
        json.dump(identity, output, indent=2, sort_keys=True)
    with open(os.path.join(destination, "manifest.json"), "w") as output:
        json.dump(manifest, output, indent=2, sort_keys=True)
    with open(os.path.join(destination, "summary.json"), "w") as output:
        json.dump(summary, output, indent=2, sort_keys=True)
    return destination


def _stop_collect(inst, before, identity, label):
    expected_pid = identity["pid"]
    stop_error = None
    stop_text = ""
    try:
        with _error_log_window(inst.ds_paths.error_log) as window:
            inst.stop(timeout=600)
        stop_text = window["text"]
    except Exception as error:
        stop_error = repr(error)

    stopped = not inst.status()
    after, core_pattern = _artifact_snapshot()
    artifacts = _new_artifacts(before, after)
    classification = _classify_reports(artifacts)
    cores = [path for path, metadata in artifacts.items()
             if metadata["kind"] == "core" and metadata["size"] > 0]
    systemd_state = _systemd_stop_state(inst) if stopped else {}
    summary = {
        **classification,
        "cores": cores,
        "core_pattern": core_pattern,
        "expected_pid": expected_pid,
        "pid_gone": not os.path.exists("/proc/%s" % expected_pid),
        "stop_error": stop_error,
        "stopped": stopped,
        "stop_log_seen": "main - slapd stopped." in stop_text,
        "systemd": systemd_state,
    }
    _archive(label, artifacts, identity, summary, inst)
    return summary


def _assert_common(summary):
    assert summary["stop_error"] is None
    assert summary["stopped"]
    assert summary["pid_gone"]
    assert summary["stop_log_seen"]
    assert summary["cores"] == []
    assert summary["invalid_access"] == []
    assert summary["unknown_reports"] == []
    assert summary["unknown_blocks"] == []
    assert summary["systemd"] == {
        "ActiveState": "inactive",
        "SubState": "dead",
        "Result": "success",
        "ExecMainCode": "1",
        "ExecMainStatus": "0",
    }


def _raise_phase_failure(operation_failure, collection_failure):
    if operation_failure is not None:
        if collection_failure is not None:
            operation_failure[1].add_note(
                "stop/evidence collection also failed: %r" %
                collection_failure[1])
        raise operation_failure[1].with_traceback(operation_failure[2])
    if collection_failure is not None:
        raise collection_failure[1].with_traceback(collection_failure[2])


def _run_process_phase(inst, before, label, operation=None):
    identity = {"pid": inst.get_pid(), "serverid": inst.serverid}
    operation_failure = None
    try:
        identity = _runtime_identity(inst)
        if operation is not None:
            operation(inst, identity)
    except BaseException:
        operation_failure = sys.exc_info()

    summary = None
    collection_failure = None
    try:
        summary = _stop_collect(inst, before, identity, label)
        _assert_common(summary)
    except BaseException:
        collection_failure = sys.exc_info()
    _raise_phase_failure(operation_failure, collection_failure)
    return summary


def _cleanup_instance(inst):
    active_failure = sys.exc_info()
    try:
        if inst.status():
            inst.stop(timeout=600)
        if inst.exists():
            inst.delete()
    except BaseException as cleanup_error:
        if active_failure[1] is None:
            raise
        active_failure[1].add_note(
            "instance cleanup also failed: %r" % cleanup_error)


def _assert_health(inst, base=DEFAULT_SUFFIX):
    result_type, dns = search_dns(
        inst, "(objectClass=*)", base=base, scope=ldap.SCOPE_BASE
    )
    assert result_type == ldap.RES_SEARCH_RESULT
    assert dns == [base.lower()]


def _configured_vlv_dns(inst):
    entries = inst.search_s(
        VLV_CONFIG_BASE, ldap.SCOPE_ONELEVEL,
        "(objectClass=vlvSearch)", ["1.1"]
    )
    return sorted(entry.dn.lower() for entry in entries)


def _verify_explicit_vlv(inst):
    assert _configured_vlv_dns(inst) == [VLV_SEARCH_DN.lower()]
    assert VLVSearch(inst, dn=VLV_SEARCH_DN).exists()
    assert VLVIndex(inst, dn=VLV_INDEX_DN).exists()
    _assert_health(inst)


def _configure_and_reindex_vlv(inst, _identity):
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    created = [users.create_test_user(uid=48000 + index, gid=48000)
               for index in range(16)]
    expected_dns = sorted(entry.dn.lower() for entry in created)
    result_type, dns = search_dns(
        inst, "(uid=test_user_48*)", base=DEFAULT_SUFFIX)
    assert result_type == ldap.RES_SEARCH_RESULT
    assert dns == expected_dns

    vlv_search = VLVSearch(inst)
    vlv_search.create(
        basedn=VLV_CONFIG_BASE,
        properties={
            "cn": VLV_SEARCH_CN,
            "vlvbase": DEFAULT_SUFFIX,
            "vlvfilter": "(uid=test_user_48*)",
            "vlvscope": str(ldap.SCOPE_SUBTREE),
        },
    )
    vlv_index = VLVIndex(inst)
    vlv_index.create(
        basedn=VLV_SEARCH_DN,
        properties={"cn": VLV_INDEX_CN, "vlvsort": "cn"},
    )
    assert Tasks(inst).reindex(
        suffix=DEFAULT_SUFFIX,
        attrname=vlv_index.rdn,
        args={TASK_WAIT: True},
        vlv=True,
    ) == 0
    _verify_explicit_vlv(inst)


def _run_startup_baseline():
    before, _core_pattern = _artifact_snapshot()
    topology = create_topology({ReplicaRole.STANDALONE: 1}, request=None)
    inst = topology.standalone
    try:
        bootstrap_pid = inst.get_pid()
        bootstrap = _run_process_phase(
            inst, before, "startup-bootstrap-%s" % bootstrap_pid)

        before, _core_pattern = _artifact_snapshot()
        inst.start()

        def verify_default(current, _identity):
            assert _configured_vlv_dns(current) == []
            _assert_health(current)

        startup_pid = inst.get_pid()
        startup = _run_process_phase(
            inst, before, "startup-default-%s" % startup_pid,
            verify_default)
        return {"bootstrap": bootstrap, "default_restart": startup}
    finally:
        _cleanup_instance(inst)


def _run_explicit_vlv_lifecycle():
    before, _core_pattern = _artifact_snapshot()
    topology = create_topology({ReplicaRole.STANDALONE: 1}, request=None)
    inst = topology.standalone
    try:
        reindex_pid = inst.get_pid()
        reindex = _run_process_phase(
            inst, before, "explicit-vlv-reindex-%s" % reindex_pid,
            _configure_and_reindex_vlv)

        starts = []
        for start_index in (1, 2):
            before, _core_pattern = _artifact_snapshot()
            inst.start()
            pid = inst.get_pid()
            starts.append(_run_process_phase(
                inst, before,
                "explicit-vlv-start-%s-pid-%s" % (start_index, pid),
                lambda current, _identity: _verify_explicit_vlv(current),
            ))
        return {
            "reindex": reindex,
            "restart_1": starts[0],
            "restart_2": starts[1],
        }
    finally:
        _cleanup_instance(inst)


def _run_setup_matched_case(operation_count):
    before, _core_pattern = _artifact_snapshot()
    topology = create_topology({ReplicaRole.STANDALONE: 1}, request=None)
    inst = topology.standalone
    try:
        original_pid = inst.get_pid()

        def run_operations(current, _identity):
            assert current.get_db_lib() == "mdb"
            with ownership_workload(current, remove_on_exit=False):
                current.config.set("nsslapd-errorlog-logbuffering", "off")
                level = int(current.config.get_attr_val_utf8(
                    "nsslapd-errorlog-level") or "0")
                current.config.set(
                    "nsslapd-errorlog-level",
                    str(level | 1 | 32 | 524288))

                with _error_log_window(current.ds_paths.error_log) as window:
                    for _iteration in range(operation_count):
                        result_type, dns = search_dns(
                            current, NOT_FIRST_EMPTY_RANGE_FILTER,
                            base=OWNERSHIP_TEST_BASE,
                            scope=ldap.SCOPE_SUBTREE,
                        )
                        assert result_type == ldap.RES_SEARCH_RESULT
                        assert dns == []
                _assert_diagnostics(window["text"], operation_count)
                _assert_health(current, OWNERSHIP_TEST_BASE)
                assert current.status()
                assert current.get_pid() == original_pid

        return _run_process_phase(
            inst, before,
            "not-first-ops-%03d-pid-%s" %
            (operation_count, original_pid),
            run_operations)
    finally:
        _cleanup_instance(inst)


def _or_filter(values):
    return "(|%s)" % "".join("(uid=%s)" % value for value in values)


def _connection_search_dns(conn, filterstr, serverctrls=None):
    msgid = conn.search_ext(
        DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filterstr, ["1.1"],
        serverctrls=serverctrls,
    )
    result_type, result_data, _, result_controls = conn.result3(msgid)
    assert result_type == ldap.RES_SEARCH_RESULT
    dns = sorted(ensure_str(dn).lower() for dn, _ in result_data if dn)
    return dns, result_controls


def _response_page_control(controls):
    matches = [control for control in controls
               if control.controlType ==
               SimplePagedResultsControl.controlType]
    assert len(matches) == 1
    return matches[0]


def _complete_paged_dns(conn, filterstr):
    request = SimplePagedResultsControl(
        True, size=OR_PAGE_SIZE, cookie="")
    dns = []
    while True:
        page_dns, controls = _connection_search_dns(
            conn, filterstr, serverctrls=[request])
        dns.extend(page_dns)
        response = _response_page_control(controls)
        if not response.cookie:
            return sorted(dns)
        request.cookie = response.cookie


def _assert_connection_health(conn):
    msgid = conn.search_ext(
        DEFAULT_SUFFIX, ldap.SCOPE_BASE, "(objectClass=*)", ["1.1"])
    result_type, result_data, _, _ = conn.result3(msgid)
    assert result_type == ldap.RES_SEARCH_RESULT
    assert [ensure_str(dn).lower() for dn, _ in result_data] == [
        DEFAULT_SUFFIX.lower()
    ]


def _run_or_paging_case():
    before, _core_pattern = _artifact_snapshot()
    topology = create_topology({ReplicaRole.STANDALONE: 1}, request=None)
    inst = topology.standalone
    try:
        original_pid = inst.get_pid()

        def run_operations(current, _identity):
            assert current.config.get_attr_val_utf8_l(
                OR_LOOKUP_ATTR) == "on"
            users = UserAccounts(current, DEFAULT_SUFFIX)
            created = [users.create_test_user(
                uid=49000 + index, gid=49000)
                for index in range(OR_ENTRY_COUNT)]
            values = ["test_user_%s" % (49000 + index)
                      for index in range(OR_ENTRY_COUNT)]
            expected = sorted(entry.dn.lower() for entry in created)
            filterstr = _or_filter(values)

            cancel_conn = DirectoryManager(current).bind()
            request = SimplePagedResultsControl(
                True, size=OR_PAGE_SIZE, cookie="")
            try:
                complete, _controls = _connection_search_dns(
                    cancel_conn, filterstr)
                assert complete == expected
                first_page, controls = _connection_search_dns(
                    cancel_conn, filterstr, serverctrls=[request])
                assert len(first_page) == OR_PAGE_SIZE
                assert set(first_page) < set(expected)
                response = _response_page_control(controls)
                assert response.cookie
                request.size = 0
                request.cookie = response.cookie
                cancelled, controls = _connection_search_dns(
                    cancel_conn, filterstr, serverctrls=[request])
                assert cancelled == []
                assert not _response_page_control(controls).cookie
                _assert_connection_health(cancel_conn)
            finally:
                cancel_conn.unbind_s()

            first_conn = DirectoryManager(current).bind()
            request = SimplePagedResultsControl(
                True, size=OR_PAGE_SIZE, cookie="")
            try:
                first_page, controls = _connection_search_dns(
                    first_conn, filterstr, serverctrls=[request])
                assert len(first_page) == OR_PAGE_SIZE
                assert set(first_page) < set(expected)
                assert _response_page_control(controls).cookie
            finally:
                first_conn.unbind_s()

            second_conn = DirectoryManager(current).bind()
            try:
                _assert_connection_health(second_conn)
                assert _complete_paged_dns(
                    second_conn, filterstr) == expected
            finally:
                second_conn.unbind_s()
            assert current.status()
            assert current.get_pid() == original_pid

        return _run_process_phase(
            inst, before, "or-paging-pid-%s" % original_pid,
            run_operations)
    finally:
        _cleanup_instance(inst)


def _assert_target_matrix(results):
    target = {
        count: result["totals"]["not-first-idl"]
        for count, result in results.items()
    }
    expected = os.environ.get("DS_EXPECT_TARGET_LEAK", "absent")
    assert expected in {"present", "absent"}, expected
    blocks = {
        count: [
            block
            for report in result["reports"]
            for block in report["blocks"]
            if block["classification"] == "not-first-idl"
        ]
        for count, result in results.items()
    }
    if expected == "present":
        assert target[0] == {"bytes": 0, "objects": 0}
        assert blocks[0] == []
        unit = target[1]
        assert 120 <= unit["bytes"] <= 200, target
        assert len(blocks[1]) == 2, blocks[1]
        assert sorted(block["objects"] for block in blocks[1]) == [1, 1]
        unit_bytes = sorted(block["bytes"] for block in blocks[1])
        for count in (10, 100):
            assert len(blocks[count]) == 2, blocks[count]
            assert sorted(block["objects"] for block in blocks[count]) == [
                count, count
            ]
            assert sorted(block["bytes"] for block in blocks[count]) == [
                size * count for size in unit_bytes
            ]
            assert target[count]["bytes"] == unit["bytes"] * count, target
            assert target[count]["objects"] == unit["objects"] * count, target
    else:
        assert all(value == {"bytes": 0, "objects": 0}
                   for value in target.values()), target
        assert all(value == [] for value in blocks.values()), blocks


def _summary_values(*groups):
    summaries = []
    for group in groups:
        if group is None:
            continue
        if isinstance(group, dict) and "totals" not in group:
            summaries.extend(group.values())
        else:
            summaries.append(group)
    return summaries


def _assert_tls_matrix(baseline, explicit_vlv, all_summaries):
    expected = os.environ.get("DS_EXPECT_TLS_LEAK", "absent")
    assert expected in {"present", "absent"}, expected
    if expected == "present":
        for classification in TLS_CLASSIFICATIONS:
            assert sum(summary["totals"][classification]["bytes"]
                       for summary in baseline.values()) > 0
            assert sum(summary["totals"][classification]["bytes"]
                       for summary in explicit_vlv.values()) > 0
    else:
        for classification in TLS_CLASSIFICATIONS:
            assert all(summary["totals"][classification] == {
                "bytes": 0, "objects": 0
            } for summary in all_summaries)


def _assert_no_report_artifacts(summaries):
    if os.environ.get("DS_REQUIRE_NO_REPORTS") == "1":
        assert all(summary["report_artifacts"] == []
                   for summary in summaries)

def test_not_first_empty_range_asan_lifecycle(request):
    """Gate NOT-first, MDB TLS, VLV, and OR lifecycles under ASan.

    :id: 3b4f9ad5-c11a-41f4-a0e3-9bfe3f1990f4
    :setup: Fresh compile-time ASan MDB processes with matching debuginfo
    :steps:
        1. Stop bootstrap and repeated default processes with no VLV objects
        2. Reindex an explicit VLV and stop two restarted processes
        3. Run setup-matched 0, 1, 10, and 100 NOT-first searches
        4. Cancel and disconnect eligible paged OR searches
        5. Archive, symbolize, and classify every report and core
    :expectedresults:
        1. Default startup has no configured VLV and every stop is clean
        2. Both TLS roots are reproduced before and absent after their fixes
        3. Both NOT-first chains scale before and are absent after their fix
        4. OR results, cancellation, reconnect, and health checks are exact
        5. No invalid access, core, unknown report, or unknown leak is present
    """
    assert request.config.getoption("--sanitizer") is None
    target_expectation = os.environ.get("DS_EXPECT_TARGET_LEAK", "absent")
    assert target_expectation in {"present", "absent", "skip"}

    baseline = _run_startup_baseline()
    explicit_vlv = _run_explicit_vlv_lifecycle()
    results = {}
    or_paging = None
    if target_expectation != "skip":
        results = {
            count: _run_setup_matched_case(count)
            for count in (0, 1, 10, 100)
        }
        _assert_target_matrix(results)
        or_paging = _run_or_paging_case()

    summaries = _summary_values(
        baseline, explicit_vlv, results, or_paging)
    _assert_tls_matrix(baseline, explicit_vlv, summaries)
    _assert_no_report_artifacts(summaries)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
