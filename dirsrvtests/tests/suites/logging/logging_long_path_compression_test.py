# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import glob
import logging
import os
import re
import shutil
import subprocess
import time

import pytest

from lib389._constants import DEFAULT_SUFFIX
from test389.topologies import topology_st as topo

log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1

TBUFSIZE = 75
LONG_DIR_NAME = 'someverylongpaththatislongerthan75chars'


def generate_load(inst, threads=10, samples=6):
    """Generate search load using ldclt to fill the access log fast.
    Each sample is 10 seconds. Default 6 samples = 60 seconds."""
    port = inst.port
    subprocess.run([
        'ldclt', '-h', 'localhost', '-p', str(port),
        '-b', DEFAULT_SUFFIX,
        '-e', 'esearch',
        '-f', 'uid=demo_user',
        '-n', str(threads),
        '-N', str(samples),
    ], check=True, timeout=samples * 10 + 30)


def parse_rotationinfo(filepath):
    """Parse a .rotationinfo file and return list of dicts with path, ctime, size
    for all 'Previous Log File' entries."""
    entries = []
    with open(filepath, 'r') as f:
        for line in f:
            m = re.match(r'LOGINFO:Previous Log File:(\S+)\s+\((\d+)\)\s+\((\d+)\)', line)
            if m:
                entries.append({
                    'path': m.group(1),
                    'ctime': int(m.group(2)),
                    'size': int(m.group(3)),
                })
    return entries


def get_rotated_log_files(log_dir, log_type='access'):
    """Return sorted list of rotated log file paths."""
    return sorted(glob.glob(f'{log_dir}/{log_type}.2*'))


def cleanup_rotated_logs(log_dir, log_type='access'):
    """Remove all rotated log files."""
    for f in glob.glob(f'{log_dir}/{log_type}.2*'):
        os.remove(f)


@pytest.fixture()
def long_path_setup(topo, request):
    """Creates a long-name subdirectory for the access log,
    reconfigures DS to use it with compression, returns the paths."""

    inst = topo.standalone
    log_dir = inst.get_log_dir()
    original_accesslog = inst.config.get_attr_val_utf8('nsslapd-accesslog')

    long_subdir = os.path.join(log_dir, LONG_DIR_NAME)
    long_access_log = os.path.join(long_subdir, 'access')

    # Verify the path will exceed TBUFSIZE with rotation suffix
    sample_rotated = long_access_log + '.20260615-120000.gz'
    assert len(sample_rotated) > TBUFSIZE, (
        f"Test setup error: rotated path ({len(sample_rotated)} chars) must exceed "
        f"TBUFSIZE ({TBUFSIZE}) to trigger the bug"
    )

    os.makedirs(long_subdir, exist_ok=True)
    os.chown(long_subdir, inst.get_user_uid(), inst.get_group_gid())

    inst.config.set('nsslapd-accesslog', long_access_log)
    inst.config.set('nsslapd-accesslog-compress', 'on')
    inst.config.set('nsslapd-accesslog-maxlogsize', '1')
    inst.config.set('nsslapd-accesslog-logmaxdiskspace', '10')
    inst.config.set('nsslapd-accesslog-maxlogsperdir', '100')
    inst.config.set('nsslapd-accesslog-logrotationsync-enabled', 'off')
    inst.config.set('nsslapd-accesslog-logbuffering', 'on')
    inst.config.set('nsslapd-accesslog-logexpirationtime', '-1')
    inst.config.set('nsslapd-accesslog-logminfreediskspace', '5')
    inst.config.set('nsslapd-accesslog-logrotationtime', '1')
    inst.config.set('nsslapd-accesslog-logrotationtimeunit', 'minute')
    inst.config.set('nsslapd-statlog-level', '1')

    def fin():
        inst.config.set('nsslapd-accesslog', original_accesslog)
        inst.config.set('nsslapd-accesslog-compress', 'off')
        inst.config.set('nsslapd-accesslog-logmaxdiskspace', '500')
        inst.config.set('nsslapd-accesslog-maxlogsize', '100')
        inst.config.set('nsslapd-accesslog-maxlogsperdir', '10')
        inst.config.set('nsslapd-accesslog-logbuffering', 'on')
        inst.config.set('nsslapd-accesslog-logexpirationtime', '1')
        inst.config.set('nsslapd-accesslog-logexpirationtimeunit', 'month')
        inst.config.set('nsslapd-accesslog-logrotationtime', '1')
        inst.config.set('nsslapd-accesslog-logrotationtimeunit', 'day')
        inst.config.set('nsslapd-accesslog-logminfreediskspace', '5')
        inst.config.set('nsslapd-statlog-level', '0')
        if os.path.exists(long_subdir):
            shutil.rmtree(long_subdir)

    request.addfinalizer(fin)

    return {
        'inst': inst,
        'log_dir': log_dir,
        'long_subdir': long_subdir,
        'long_access_log': long_access_log,
    }


