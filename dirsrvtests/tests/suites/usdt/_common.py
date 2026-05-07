# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import concurrent.futures
import glob
import os
import re
import subprocess
import threading

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
