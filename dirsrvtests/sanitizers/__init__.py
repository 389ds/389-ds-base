# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
Runtime sanitizer config for pytest --sanitizer=lsan|tsan.

Injects LeakSanitizer or ThreadSanitizer into ns-slapd via LD_PRELOAD
without requiring an instrumented build. See README.md and setup_host.sh.
"""

import glob
import os

# Sanitizer name -> runtime configuration.
# lib_glob targets x86_64 (Fedora/RHEL); adjust for other architectures.
SANITIZERS = {
    'lsan': {
        'lib_glob': '/usr/lib64/liblsan.so*',
        'package': 'liblsan',
        'env_var': 'LSAN_OPTIONS',
        'options': 'detect_leaks=1:exitcode=0:print_suppressions=0',
    },
    'tsan': {
        'lib_glob': '/usr/lib64/libtsan.so*',
        'package': 'libtsan',
        'env_var': 'TSAN_OPTIONS',
        'options': 'exitcode=0:second_deadlock_stack=1',
    },
}


def find_library(lib_glob):
    """Return first matching .so path, or None.

    Globs because the version suffix varies across distros
    (liblsan.so.0 vs liblsan.so.1).
    """
    for path in sorted(glob.glob(lib_glob)):
        if os.path.isfile(path):
            return path
    return None
