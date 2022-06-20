# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import os
from lib389.topologies import topology_st
from lib389.password_plugins import PBKDF2Plugin
from lib389.utils import ds_is_older
from lib389.migrate.openldap.config import olConfig
from lib389.migrate.openldap.config import olOverlayType
from lib389.migrate.plan import Migration
from lib389.plugins import MemberOfPlugin

pytestmark = pytest.mark.tier1

DATADIR1 = os.path.join(os.path.dirname(__file__), '../../data/openldap_2_389/5323/')

@pytest.mark.skipif(ds_is_older('1.4.3'), reason="Not implemented")
def test_migrate_openldap_monitor(topology_st):
    """Attempt a migration with a monitor database configured.

    :id: 3bf7a7e8-7482-49ee-bc3c-e5a174463844
    :setup: Data directory with an openldap config with monitor db
    :steps:
        1. Parse the configuration
        2. Execute a full migration plan
        3. Assert monitor was skipped

    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    inst = topology_st.standalone
    config_path = os.path.join(DATADIR1, 'slapd.d')
    config = olConfig(config_path)

    assert len(config.databases) == 1

    ldifs = {}

    migration = Migration(inst, config.schema, config.databases, ldifs)

    print("==== migration plan ====")
    print(migration.__unicode__())
    print("==== end migration plan ====")

    migration.execute_plan()
    # End test, should suceed with no exceptions.


