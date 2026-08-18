# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import glob
import logging
import os
import shutil
import subprocess

import pytest

from test389.topologies import topology_st as topo

from ._common import ns_slapd_path, libslapd_path, binary_has_sdt_notes

DEBUGGING = os.getenv("DEBUGGING", default=False)
log = logging.getLogger(__name__)
log.setLevel(logging.DEBUG if DEBUGGING else logging.INFO)


# Probe sets mirror STAP_PROBE call sites; keep in sync with the C source.
EXISTING_PROBES = {
    "do_search__entry", "do_search__return",
    "op_shared_search__entry", "op_shared_search__prepared",
    "op_shared_search__backends", "op_shared_search__return",
    "vslapd_log_audit__entry", "vslapd_log_audit__prepared",
    "vslapd_log_audit__buffer",
    "vslapd_log_auditfail__entry", "vslapd_log_auditfail__prepared",
    "vslapd_log_auditfail__buffer",
    "vslapd_log_access__entry", "vslapd_log_access__prepared",
    "vslapd_log_access__buffer",
    "vslapd_log_security__entry", "vslapd_log_security__prepared",
    "vslapd_log_security__buffer",
}
WORK_QUEUE_PROBES = {
    "work_q__enqueue", "work_q__dequeue",
    "worker__busy", "worker__idle",
    "work__blocked",
}

EXPECTED_ARG_COUNTS = {
    "do_search__entry": 0, "do_search__return": 0,
    "op_shared_search__entry": 0, "op_shared_search__prepared": 0,
    "op_shared_search__backends": 0, "op_shared_search__return": 0,
    "vslapd_log_audit__entry": 0, "vslapd_log_audit__prepared": 0,
    "vslapd_log_audit__buffer": 0,
    "vslapd_log_auditfail__entry": 0, "vslapd_log_auditfail__prepared": 0,
    "vslapd_log_auditfail__buffer": 0,
    "vslapd_log_access__entry": 0, "vslapd_log_access__prepared": 0,
    "vslapd_log_access__buffer": 0,
    "vslapd_log_security__entry": 0, "vslapd_log_security__prepared": 0,
    "vslapd_log_security__buffer": 0,
    "work_q__enqueue": 3,
    "work_q__dequeue": 3,
    "worker__busy": 4,
    "worker__idle": 1,
    "work__blocked": 2,
}

PROFILING_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__),
                 "..", "..", "..", "..", "profiling", "stap")
)


def _elf_targets(topo):
    yield ns_slapd_path(topo)
    lib = libslapd_path(topo)
    if lib:
        yield lib


def _readelf_notes(binary):
    return subprocess.run(
        ["readelf", "-n", binary],
        capture_output=True, text=True, check=True,
    ).stdout


def _read_probe_names(binary):
    names = set()
    for line in _readelf_notes(binary).splitlines():
        line = line.strip()
        if line.startswith("Name:"):
            names.add(line.split(":", 1)[1].strip())
    return names


def _read_all_probe_names(topo):
    found = set()
    for path in _elf_targets(topo):
        if path and os.path.exists(path):
            found |= _read_probe_names(path)
    return found


def _read_probe_arg_specs(binary):
    """{probe_name: [arg_specs]} parsed from readelf -n."""

    specs = {}
    current = None
    for line in _readelf_notes(binary).splitlines():
        line = line.strip()
        if line.startswith("Name:"):
            current = line.split(":", 1)[1].strip()
        elif line.startswith("Arguments:") and current is not None:
            args = line.split(":", 1)[1].strip()
            specs[current] = args.split() if args else []
            current = None
    return specs


def _read_all_probe_arg_specs(topo):
    out = {}
    for path in _elf_targets(topo):
        if path and os.path.exists(path):
            out.update(_read_probe_arg_specs(path))
    return out


def _usdt_targets_or_skip(topo):
    targets = [p for p in _elf_targets(topo)
               if p and os.path.exists(p) and binary_has_sdt_notes(p)]
    if not targets:
        pytest.skip("ns-slapd not built with --enable-usdt "
                    "(no stapsdt notes in ns-slapd or libslapd.so)")
    return targets


@pytest.mark.tier1
@pytest.mark.skipif(not shutil.which("readelf"),
                    reason="readelf (binutils) is required")
