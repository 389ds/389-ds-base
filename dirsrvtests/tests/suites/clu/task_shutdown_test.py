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

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.dbgen import dbgen_users
from lib389.idm.account import Account
from lib389.plugins import (
    AutoMembershipDefinitions,
    MemberOfPlugin,
    ReferentialIntegrityPlugin,
    AutoMembershipPlugin,
    USNPlugin,
)
from lib389.properties import TASK_WAIT
from lib389.tasks import Tasks
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier0

log = logging.getLogger(__name__)

# Number of entries for import
NUM_IMPORT_ENTRIES = 10000


def _allow_core_dumps(inst):
    """Ensure init config allows core dumps (ulimit / LimitCORE)."""
    initconfig_dir = inst.get_initconfig_dir()
    sysconfig_dirsrv = os.path.join(initconfig_dir, 'dirsrv')
    sysconfig_systemd = sysconfig_dirsrv + '.systemd'

    for path, pattern, line in [
        (sysconfig_dirsrv, 'ulimit -c unlimited', 'ulimit -c unlimited\n'),
        (sysconfig_systemd, 'LimitCORE=infinity', 'LimitCORE=infinity\n'),
        ]:
        if not os.path.isfile(path):
            continue
        with open(path, 'r') as f:
            content = f.read()
        if pattern not in content:
            log.info(f'Adding {line.strip()} to {path}')
            with open(path, 'a') as f:
                f.write(line)

    inst.restart(timeout=10)


def _assert_no_core(inst, label=''):
    """Assert no core file in the instance log directory; move to /tmp if found."""
    logdir = os.path.dirname(inst.errlog)
    core_glob = os.path.join(logdir, 'core*')
    cores = glob.glob(core_glob)
    if cores:
        mytmp = '/tmp'
        for c in cores:
            try:
                os.rename(c, os.path.join(mytmp, os.path.basename(c) + '.' + (label or 'task_shutdown')))
            except OSError:
                pass
        log.error(f'Core file(s) found in {logdir}; moved to {mytmp}. Test failed.')
        pytest.fail(f'Core file generated (label: {label or "task_shutdown"})')
    log.info(f'No core files found {(label or "ok")}')


@pytest.fixture(scope="function")
def task_shutdown_setup(topo):
    """Allow core dumps, generate LDIF with dbgen, import, and return the instance."""
    inst = topo.standalone
    _allow_core_dumps(inst)

    ldif_file = os.path.join(inst.get_ldif_dir(), 'task_shutdown.ldif')
    if os.path.exists(ldif_file):
        os.remove(ldif_file)

    dbgen_users(inst, NUM_IMPORT_ENTRIES, ldif_file, DEFAULT_SUFFIX, generic=True)
    assert os.path.isfile(ldif_file), 'dbgen should create LDIF'

    tasks = Tasks(inst)
    tasks.importLDIF(DEFAULT_SUFFIX, None, ldif_file, {TASK_WAIT: True})
    log.info('Import complete: %s', ldif_file)

    return inst


def test_task_shutdown_memberof(task_shutdown_setup):
    """Stop server during fixupMemberOf (no wait); assert no core.

    :id: f47ac10b-58cc-4372-a567-0e02b2c3d4e5
    :setup: Standalone instance
    :steps:
        1. Enable MemberOf and Referential Integrity plugins, restart
        2. Start fixupMemberOf task with TASK_WAIT=False
        3. Stop server, assert no core file, start server
        4. Disable plugins, restart
    :expectedresults:
        1. Success
        2. Success
        3. No core file; server starts
        4. Success
    """
    inst = task_shutdown_setup
    MemberOfPlugin(inst).enable()
    ReferentialIntegrityPlugin(inst).enable()
    inst.restart(timeout=10)

    Tasks(inst).fixupMemberOf(suffix=DEFAULT_SUFFIX, args={TASK_WAIT: False})
    inst.stop(timeout=10)
    _assert_no_core(inst, 'memberof')
    inst.start(timeout=10)

    ReferentialIntegrityPlugin(inst).disable()
    MemberOfPlugin(inst).disable()
    inst.restart(timeout=10)


