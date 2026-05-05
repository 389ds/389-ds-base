# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import subprocess
import uuid

import pytest

from lib389._constants import DEFAULT_SUFFIX, DN_DM
# from test389.topologies import topology_m1 as topo
from test389.topologies import topology_st as topo
from lib389.tasks import (ImportTask, ExportTask, BackupTask, RestoreTask, AutomemberRebuildMembershipTask,
                          AutomemberAbortRebuildTask, MemberUidFixupTask, MemberOfFixupTask, USNTombstoneCleanupTask,
                          DBCompactTask, EntryUUIDFixupTask, SchemaReloadTask, SyntaxValidateTask,
                          FixupLinkedAttributesTask)
from lib389.plugins import USNPlugin, POSIXWinsyncPlugin, LinkedAttributesPlugin, AutoMembershipPlugin, MemberOfPlugin
from lib389.dbgen import dbgen_users
from lib389.idm.user import UserAccount
from lib389.idm.group import Groups
from lib389.idm.posixgroup import PosixGroups

log = logging.getLogger(__name__)

DSCONF = '/usr/sbin/dsconf'
# Enough entries for import/export/reindex to emit multiple nsTaskLog updates when --watch is used.
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


def test_dsconf_task_watch_output(topo):
    """dsconf backend / backup tasks: ``--watch`` yields more output than default

    Verifies that ``task.watch()``-driven progress (incremental nsTaskLog lines) appears when
    ``-w`` / ``--watch`` is passed, and that the non-watch paths stay comparatively minimal.

    Reindex is compared with ``--wait`` alone versus ``--wait --watch``; without ``--wait``,
    ``backend index reindex`` returns immediately and does not wait for the task.

    Backup ``create`` / ``restore`` use ``--timeout 0`` so the default 120s task limit does not
    fire on a large database.

    :id: 7c4e8a91-2b0d-4f6e-9c1d-8a3e5f7b0d2c
    :setup: Standalone Instance
    :steps:
        1. Online-import a large LDIF without ``--watch``; capture output line count
        2. Re-import the same LDIF with ``--watch``; capture output — expect more lines
        3. Export without ``--watch``; then export with ``--watch`` — expect more lines on watch
        4. ``backend index reindex`` with ``--wait`` only vs ``--wait --watch`` — expect more lines with watch
        5. ``backup create`` to a dedicated directory without ``--watch``, then to another with ``--watch``
        6. ``backup restore`` from each archive without / with ``--watch``
    :expectedresults:
        1. Import succeeds; non-watch output has few lines
        2. Import with watch succeeds; strictly more non-empty log lines than without watch
        3. Both exports succeed; watch run is more verbose
        4. Both reindex runs succeed; watch run is more verbose
        5. Both backup creates succeed; watch run is more verbose
        6. Both restores succeed; watch run is more verbose
    """
    inst = topo.standalone
    be = 'userRoot'
    tag = uuid.uuid4().hex[:12]
    import_ldif = os.path.join(inst.ldifdir, 'dsconf_watch_test_import.ldif')
    export_ldif_a = os.path.join(inst.ldifdir, 'dsconf_watch_test_export_a.ldif')
    export_ldif_b = os.path.join(inst.ldifdir, 'dsconf_watch_test_export_b.ldif')

    dbgen_users(
        inst,
        WATCH_TEST_NUM_USERS,
        import_ldif,
        DEFAULT_SUFFIX,
        parent='ou=people,' + DEFAULT_SUFFIX,
        generic=True,
    )

    # import
    rc, out_imp_nowatch = _run_dsconf(
        inst,
        ['backend', 'import', be, import_ldif],
    )
    assert rc == 0, out_imp_nowatch
    lines_imp_nowatch = len(_non_empty_lines(out_imp_nowatch))
    assert "Processing file" not in out_imp_nowatch, "'Processing file' should not be in the output"

    rc, out_imp_watch = _run_dsconf(
        inst,
        ['backend', 'import', be, import_ldif, '--watch'],
    )
    assert rc == 0, out_imp_watch
    lines_imp_watch = len(_non_empty_lines(out_imp_watch))
    assert lines_imp_watch > lines_imp_nowatch, (
        f'import --watch should log more lines than without (got {lines_imp_watch} vs {lines_imp_nowatch})'
    )
    assert "Processing file" in out_imp_watch, "'Processing file' should be in the output"

    # export
    rc, out_exp_nowatch = _run_dsconf(
        inst,
        ['backend', 'export', be, '-l', export_ldif_a],
    )
    assert rc == 0, out_exp_nowatch
    lines_exp_nowatch = len(_non_empty_lines(out_exp_nowatch))
    assert "userRoot: Processed" not in out_exp_nowatch, "'userRoot: Processed' should not be in the output"

    rc, out_exp_watch = _run_dsconf(
        inst,
        ['backend', 'export', be, '-l', export_ldif_b, '--watch'],
    )
    assert rc == 0, out_exp_watch
    lines_exp_watch = len(_non_empty_lines(out_exp_watch))
    assert lines_exp_watch > lines_exp_nowatch, (
        f'export --watch should log more lines than without (got {lines_exp_watch} vs {lines_exp_nowatch})'
    )
    assert "userRoot: Processed" in out_exp_watch, "'userRoot: Processed' should be in the output"

    # reindex
    rc, out_idx_nowatch = _run_dsconf(
        inst,
        ['backend', 'index', 'reindex', be, '--wait'],
    )
    assert rc == 0, out_idx_nowatch
    lines_idx_nowatch = len(_non_empty_lines(out_idx_nowatch))
    assert "Indexing attribute:" not in out_idx_nowatch, "'Indexing attribute:' should not be in the output"

    rc, out_idx_watch = _run_dsconf(
        inst,
        ['backend', 'index', 'reindex', be, '--wait', '--watch'],
    )
    assert rc == 0, out_idx_watch
    lines_idx_watch = len(_non_empty_lines(out_idx_watch))
    assert lines_idx_watch > lines_idx_nowatch, (
        f'reindex --watch should log more lines than --wait alone (got {lines_idx_watch} vs {lines_idx_nowatch})'
    )
    assert "Indexing attribute:" in out_idx_watch, "'Indexing attribute:' should be in the output"

    # backup create
    bak_create_a = os.path.join(inst.ds_paths.backup_dir, f'dsconf_watch_backup_create_a_{tag}')
    bak_create_b = os.path.join(inst.ds_paths.backup_dir, f'dsconf_watch_backup_create_b_{tag}')

    rc, out_bak_c_nowatch = _run_dsconf(
        inst,
        ['backup', 'create', bak_create_a],
    )
    assert rc == 0, out_bak_c_nowatch
    lines_bak_c_nowatch = len(_non_empty_lines(out_bak_c_nowatch))
    assert "Creating backup" not in out_bak_c_nowatch, "'Creating backup' should not be in the output"

    rc, out_bak_c_watch = _run_dsconf(
        inst,
        ['backup', 'create', bak_create_b, '--watch'],
    )
    assert rc == 0, out_bak_c_watch
    lines_bak_c_watch = len(_non_empty_lines(out_bak_c_watch))
    assert lines_bak_c_watch > lines_bak_c_nowatch, (
        f'backup create --watch should log more lines (got {lines_bak_c_watch} vs {lines_bak_c_nowatch})'
    )
    assert "Creating backup" in out_bak_c_watch, "'Creating backup' should be in the output"

    # backup restore
    rc, out_bak_r_nowatch = _run_dsconf(
        inst,
        ['backup', 'restore', bak_create_a, '--timeout', '0'],
    )
    assert rc == 0, out_bak_r_nowatch
    lines_bak_r_nowatch = len(_non_empty_lines(out_bak_r_nowatch))
    assert "Restoring backup" not in out_bak_r_nowatch, "'Restoring backup' should not be in the output"

    rc, out_bak_r_watch = _run_dsconf(
        inst,
        ['backup', 'restore', bak_create_b, '--timeout', '0', '--watch'],
    )
    assert rc == 0, out_bak_r_watch
    lines_bak_r_watch = len(_non_empty_lines(out_bak_r_watch))
    assert lines_bak_r_watch > lines_bak_r_nowatch, (
        f'backup restore --watch should log more lines (got {lines_bak_r_watch} vs {lines_bak_r_nowatch})'
    )
    assert "Restoring backup" in out_bak_r_watch, "'Restoring backup' should be in the output"


