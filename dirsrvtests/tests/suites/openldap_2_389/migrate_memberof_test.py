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

DATADIR1 = os.path.join(os.path.dirname(__file__), '../../data/openldap_2_389/memberof/')

@pytest.mark.skipif(ds_is_older('1.4.3'), reason="Not implemented")
def test_migrate_openldap_memberof(topology_st):
    """Attempt a migration with memberof configured, and ensure it migrates

    :id: f59f4c6a-7c85-40d1-91ee-dccbc0bd7ef8
    :setup: Data directory with an openldap config with memberof
    :steps:
        1. Parse the configuration
        2. Execute a full migration plan
        3. Assert memberof was configured

    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    inst = topology_st.standalone
    config_path = os.path.join(DATADIR1, 'slapd.d')
    config = olConfig(config_path)

    for overlay in config.databases[0].overlays:
        print("==================================================")
        print("%s" % overlay.otype)
        print("==================================================")
        assert overlay.otype != olOverlayType.UNKNOWN

    ldifs = {}

    migration = Migration(inst, config.schema, config.databases, ldifs)

    print("==== migration plan ====")
    print(migration.__unicode__())
    print("==== end migration plan ====")

    migration.execute_plan()
    # End test, should suceed with no exceptions.

    memberof = MemberOfPlugin(inst)
    assert memberof.status()


