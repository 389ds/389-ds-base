# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import pytest
import time
import shutil
from lib389.idm.user import nsUserAccounts, UserAccounts
from lib389.idm.account import Accounts
from lib389.idm.domain import Domain
from lib389.topologies import topology_st as topology
from lib389.backend import Backends
from lib389.paths import Paths
from lib389.utils import ds_is_older
from lib389._constants import *
from lib389.plugins import EntryUUIDPlugin

default_paths = Paths()

pytestmark = pytest.mark.tier1

DATADIR1 = os.path.join(os.path.dirname(__file__), '../../data/entryuuid/')
IMPORT_UUID_A = "973e1bbf-ba9c-45d4-b01b-ff7371fd9008"
UUID_BETWEEN = "eeeeeeee-0000-0000-0000-000000000000"
IMPORT_UUID_B = "f6df8fe9-6b30-46aa-aa13-f0bf755371e8"
UUID_MIN = "00000000-0000-0000-0000-000000000000"
UUID_MAX = "ffffffff-ffff-ffff-ffff-ffffffffffff"

def _entryuuid_import_and_search(topology):
    # 1
    ldif_dir = topology.standalone.get_ldif_dir()
    target_ldif = os.path.join(ldif_dir, 'localhost-userRoot-2020_03_30_13_14_47.ldif')
    import_ldif = os.path.join(DATADIR1, 'localhost-userRoot-2020_03_30_13_14_47.ldif')
    shutil.copyfile(import_ldif, target_ldif)
    os.chmod(target_ldif, 0o777)

    be = Backends(topology.standalone).get('userRoot')
    task = be.import_ldif([target_ldif])
    task.wait()
    assert(task.is_complete() and task.get_exit_code() == 0)

    accounts = Accounts(topology.standalone, DEFAULT_SUFFIX)
    # 2 - positive eq test
    r2 = accounts.filter("(entryUUID=%s)" % IMPORT_UUID_A)
    assert(len(r2) == 1)
    r3 = accounts.filter("(entryuuid=%s)" % IMPORT_UUID_B)
    assert(len(r3) == 1)
    # 3 - negative eq test
    r4 = accounts.filter("(entryuuid=%s)" % UUID_MAX)
    assert(len(r4) == 0)
    # 4 - le search
    r5 = accounts.filter("(entryuuid<=%s)" % UUID_BETWEEN)
    assert(len(r5) == 1)
    # 5 - ge search
    r6 = accounts.filter("(entryuuid>=%s)" % UUID_BETWEEN)
    assert(len(r6) == 1)
    # 6 - le 0 search
    r7 = accounts.filter("(entryuuid<=%s)" % UUID_MIN)
    assert(len(r7) == 0)
    # 7 - ge f search
    r8 = accounts.filter("(entryuuid>=%s)" % UUID_MAX)
    assert(len(r8) == 0)
    # 8 - export db
    task = be.export_ldif()
    task.wait()
    assert(task.is_complete() and task.get_exit_code() == 0)


@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.2.0'), reason="Entryuuid is not available in older versions")
def test_entryuuid_indexed_import_and_search(topology):
    """ Test that an ldif of entries containing entryUUID's can be indexed and searched
    correctly. As https://tools.ietf.org/html/rfc4530 states, the MR's are equality and
    ordering, so we check these are correct.

    :id: c98ee6dc-a7ee-4bd4-974d-597ea966dad9

    :setup: Standalone instance

    :steps:
        1. Import the db from the ldif
        2. EQ search for an entryuuid (match)
        3. EQ search for an entryuuid that does not exist
        4. LE search for an entryuuid lower (1 res)
        5. GE search for an entryuuid greater (1 res)
        6. LE for the 0 uuid (0 res)
        7. GE for the f uuid (0 res)
        8. export the db to ldif

    :expectedresults:
        1. Success
        2. 1 match
        3. 0 match
        4. 1 match
        5. 1 match
        6. 0 match
        7. 0 match
        8. success
    """
    # Assert that the index correctly exists.
    be = Backends(topology.standalone).get('userRoot')
    indexes = be.get_indexes()
    indexes.ensure_state(properties={
        'cn': 'entryUUID',
        'nsSystemIndex': 'false',
        'nsIndexType': ['eq', 'pres'],
    })
    _entryuuid_import_and_search(topology)

