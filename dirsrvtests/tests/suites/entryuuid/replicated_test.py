# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import pytest
import logging
from lib389.topologies import topology_m2 as topo_m2
from lib389.idm.user import nsUserAccounts
from lib389.paths import Paths
from lib389.utils import ds_is_older
from lib389._constants import *
from lib389.replica import ReplicationManager
from lib389.plugins import EntryUUIDPlugin
from lib389.tasks import EntryUUIDFixupTask

default_paths = Paths()

pytestmark = pytest.mark.tier1

@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.2.0'), reason="Entryuuid is not available in older versions")

def test_entryuuid_with_replication(topo_m2):
    """ Check that entryuuid works with replication

    :id: a5f15bf9-7f63-473a-840c-b9037b787024

    :setup: two node mmr

    :steps:
        1. Create an entry on one server
        2. Wait for replication
        3. Assert it is on the second

    :expectedresults:
        1. Success
        1. Success
        1. Success
    """

    server_a = topo_m2.ms["supplier1"]
    server_b = topo_m2.ms["supplier2"]
    server_a.config.loglevel(vals=(ErrorLog.DEFAULT,ErrorLog.TRACE))
    server_b.config.loglevel(vals=(ErrorLog.DEFAULT,ErrorLog.TRACE))

    repl = ReplicationManager(DEFAULT_SUFFIX)

    account_a = nsUserAccounts(server_a, DEFAULT_SUFFIX).create_test_user(uid=2000)
    euuid_a = account_a.get_attr_vals_utf8('entryUUID')
    print("🧩 %s" % euuid_a)
    assert(euuid_a is not None)
    assert(len(euuid_a) == 1)

    repl.wait_for_replication(server_a, server_b)

    account_b = nsUserAccounts(server_b, DEFAULT_SUFFIX).get("test_user_2000")
    euuid_b = account_b.get_attr_vals_utf8('entryUUID')
    print("🧩 %s" % euuid_b)

    server_a.config.loglevel(vals=(ErrorLog.DEFAULT,))
    server_b.config.loglevel(vals=(ErrorLog.DEFAULT,))

    assert(euuid_b is not None)
    assert(len(euuid_b) == 1)
    assert(euuid_b == euuid_a)

    account_b.set("description", "update")
    repl.wait_for_replication(server_b, server_a)

    euuid_c = account_a.get_attr_vals_utf8('entryUUID')
    print("🧩 %s" % euuid_c)
    assert(euuid_c is not None)
    assert(len(euuid_c) == 1)
    assert(euuid_c == euuid_a)

@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.2.0'), reason="Entryuuid is not available in older versions")
def test_entryuuid_fixup_with_replication(topo_m2):
    """ Check that entryuuid fixup task works with replication

    :id: 4ff25022-2de8-11ed-b393-482ae39447e5
    :setup: two node mmr

    :steps:
        1. Disable EntryUUID plugin.
        2. Create an user entry.
        3. Enable EntryUUID plugin.
        4. Check that the user entry does not have an entryuuid attribute
        5. Run fixup task
        6. Wait for task completion
        7. Check that the user entry has an entryuuid attribute
        8. Wait until changes get replicated
        9. Check that the user entry on the other supplier has same entryuuid attribute

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
    """
    server_a = topo_m2.ms["supplier1"]
    server_b = topo_m2.ms["supplier2"]
    server_a.config.loglevel(vals=(ErrorLog.DEFAULT,ErrorLog.TRACE))
    server_b.config.loglevel(vals=(ErrorLog.DEFAULT,ErrorLog.TRACE))

    # 1. Disable EntryUUID plugin.
    plugin = EntryUUIDPlugin(server_a)
    plugin.disable()
    server_a.restart()

    # 2. Create an user entry.
    # uid must differ than the test test_entryuuid_with_replication one
    # to avoid conflict between the tests.
    account_a = nsUserAccounts(server_a, DEFAULT_SUFFIX).create_test_user(uid=3000)

    # 3. Enable EntryUUID plugin.
    plugin.enable()
    server_a.restart()

    # 4. Check that the user entry does not have an entryuuid attribute
    euuid_a = account_a.get_attr_vals_utf8('entryUUID')
    assert(not euuid_a)

    # 5. Run fixup task
    task = EntryUUIDFixupTask(server_a).create(properties={
        'basedn': DEFAULT_SUFFIX,
        'filter': "objectClass=*"
    })

    # 6. Wait for task completion
    task.wait()
    assert task.is_complete()

    # 7. Check that the user entry has an entryuuid attribute
    euuid_a = account_a.get_attr_vals_utf8('entryUUID')
    assert(euuid_a)

    # 8. Wait until changes get replicated
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(server_b, server_a)

    # 9. Check that the user entry on the other supplier has same entryuuid attribute
    account_b = nsUserAccounts(server_b, DEFAULT_SUFFIX).get("test_user_3000")
    euuid_b = account_b.get_attr_vals_utf8('entryUUID')
    assert euuid_a == euuid_b