def test_task_shutdown_automember(task_shutdown_setup):
    """Stop server during automember rebuild/export/map (no wait); assert no core.

    :id: a0b1c2d3-e4f5-46a7-b8c9-d0e1f2a3b4c5
    :setup: Standalone instance
    :steps:
        1. Enable AutoMembership and Referential Integrity, add automember definition
        2. automemberRebuild(TASK_WAIT=False), stop, assert no core, start
        3. automemberExport(TASK_WAIT=False), stop, assert no core, start
        4. automemberMap(TASK_WAIT=False), stop, assert no core, start
        5. Disable referint and automember, restart
    :expectedresults:
        1. Success
        2. No core file; server starts
        3. No core file; server starts
        4. No core file; server starts
        5. Success
    """
    inst = task_shutdown_setup
    AutoMembershipPlugin(inst).enable()
    ReferentialIntegrityPlugin(inst).enable()

    definitions = AutoMembershipDefinitions(inst)
    definitions.ensure_state(
        rdn='cn=group cfg',
        properties={
            'cn': 'group cfg',
            'autoMemberScope': DEFAULT_SUFFIX,
            'autoMemberFilter': 'objectclass=inetorgperson',
            'autoMemberDefaultGroup': 'cn=group0,' + DEFAULT_SUFFIX,
            'autoMemberGroupingAttr': 'uniquemember:dn',
        },
    )
    inst.restart(timeout=10)

    tasks = Tasks(inst)

    tasks.automemberRebuild(suffix=DEFAULT_SUFFIX, args={TASK_WAIT: False})
    inst.stop(timeout=10)
    _assert_no_core(inst, 'automember_rebuild')
    inst.start(timeout=10)

    export_ldif = '/tmp/task_shutdown_automember_exported.ldif'
    tasks.automemberExport(suffix=DEFAULT_SUFFIX, ldif_out=export_ldif, args={TASK_WAIT: False})
    inst.stop(timeout=10)
    _assert_no_core(inst, 'automember_export')
    inst.start(timeout=10)

    ldif_in = os.path.join(inst.get_ldif_dir(), 'task_shutdown.ldif')
    map_ldif = '/tmp/task_shutdown_automember_map.ldif'
    tasks.automemberMap(ldif_in=ldif_in, ldif_out=map_ldif, args={TASK_WAIT: False})
    inst.stop(timeout=10)
    _assert_no_core(inst, 'automember_map')
    inst.start(timeout=10)

    ReferentialIntegrityPlugin(inst).disable()
    AutoMembershipPlugin(inst).disable()
    inst.restart(timeout=10)


def test_task_shutdown_syntaxvalidate(task_shutdown_setup):
    """Stop server during syntaxValidate (no wait); assert no core.

    :id: b1c2d3e4-f5a6-47b8-c9d0-e1f2a3b4c5d6
    :setup: Standalone instance
    :steps:
        1. Start syntaxValidate task with TASK_WAIT=False
        2. Stop server, assert no core file, start server
    :expectedresults:
        1. Success
        2. No core file; server starts
    """
    inst = task_shutdown_setup
    Tasks(inst).syntaxValidate(suffix=DEFAULT_SUFFIX, args={TASK_WAIT: False})
    inst.stop(timeout=10)
    _assert_no_core(inst, 'syntaxvalidate')
    inst.start(timeout=10)


def test_task_shutdown_usn(task_shutdown_setup):
    """Stop server during USN tombstone cleanup (no wait); assert no core.

    :id: c2d3e4f5-a6b7-48c9-d0e1-f2a3b4c5d6e7
    :setup: Standalone instance
    :steps:
        1. Enable USN plugin, restart
        2. Delete all inetOrgPerson entries
        3. Start usnTombstoneCleanup with TASK_WAIT=False
        4. Stop server, assert no core file, start server
        5. Disable USN, restart
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. No core file; server starts
        5. Success
    """
    inst = task_shutdown_setup
    USNPlugin(inst).enable()
    inst.restart(timeout=10)

    entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectclass=inetorgperson)')
    for ent in entries:
        Account(inst, ent.dn).delete()

    Tasks(inst).usnTombstoneCleanup(suffix=DEFAULT_SUFFIX, bename='userRoot', args={TASK_WAIT: False})
    inst.stop(timeout=10)
    _assert_no_core(inst, 'usn')
    inst.start(timeout=10)

    USNPlugin(inst).disable()
    inst.restart(timeout=10)


def test_task_shutdown_schemareload(topo):
    """Stop server during schemaReload (no wait); assert no core.

    :id: d3e4f5a6-b7c8-49d0-e1f2-a3b4c5d6e7f8
    :setup: Standalone instance
    :steps:
        1. Start schemaReload task with TASK_WAIT=False
        2. Stop server, assert no core file, start server
    :expectedresults:
        1. Success
        2. No core file; server starts
    """
    inst = topo.standalone
    _allow_core_dumps(inst)

    Tasks(inst).schemaReload(args={TASK_WAIT: False})
    inst.stop(timeout=10)
    _assert_no_core(inst, 'schema_reload')
    inst.start(timeout=10)


if __name__ == '__main__':
    import sys
    pytest.main([__file__, '-s'] + sys.argv[1:])