@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.2.0'), reason="Entryuuid is not available in older versions")
def test_entryuuid_unindexed_import_and_search(topology):
    """ Test that an ldif of entries containing entryUUID's can be UNindexed searched
    correctly. As https://tools.ietf.org/html/rfc4530 states, the MR's are equality and
    ordering, so we check these are correct.

    :id: b652b54d-f009-464b-b5bd-299a33f97243

    :setup: Standalone instance

    :steps:
        1. Import the db from the ldif
        2. EQ search for an entryuuid (match)
        3. EQ search for an entryuuid that does not exist
        4. LE search for an entryuuid lower (1 res)
        5. GE search for an entryuuid greater (1 res)
        6. LE for the 0 uuid (0 res)
        7. GE for the f uuid (0 res)
        8. export the db to ldif

    :expectedresults:
        1. Success
        2. 1 match
        3. 0 match
        4. 1 match
        5. 1 match
        6. 0 match
        7. 0 match
        8. success
    """
    # Assert that the index does NOT exist for this test.
    be = Backends(topology.standalone).get('userRoot')
    indexes = be.get_indexes()
    try:
        idx = indexes.get('entryUUID')
        idx.delete()
    except ldap.NO_SUCH_OBJECT:
        # It's already not present, move along, nothing to see here.
        pass
    _entryuuid_import_and_search(topology)

# Test entryUUID generation
@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.2.0'), reason="Entryuuid is not available in older versions")
def test_entryuuid_generation_on_add(topology):
    """ Test that when an entry is added, the entryuuid is added.

    :id: a7439b0a-dcee-4cd6-b8ef-771476c0b4f6

    :setup: Standalone instance

    :steps:
        1. Create a new entry in the db
        2. Check it has an entry uuid

    :expectedresults:
        1. Success
        2. An entry uuid is present
    """
    # Step one - create a user!
    account = nsUserAccounts(topology.standalone, DEFAULT_SUFFIX).create_test_user()
    # Step two - does it have an entryuuid?
    euuid = account.get_attr_val_utf8('entryUUID')
    print(euuid)
    assert(euuid is not None)

# Test fixup task
@pytest.mark.skipif(not default_paths.rust_enabled or ds_is_older('1.4.2.0'), reason="Entryuuid is not available in older versions")
def test_entryuuid_fixup_task(topology):
    """Test that when an entries without UUID's can have one generated via
    the fixup process.

    :id: ad42bba2-ffb2-4c22-a37d-cbe7bcf73d6b

    :setup: Standalone instance

    :steps:
        1. Disable the entryuuid plugin
        2. Create an entry
        3. Enable the entryuuid plugin
        4. Run the fixup
        5. Assert the entryuuid now exists
        6. Restart and check they persist

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Suddenly EntryUUID!
        6. Still has EntryUUID!
    """
    # 1. Disable the plugin
    plug = EntryUUIDPlugin(topology.standalone)
    plug.disable()
    topology.standalone.restart()

    # 2. create the account
    account = nsUserAccounts(topology.standalone, DEFAULT_SUFFIX).create_test_user(uid=2000)
    euuid = account.get_attr_val_utf8('entryUUID')
    assert(euuid is None)

    # 3. enable the plugin
    plug.enable()
    topology.standalone.restart()

    # 4. run the fix up
    # For now set the log level to high!
    topology.standalone.config.loglevel(vals=(ErrorLog.DEFAULT,ErrorLog.TRACE))
    task = plug.fixup(DEFAULT_SUFFIX)
    task.wait()
    assert(task.is_complete() and task.get_exit_code() == 0)
    topology.standalone.config.loglevel(vals=(ErrorLog.DEFAULT,))

    # 5.1 Assert the uuid on the user.
    euuid_user = account.get_attr_val_utf8('entryUUID')
    assert(euuid_user is not None)

    # 5.2 Assert it on the domain entry.
    domain = Domain(topology.standalone, dn=DEFAULT_SUFFIX)
    euuid_domain = domain.get_attr_val_utf8('entryUUID')
    assert(euuid_domain is not None)

    # Assert it persists after a restart.
    topology.standalone.restart()
    # 6.1 Assert the uuid on the use.
    euuid_user_2 = account.get_attr_val_utf8('entryUUID')
    assert(euuid_user_2 == euuid_user)

    # 6.2 Assert it on the domain entry.
    euuid_domain_2 = domain.get_attr_val_utf8('entryUUID')
    assert(euuid_domain_2 == euuid_domain)

