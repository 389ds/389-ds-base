# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import subprocess
import uuid

import pytest

from lib389._constants import DEFAULT_BENAME, DEFAULT_SUFFIX, DN_DM
from test389.topologies import topology_st as topo
from lib389.dbgen import dbgen_users
from tempfile import TemporaryDirectory

DSCONF = '/usr/sbin/dsconf'
DSCTL = '/usr/sbin/dsctl'
WATCH_TEST_NUM_USERS = 20000


def _dsconf_base_cmd(inst):
    return [DSCONF, inst.serverid, '-D', DN_DM, '-w', 'password']


def _run_dsconf(inst, argv, timeout=7200):
    """Run dsconf; return (returncode, combined stdout+stderr)."""
    cmd = _dsconf_base_cmd(inst) + argv
    proc = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    out = (proc.stdout or '') + (proc.stderr or '')
    return proc.returncode, out


def _non_empty_lines(text):
    return [ln for ln in text.splitlines() if ln.strip()]


def _dsctl_base_cmd(inst):
    return [DSCTL, inst.serverid]


def _run_dsctl(inst, argv, timeout=7200):
    """Run dsctl; return (returncode, combined stdout+stderr)."""
    cmd = _dsctl_base_cmd(inst) + argv
    proc = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    out = (proc.stdout or '') + (proc.stderr or '')
    return proc.returncode, out


def test_dsctl_task_watch_output(topo):
    """dsctl db2ldif, ldif2db, db2bak, bak2db: ``--watch`` yields more output than default

    Offline ns-slapd tools are run with ``-V`` when ``--watch`` is set; that verbose output is
    logged at INFO so it is visible without ``dsctl -v``.

    :id: 3f8a2c10-9e4b-4d7a-8f2e-1c0d5b6a7e9f
    :setup: Standalone Instance (stopped for offline db utilities)
    :steps:
        1. Online-import a large LDIF via dsconf to populate the database
        2. Stop the server; run ``db2ldif`` without then with ``--watch``
        3. Run ``ldif2db`` twice with the same LDIF (without / with ``--watch``)
        4. Run ``db2bak`` to two new archive paths (without / with ``--watch``)
        5. Run ``bak2db`` from each archive (without / with ``--watch``)
        6. Start the server
    :expectedresults:
        1. Each dsctl invocation succeeds (rc 0)
        2. For each pair, the ``--watch`` run has strictly more non-empty output lines
    """
    inst = topo.standalone
    tag = uuid.uuid4().hex[:12]
    be = DEFAULT_BENAME

    ldif_populate = os.path.join(inst.ldifdir, f'dsctl_watch_populate_{tag}.ldif')
    dbgen_users(
        inst,
        WATCH_TEST_NUM_USERS,
        ldif_populate,
        DEFAULT_SUFFIX,
        parent='ou=people,' + DEFAULT_SUFFIX,
        generic=True,
    )

    rc, out = _run_dsconf(inst, ['backend', 'import', be, ldif_populate])
    assert rc == 0, out

    inst.stop()

    try:
        export_a = os.path.join(inst.ldifdir, f'dsctl_watch_db2ldif_a_{tag}.ldif')
        export_b = os.path.join(inst.ldifdir, f'dsctl_watch_db2ldif_b_{tag}.ldif')

        # db2ldif
        rc, out_db2ldif_nowatch = _run_dsctl(inst, ['db2ldif', be, export_a])
        assert rc == 0, out_db2ldif_nowatch
        lines_db2ldif_nowatch = len(_non_empty_lines(out_db2ldif_nowatch))
        assert "export userRoot: " not in out_db2ldif_nowatch, "'export userRoot: ' should not be in the output"

        rc, out_db2ldif_watch = _run_dsctl(inst, ['db2ldif', be, export_b, '--watch'])
        assert rc == 0, out_db2ldif_watch
        lines_db2ldif_watch = len(_non_empty_lines(out_db2ldif_watch))
        assert lines_db2ldif_watch > lines_db2ldif_nowatch, (
            f'db2ldif --watch should log more lines (got {lines_db2ldif_watch} vs {lines_db2ldif_nowatch})'
        )
        assert "export userRoot: " in out_db2ldif_watch, "'export userRoot: ' should be in the output"

        # ldif2db
        rc, out_ldif2db_nowatch = _run_dsctl(inst, ['ldif2db', be, ldif_populate])
        assert rc == 0, out_ldif2db_nowatch
        lines_ldif2db_nowatch = len(_non_empty_lines(out_ldif2db_nowatch))
        assert "import userRoot: Import complete" not in out_ldif2db_nowatch, "'import userRoot: Import complete' should not be in the output"

        rc, out_ldif2db_watch = _run_dsctl(inst, ['ldif2db', be, ldif_populate, '--watch'])
        assert rc == 0, out_ldif2db_watch
        lines_ldif2db_watch = len(_non_empty_lines(out_ldif2db_watch))
        assert lines_ldif2db_watch > lines_ldif2db_nowatch, (
            f'ldif2db --watch should log more lines (got {lines_ldif2db_watch} vs {lines_ldif2db_nowatch})'
        )
        assert "import userRoot: Import complete" in out_ldif2db_watch, "'import userRoot: Import complete' should be in the output"

        bak_a = os.path.join(inst.ds_paths.backup_dir, f'dsctl_watch_bak_a_{tag}')
        bak_b = os.path.join(inst.ds_paths.backup_dir, f'dsctl_watch_bak_b_{tag}')

        # db2bak
        rc, out_db2bak_nowatch = _run_dsctl(inst, ['db2bak', bak_a])
        assert rc == 0, out_db2bak_nowatch
        lines_db2bak_nowatch = len(_non_empty_lines(out_db2bak_nowatch))
        assert "archive_copyfile - Copying" not in out_db2bak_nowatch, "'archive_copyfile - Copying' should not be in the output"

        rc, out_db2bak_watch = _run_dsctl(inst, ['db2bak', bak_b, '--watch'])
        assert rc == 0, out_db2bak_watch
        lines_db2bak_watch = len(_non_empty_lines(out_db2bak_watch))
        assert lines_db2bak_watch > lines_db2bak_nowatch, (
            f'db2bak --watch should log more lines (got {lines_db2bak_watch} vs {lines_db2bak_nowatch})'
        )
        assert "archive_copyfile - Copying" in out_db2bak_watch, "'archive_copyfile - Copying' should be in the output"

        # bak2db
        rc, out_bak2db_nowatch = _run_dsctl(inst, ['bak2db', bak_a])
        assert rc == 0, out_bak2db_nowatch
        lines_bak2db_nowatch = len(_non_empty_lines(out_bak2db_nowatch))
        assert "- Copying" not in out_bak2db_nowatch, "'- Copying' should not be in the output"

        rc, out_bak2db_watch = _run_dsctl(inst, ['bak2db', bak_b, '--watch'])
        assert rc == 0, out_bak2db_watch
        lines_bak2db_watch = len(_non_empty_lines(out_bak2db_watch))
        assert lines_bak2db_watch > lines_bak2db_nowatch, (
            f'bak2db --watch should log more lines (got {lines_bak2db_watch} vs {lines_bak2db_nowatch})'
        )
        assert "- Copying" in out_bak2db_watch, "'- Copying' should be in the output"

    finally:
        inst.start()