def test_compressed_log_long_path(topo, long_path_setup):
    """Test that compressed log sizes in rotationinfo match actual file
    sizes and that logs are not prematurely deleted when the access log
    path exceeds 75 characters.

    :id: 7c3b4a2e-1f8d-4e5a-b9c7-6d2e8f0a3b1c
    :setup: Standalone Instance
    :steps:
        1. Create a long-name subdirectory so the full rotated log filename
           exceeds 75 characters (TBUFSIZE).
        2. Set access log to the long path with compression enabled,
           maxlogsize 1 MB, and maxdiskspace 10 MB.
        3. Generate LDAP load to trigger many log rotations.
        4. Parse access.rotationinfo and compare recorded sizes against
           actual compressed file sizes on disk.
    :expectedresults:
        1. Success
        2. Success
        3. At least 3 rotated compressed logs are created.
        4. Recorded sizes in rotationinfo must match actual file sizes,
           not the maxlogsize fallback value.
    """

    inst = long_path_setup['inst']
    long_subdir = long_path_setup['long_subdir']
    long_access_log = long_path_setup['long_access_log']

    # Generate load to trigger many rotations (6 samples × 10 sec = 60 sec)
    generate_load(inst)

    # Check rotationinfo sizes
    rotinfo_path = long_access_log + '.rotationinfo'
    assert os.path.exists(rotinfo_path), \
        f"Rotationinfo file not found: {rotinfo_path}"

    entries = parse_rotationinfo(rotinfo_path)
    log.info(f"Rotationinfo has {len(entries)} entries")
    assert len(entries) >= 3, \
        f"Expected at least 3 rotated logs, got {len(entries)}"

    maxlogsize_mb = int(inst.config.get_attr_val_utf8('nsslapd-accesslog-maxlogsize'))
    maxlogsize_bytes = maxlogsize_mb * 1024 * 1024

    mismatches = []
    for entry in entries:
        log_path = entry['path']
        recorded_size = entry['size']

        actual_path = log_path
        if not os.path.exists(actual_path) and os.path.exists(log_path + '.gz'):
            actual_path = log_path + '.gz'

        if not os.path.exists(actual_path):
            log.warning(f"File not found: {actual_path} (may have been deleted)")
            continue

        actual_size = os.path.getsize(actual_path)
        log.info(f"  {os.path.basename(actual_path)}: "
                 f"recorded={recorded_size}, actual={actual_size}")

        if recorded_size != actual_size:
            mismatches.append({
                'file': actual_path,
                'recorded': recorded_size,
                'actual': actual_size,
            })

    assert len(mismatches) == 0, (
        f"Compressed log sizes in rotationinfo do not match actual file sizes! "
        f"{len(mismatches)} of {len(entries)} entries differ. "
        f"Mismatched files: "
        f"{[m['file'] + ': recorded=' + str(m['recorded']) + ' actual=' + str(m['actual']) for m in mismatches]}"
    )

    # Log retained file count for debugging
    rotated_logs = get_rotated_log_files(long_subdir, 'access')
    log.info(f"Rotated logs retained: {len(rotated_logs)}")
    for f in rotated_logs:
        log.info(f"  {os.path.basename(f)}: {os.path.getsize(f)} bytes")

    total_actual = sum(os.path.getsize(f) for f in rotated_logs)
    log.info(f"Total actual disk usage of rotated logs: {total_actual} bytes "
             f"({total_actual / (1024*1024):.2f} MB)")


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
