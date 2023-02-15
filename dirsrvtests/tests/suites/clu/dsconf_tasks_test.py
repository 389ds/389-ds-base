# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
from lib389._constants import DEFAULT_SUFFIX
# from lib389.topologies import topology_m1 as topo
from lib389.topologies import topology_st as topo
from lib389.tasks import (ImportTask, ExportTask, BackupTask, RestoreTask, AutomemberRebuildMembershipTask,
                          AutomemberAbortRebuildTask, MemberUidFixupTask, MemberOfFixupTask, USNTombstoneCleanupTask,
                          DBCompactTask, EntryUUIDFixupTask, SchemaReloadTask, SyntaxValidateTask,
                          FixupLinkedAttributesTask, DBCompactTask)
from lib389.plugins import USNPlugin, POSIXWinsyncPlugin, LinkedAttributesPlugin, AutoMembershipPlugin, MemberOfPlugin
from lib389.dbgen import dbgen_users
from lib389.idm.user import UserAccount
from lib389.idm.group import Groups
from lib389.idm.posixgroup import PosixGroups  # not sure if this is need yet MARK

log = logging.getLogger(__name__)


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