def test_usdt_probes_compiled_in(topo):
    """All expected probes are present in ns-slapd / libslapd.so.

    :id: e71ae58c-cbd9-41ab-912c-172f60e027b0
    :setup: Standalone instance
    :steps:
        1. Locate ns-slapd and libslapd.so, skip if no stapsdt notes
        2. Read every embedded probe name via readelf -n
        3. Assert EXISTING_PROBES is a subset
        4. Assert WORK_QUEUE_PROBES is a subset
    :expectedresults:
        1. Skip cleanly when not built with --enable-usdt
        2. Probe-name set is non-empty
        3. No regressions in existing probes
        4. All four new probes present
    """

    _usdt_targets_or_skip(topo)
    found = _read_all_probe_names(topo)
    log.info("Found %d USDT probes", len(found))
    log.debug("Probes: %s", sorted(found))

    missing_existing = sorted(EXISTING_PROBES - found)
    assert not missing_existing, (
        f"Existing USDT probes missing (regression!): {missing_existing}"
    )
    missing_new = sorted(WORK_QUEUE_PROBES - found)
    assert not missing_new, (
        f"New work-queue/worker USDT probes missing: {missing_new}"
    )


@pytest.mark.tier1
@pytest.mark.skipif(not shutil.which("readelf"),
                    reason="readelf (binutils) is required")
def test_probe_argument_counts_match(topo):
    """Compiled-in probe argument counts match their STAP_PROBE<N> call sites.

    :id: da0c7c41-637c-4805-9c18-2fe4a08af9e9
    :setup: Standalone instance
    :steps:
        1. Read probe arg specs from both ELFs via readelf -n
        2. Compare each probe's actual arg count against EXPECTED_ARG_COUNTS
    :expectedresults:
        1. Specs parse
        2. All counts match
    """

    _usdt_targets_or_skip(topo)
    specs = _read_all_probe_arg_specs(topo)

    mismatches = []
    for probe, expected in EXPECTED_ARG_COUNTS.items():
        if probe not in specs:
            mismatches.append(f"{probe}: missing from binary entirely")
            continue
        actual = len(specs[probe])
        if actual != expected:
            mismatches.append(
                f"{probe}: expected {expected} args, got {actual} "
                f"({specs[probe]})"
            )
    assert not mismatches, (
        "Probe argument count mismatch:\n  " + "\n  ".join(mismatches)
    )


@pytest.mark.tier1
@pytest.mark.skipif(not shutil.which("readelf"),
                    reason="readelf (binutils) is required")
def test_no_unexpected_probes(topo):
    """Every compiled-in probe is declared in EXPECTED_ARG_COUNTS.

    :id: 891e9048-a32e-4a6e-8b24-6c9b3fa6fef9
    :setup: Standalone instance
    :steps:
        1. Read all probe names from both ELFs
        2. Diff against EXPECTED_ARG_COUNTS keys
    :expectedresults:
        1. Probe-name set is non-empty
        2. Every binary-resident probe is declared in this test
    """

    _usdt_targets_or_skip(topo)
    found = _read_all_probe_names(topo)
    unknown = sorted(found - set(EXPECTED_ARG_COUNTS.keys()))
    assert not unknown, (
        f"Probes found in binary but not declared in this test: {unknown}. "
        f"Add them to EXPECTED_ARG_COUNTS (and EXISTING_PROBES or "
        f"WORK_QUEUE_PROBES)."
    )


@pytest.mark.tier1
@pytest.mark.skipif(not shutil.which("stap"),
                    reason="systemtap (stap) is required")
def test_stp_scripts_parse(topo):
    """All shipped .stp scripts parse cleanly via stap -p1.

    :id: 18ef9213-753f-48bd-83c5-d0c06dd6e95d
    :setup: Standalone instance
    :steps:
        1. Glob systemtap scripts in the profiling/stap directory
        2. Run stap -p1 <script> <ns-slapd> <libslapd.so> for each
    :expectedresults:
        1. At least one script found
        2. All exit zero
    """

    if not os.path.isdir(PROFILING_DIR):
        pytest.skip(f"profiling/stap directory not present at {PROFILING_DIR}")

    scripts = sorted(glob.glob(os.path.join(PROFILING_DIR, "*.stp")))
    assert scripts, f"no .stp scripts found in {PROFILING_DIR}"

    ns_slapd = ns_slapd_path(topo)
    libslapd = libslapd_path(topo) or ns_slapd

    failures = []
    for script in scripts:
        proc = subprocess.run(
            ["stap", "-p1", script, ns_slapd, libslapd],
            capture_output=True, text=True,
        )
        if proc.returncode != 0:
            failures.append(
                f"{os.path.basename(script)}:\n    {proc.stderr.strip()}"
            )
        else:
            log.info("stap -p1 OK: %s", os.path.basename(script))

    assert not failures, (
        "stap -p1 failed for:\n  " + "\n  ".join(failures)
    )


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
