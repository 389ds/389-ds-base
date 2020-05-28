# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import ldap
import time
from ldap.syncrepl import SyncreplConsumer
import pytest
from lib389 import DirSrv
from lib389.idm.user import nsUserAccounts, UserAccounts
from lib389.topologies import topology_st as topology
from lib389.paths import Paths
from lib389.utils import ds_is_older
from lib389.plugins import RetroChangelogPlugin, ContentSyncPlugin
from lib389._constants import *

from . import ISyncRepl, syncstate_assert

default_paths = Paths()
pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

@pytest.mark.skipif(ds_is_older('1.4.4.0'), reason="Sync repl does not support openldap compat in older versions")
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
    # Enable RetroChangelog.
    rcl = RetroChangelogPlugin(st)
    rcl.enable()
    # Set the default targetid
    rcl.replace('nsslapd-attribute', 'nsuniqueid:targetUniqueId')
    # Enable sync repl
    csp = ContentSyncPlugin(st)
    csp.enable()
    # Restart DS
    st.restart()
    # log.error("+++++++++++")
    # time.sleep(60)
    # Setup the syncer
    sync = ISyncRepl(st, openldap=True)
    # Run the checks
    syncstate_assert(st, sync)

