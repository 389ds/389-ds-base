# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import ldap
import time
import pytest
from test389.topologies import topology_st as topology
from lib389.paths import Paths
from lib389.utils import ds_is_older
from lib389.plugins import RetroChangelogPlugin, ContentSyncPlugin
from lib389._constants import ErrorLog, DEFAULT_SUFFIX
from lib389.plugins import EntryUUIDPlugin
from lib389.idm.user import nsUserAccounts

from . import ISyncRepl, syncstate_assert, OU_PEOPLE

default_paths = Paths()
pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

@pytest.mark.skipif(ldap.__version__ < '3.3.1' or not default_paths.rust_enabled or ds_is_older('1.4.4.0'),
    reason="Sync repl does not support openldap compat in older versions, and without entryuuid")
def test_syncrepl_openldap(topology):
    """ Test basic functionality of the openldap syncrepl
    compatability handler.

    :id: 03039178-2cc6-40bd-b32c-7d6de108828b

    :setup: Standalone instance

    :steps:
        1. Enable Retro Changelog
        2. Enable Syncrepl
        3. Run the syncstate test to check refresh, add, delete, mod.

    :expectedresults:
        1. Success
        1. Success
        1. Success
    """
    st = topology.standalone
    # Ensure entryuuid is setup
    plug = EntryUUIDPlugin(st)
    task = plug.fixup(DEFAULT_SUFFIX)
    task.wait()
    st.config.loglevel(vals=(ErrorLog.DEFAULT,ErrorLog.PLUGIN))
    assert(task.is_complete() and task.get_exit_code() == 0)

    # Enable RetroChangelog.
    rcl = RetroChangelogPlugin(st)
    rcl.enable()
    # Set the default targetid
    rcl.add('nsslapd-attribute', 'nsuniqueid:targetUniqueId')
    rcl.add('nsslapd-attribute', 'entryuuid:targetEntryUUID')
    # Enable sync repl
    csp = ContentSyncPlugin(st)
    csp.add('syncrepl-allow-openldap', 'on')
    csp.enable()
    # Restart DS
    st.restart()
    # Setup the syncer
    sync = ISyncRepl(st, openldap=True)
    # Run the checks
    syncstate_assert(st, sync)


@pytest.mark.skipif(ldap.__version__ < '3.3.1' or not default_paths.rust_enabled or ds_is_older('1.4.4.0'),
    reason="Sync repl does not support openldap compat in older versions, and without entryuuid")
def test_syncrepl_openldap_add_then_delete_same_window(topology):
    """Replay an ADD cancelled by a DELETE in one OpenLDAP-compat refresh window

    In OpenLDAP compatibility mode the ADD branch stores a heap-duplicated
    entryUUID on the update node. A later DELETE of the same UUID in the
    same changelog window must free that string when the ADD is cancelled
    (sync_read_entry_from_changelog).

    :id: 8bdd61bb-dded-4114-a832-c8c732ab1040
    :setup: Standalone instance
    :steps:
        1. Enable Retro Changelog, EntryUUID mapping, and OpenLDAP-compat sync
        2. Complete an initial refresh under ou=people
        3. Add an entry then delete it without an intervening sync
        4. Refresh with the cookie from step 2
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. The combined window is a no-op: no present and no delete
    """
    st = topology.standalone
    plug = EntryUUIDPlugin(st)
    task = plug.fixup(DEFAULT_SUFFIX)
    task.wait()
    st.config.loglevel(vals=(ErrorLog.DEFAULT, ErrorLog.PLUGIN))
    assert(task.is_complete() and task.get_exit_code() == 0)

    rcl = RetroChangelogPlugin(st)
    rcl.enable()
    try:
        rcl.add('nsslapd-attribute', 'nsuniqueid:targetUniqueId')
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass
    try:
        rcl.add('nsslapd-attribute', 'entryuuid:targetEntryUUID')
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass
    csp = ContentSyncPlugin(st)
    try:
        csp.add('syncrepl-allow-openldap', 'on')
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass
    csp.enable()
    st.restart()

    sync = ISyncRepl(st, openldap=True)
    sync.syncrepl_search(base=OU_PEOPLE)
    sync.syncrepl_complete()

    account = nsUserAccounts(st, DEFAULT_SUFFIX).create_test_user(uid=303546)
    account.delete()

    sync.syncrepl_search(base=OU_PEOPLE)
    sync.syncrepl_complete()
    sync.check_cookie()
    assert 0 == len(sync.entries.keys())
    assert 0 == len(sync.present)
    assert 0 == len(sync.delete)

