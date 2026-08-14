#!/bin/bash
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
# One-time host preparation for runtime sanitizer injection.
# Run as root before using pytest --sanitizer=lsan|tsan.
#
# Usage:
#   sudo dirsrvtests/sanitizers/setup_host.sh [lsan|tsan|all]

set -e

TARGET=${1:-all}

if [[ "$TARGET" != "lsan" && "$TARGET" != "tsan" && "$TARGET" != "all" ]]; then
    echo "Usage: $0 [lsan|tsan|all]" >&2
    exit 1
fi

echo "=== 389-ds-base: sanitizer host setup ==="

install_packages() {
    local pkgs=()
    if [[ "$TARGET" == "lsan" || "$TARGET" == "all" ]]; then
        pkgs+=(liblsan)
    fi
    if [[ "$TARGET" == "tsan" || "$TARGET" == "all" ]]; then
        pkgs+=(libtsan)
    fi
    if [[ ${#pkgs[@]} -gt 0 ]]; then
        echo "Installing: ${pkgs[*]}"
        dnf install -y "${pkgs[@]}"
    fi
}

# Sanitizers need ptrace-like access for symbolization and thread suspension.
setup_sysctl() {
    local val

    val=$(sysctl -n fs.suid_dumpable 2>/dev/null || echo 0)
    if [[ "$val" != "1" ]]; then
        echo "Setting fs.suid_dumpable=1 (current: $val)"
        sysctl -w fs.suid_dumpable=1
    else
        echo "fs.suid_dumpable already set to 1"
    fi

    # LSan/TSan use ptrace to suspend threads for scanning.
    # Yama ptrace_scope=1 (default on Fedora) restricts ptrace to parent processes.
    val=$(sysctl -n kernel.yama.ptrace_scope 2>/dev/null || echo "n/a")
    if [[ "$val" == "1" || "$val" == "2" || "$val" == "3" ]]; then
        echo "Setting kernel.yama.ptrace_scope=0 (current: $val)"
        sysctl -w kernel.yama.ptrace_scope=0
    else
        echo "kernel.yama.ptrace_scope already set to $val"
    fi

    # Persist across reboots
    cat > /etc/sysctl.d/99-ds-sanitizers.conf <<'SYSCTL'
fs.suid_dumpable = 1
kernel.yama.ptrace_scope = 0
SYSCTL
}

setup_selinux() {
    if ! command -v getenforce &>/dev/null; then
        echo "SELinux not available, skipping"
        return
    fi
    if [[ "$(getenforce)" == "Disabled" ]]; then
        echo "SELinux disabled, skipping"
        return
    fi

    # Install a targeted policy module that grants dirsrv_t only the
    # permissions the sanitizer runtime needs (ptrace self, execmem).
    # This is much narrower than domain_can_mmap_files or setenforce 0.
    local script_dir
    script_dir="$(cd "$(dirname "$0")" && pwd)"
    local te_file="${script_dir}/ds_sanitizer.te"

    if [[ ! -f "$te_file" ]]; then
        echo "WARNING: $te_file not found, cannot install SELinux policy"
        echo "  Fall back to: setenforce 0"
        return
    fi

    if ! command -v checkmodule &>/dev/null; then
        echo "checkmodule not found. Install: dnf install checkpolicy"
        echo "  Or fall back to: setenforce 0"
        return
    fi

    local tmpdir
    tmpdir=$(mktemp -d)
    if checkmodule -M -m -o "${tmpdir}/ds_sanitizer.mod" "$te_file" && \
       semodule_package -o "${tmpdir}/ds_sanitizer.pp" -m "${tmpdir}/ds_sanitizer.mod" && \
       semodule -i "${tmpdir}/ds_sanitizer.pp"; then
        echo "SELinux: installed ds_sanitizer policy module"
        echo "  Remove with: semodule -r ds_sanitizer"
    else
        echo "WARNING: failed to install SELinux policy module"
        echo "  Fall back to: setenforce 0"
    fi
    rm -rf "$tmpdir"
}

install_packages
setup_sysctl
setup_selinux

echo ""
echo "=== Host setup complete ==="
echo "You can now run: pytest --sanitizer=lsan tests/..."