def test_task_timeout(topo):
    """All thath te timeoutsetting works for all "tasks"

    :id: 6a6f5176-76bf-424d-bc10-d33bdfa529eb
    :setup: Standalone Instance
    :steps:
        1. Test timeout for import task
        2. Test timeout for export task
        3. Test timeout for schema validate task
        4. Test timeout for schema reload task
        5. Test timeout for automember rebuild
        6. Test timeout for automember abort
        7. Test timeout for usn cleanup task
        8. Test timeout for posix group fixup task
        9. Test timeout for member UID fixup task
        10. Test timeout for memberof fixup task
        11. Test timeout for entryuuid fixup task
        12. Test timeout for linked attrs fixup task
        13. test timeout for db compact task
    :expectedresults:
        1. Task timed out
        2. Task timed out
        3. Task timed out
        4. Task timed out
        5. Task timed out
        6. Task timed out
        7. Task timed out
        8. Task timed out
        9. Task timed out
        10. Task timed out
        11. Task timed out
        12. Task timed out
        13. Task timed out
    """

    #inst = topo.ms['supplier1']  --> this leads to a deadlock when testing MemberOfFixupTask
    inst = topo.standalone

    # Enable plugins
    plugins = [USNPlugin, POSIXWinsyncPlugin, LinkedAttributesPlugin, AutoMembershipPlugin, MemberOfPlugin]
    for plugin in plugins:
        plugin(inst).enable()
    inst.restart()

    # Test timeout for import task, first create LDIF
    import_ldif = inst.ldifdir + '/import_task_timeout.ldif'
    dbgen_users(inst, 100000, import_ldif, DEFAULT_SUFFIX, parent="ou=people," + DEFAULT_SUFFIX, generic=True)

    task = ImportTask(inst)
    task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)

    # Test timeout for export task
    export_ldif = inst.ldifdir + '/export_task_timeout.ldif'
    task = ExportTask(inst)
    task.export_suffix_to_ldif(export_ldif, DEFAULT_SUFFIX)
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)

    # Test timeout for schema validate task
    task = SyntaxValidateTask(inst).create(properties={
        'basedn': DEFAULT_SUFFIX,
        'filter': "objectClass=*"
    })
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)

    # Test timeout for schema reload task (runs too fast)
    """
    task = SchemaReloadTask(inst).create(properties={
        'schemadir': inst.schemadir,
    })
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)
    """

    # Test timeout for automember rebuild
    task = AutomemberRebuildMembershipTask(inst).create(properties={
        'basedn': DEFAULT_SUFFIX,
        'filter': "objectClass=*"
    })
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)

    # Test timeout for automember abort (runs too fast)
    """
    AutomemberRebuildMembershipTask(inst).create(properties={
        'basedn': DEFAULT_SUFFIX,
        'filter': "objectClass=*"
    })
    task = AutomemberAbortRebuildTask(inst).create()
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)
    """

    # Test timeout for usn cleanup task, first delete a bunch of users
    for idx in range(1, 1001):
        entry_idx = str(idx).zfill(6)
        dn = f"uid=user{entry_idx},ou=people,{DEFAULT_SUFFIX}"
        UserAccount(inst, dn=dn).delete()
    task = USNTombstoneCleanupTask(inst).create(properties={
        'suffix': DEFAULT_SUFFIX,
    })
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)

    # Test timeout for Posix Group fixup task (runs too fast)
    """
    groups = PosixGroups(inst, DEFAULT_SUFFIX)
    start_range = 10000
    for idx in range(1, 10):
        group_props = {
            'cn': 'test_posix_group_' + str(idx),
            'objectclass': ['posixGroup', 'groupofuniquenames'],
            'gidNumber': str(idx)
        }
        group = groups.create(properties=group_props)
        for user_idx in range(start_range, start_range + 1000):
            entry_idx = str(user_idx).zfill(6)
            dn = f"uid=user{entry_idx},ou=people,{DEFAULT_SUFFIX}"
            group.add('memberuid', dn)
            group.add('uniquemember', dn)
        start_range += 1000

    task = MemberUidFixupTask(inst).create(properties={
        'basedn': DEFAULT_SUFFIX,
        'filter': "objectClass=*"
    })
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)
    """

    # Test timeout for memberOf fixup task
    groups = Groups(inst, DEFAULT_SUFFIX)
    group_props = {'cn': 'test_group'}
    group = groups.create(properties=group_props)
    for idx in range(5000, 6000):
        entry_idx = str(idx).zfill(6)
        dn = f"uid=user{entry_idx},ou=people,{DEFAULT_SUFFIX}"
        group.add_member(dn)

    task = MemberOfFixupTask(inst).create(properties={
        'basedn': DEFAULT_SUFFIX,
        'filter': "objectClass=*"
    })
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)

    # Test timeout for entryuuid fixup task
    task = EntryUUIDFixupTask(inst).create(properties={
        'basedn': DEFAULT_SUFFIX,
        'filter': "objectClass=*"
    })
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)

    # test timeout for linked attrs fixup (runs too fast)
    """
    task = FixupLinkedAttributesTask(inst).create(properties={
        'basedn': DEFAULT_SUFFIX,
        'filter': "objectClass=*"
    })
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)
    """

    # Test time out for db compact task (runs too fast)
    """
    task = DBCompactTask(inst).create()
    task.wait(timeout=.5, sleep_interval=.5)
    assert task.get_exit_code() is None
    task.wait(timeout=0)
    """


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