def generate_ldif_file(inst, ldif, perm):
    dbgen_users(
        inst,
        WATCH_TEST_NUM_USERS/100,
        ldif,
        DEFAULT_SUFFIX,
        parent='ou=people,' + DEFAULT_SUFFIX,
        generic=True,
    )
    os.chown(ldif, 0, 0)
    os.chmod(ldif, perm)


def check_import(inst, ldif, expected_error):
    inst.log.info(f'Try importing {ldif} Expecting {expected_error}')
    rc, out = _run_dsctl(inst, ['ldif2db', DEFAULT_BENAME, ldif])
    inst.log.info(f'Get: {out}')
    assert expected_error in out


@pytest.mark.skipif(os.getuid() != 0, reason="Test not run by root")
def test_dsctl_ldif2db_file_access(topo):
    """Check that import file is readable

    :id: 642c331e-4252-11f1-9fad-c85309d5c3e3
    :setup: Standalone Instance (stopped for offline db utilities)
    :steps:
        1. Stop the server
        2. Run ``ldif2db`` on a file with 0644 permission
        3. Run ``ldif2db`` on a file with 0600 permission
        4. Run ``ldif2db`` on a directory with 0711 permission
        5. Run ``ldif2db`` on a directory with 0700 permission
        6. Run ``ldif2db`` on a file within directory with 0700 permission
        7. Start the server
    :expectedresults:
        1. Success
        2. Success
        3. dsctl should fail with 'User dirsrv cannot read file' error
        4. dsctl should fail with "Can't find file" error
        5. dsctl should fail with "Can't find file" error
        6. dsctl should fail with 'User dirsrv cannot read file' error
        7. Success
    """
    inst = topo.standalone

    inst.stop()

    try:
        with TemporaryDirectory() as dir:
            os.chmod(dir, 0o711)
            d1 = f'{dir}/d1'
            os.mkdir(d1)
            os.chmod(d1, 0o700)
            ldif1 = f'{dir}/db1.ldif'
            ldif2 = f'{dir}/db2.ldif'
            ldif3 = f'{d1}/db3.ldif'
            generate_ldif_file(inst, ldif1, 0o644)
            generate_ldif_file(inst, ldif2, 0o600)
            generate_ldif_file(inst, ldif3, 0o644)
            check_import(inst, ldif1, 'ldif2db successful')
            check_import(inst, ldif2, 'ldif2db: User dirsrv cannot read')
            check_import(inst, dir, "ldif2db: Can't find file")
            check_import(inst, d1, "ldif2db: Can't find file")
            check_import(inst, ldif3, 'ldif2db: User dirsrv cannot read')
    finally:
        inst.start()

        
if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(['-s', CURRENT_FILE])
