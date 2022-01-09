# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
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
# from lib389.migrate.plan import *

pytestmark = pytest.mark.tier1

DATADIR1 = os.path.join(os.path.dirname(__file__), '../../data/openldap_2_389/1/')

@pytest.mark.skipif(ds_is_older('1.4.3'), reason="Not implemented")
def test_parse_openldap_slapdd():
    """Test parsing an example openldap configuration. We should be able to
    at least determine the backends, what overlays they have, and some other
    minimal amount.

    :id: b0061ab0-fff4-45c6-b6c6-171ca3d2dfbc
    :setup: Data directory with an openldap config directory.
    :steps:
        1. Parse the openldap configuration

    :expectedresults:
        1. Success
    """
    config_path = os.path.join(DATADIR1, 'slapd.d')
    config = olConfig(config_path)

    # Do we have databases?
    assert len(config.databases) == 2
    # Check that we unpacked uid eq,pres,sub correctly.
    assert len(config.databases[0].index) == 4
    assert ('objectClass', 'eq') in config.databases[0].index
    assert ('uid', 'eq') in config.databases[0].index
    assert ('uid', 'pres') in config.databases[0].index
    assert ('uid', 'sub') in config.databases[0].index

    # Did our schema parse?
    assert any(['suseModuleConfiguration' in x.names for x in config.schema.classes])





@pytest.mark.skipif(ds_is_older('1.4.3'), reason="Not implemented")
def test_migrate_openldap_slapdd(topology_st):
    """

    :id: e9748040-90a0-4d69-bdde-007104f75cc5
    :setup: Data directory with an openldap config directory.
    :steps:
        1. Parse the configuration
        2. Execute a full migration plan

    :expectedresults:
        1. Success
        2. Success
    """

    inst = topology_st.standalone
    config_path = os.path.join(DATADIR1, 'slapd.d')
    config = olConfig(config_path)
    ldifs = {
        "dc=example,dc=com": os.path.join(DATADIR1, 'example_com.slapcat.ldif'),
        "dc=example,dc=net": os.path.join(DATADIR1, 'example_net.slapcat.ldif'),
    }

    migration = Migration(inst, config.schema, config.databases, ldifs)

    print("==== migration plan ====")
    print(migration.__unicode__())
    print("==== end migration plan ====")

    migration.execute_plan()

    # Check the BE's are there
    # Check plugins
    # Check the schema
    # Check a user can bind


@pytest.mark.skipif(ds_is_older('1.4.3'), reason="Not implemented")
def test_migrate_openldap_slapdd_skip_elements(topology_st):
    """

    :id: d5e16aeb-6810-423b-b5e0-f89e0596292e
    :setup: Data directory with an openldap config directory.
    :steps:
        1. Parse the configuration
        2. Execute a migration with skipped elements

    :expectedresults:
        1. Success
        2. Success
    """

    inst = topology_st.standalone
    config_path = os.path.join(DATADIR1, 'slapd.d')
    config = olConfig(config_path)
    ldifs = {
        "dc=example,dc=com": os.path.join(DATADIR1, 'example_com.slapcat.ldif'),
    }

    # 1.3.6.1.4.1.5322.13.1.1 is namedObject, so check that isn't there

    migration = Migration(inst, config.schema, config.databases, ldifs,
        skip_schema_oids=['1.3.6.1.4.1.5322.13.1.1'],
        skip_overlays=[olOverlayType.UNIQUE],
    )

    print("==== migration plan ====")
    print(migration.__unicode__())
    print("==== end migration plan ====")

    migration.execute_plan()

    # Check that the overlay ISNT there
    # Check the schema that SHOULDNT be there.





#  # how to convert the config
#  
#  # How to slapcat
#  
#  openldap_2_389 --config /etc/openldap/slapd.d --ldif "path"
#  
#  
#  --confirm
#  --ignore-overlay=X
#  --ignore-schema-oid=X
#  --no-overlays
#  --no-passwords
#  --no-schema
#  --no-indexes
#  
#  
#  
#  
#  Add skip overlay
#  Add password Strip
#  check userPasswords


