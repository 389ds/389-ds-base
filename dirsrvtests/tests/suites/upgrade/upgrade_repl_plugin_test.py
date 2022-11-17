# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import logging
import pytest
import os
from lib389.topologies import topology_st as topo
from lib389.dseldif import DSEldif
from lib389.plugins import Plugins

log = logging.getLogger(__name__)


def test_repl_plugin_name_change(topo):
    """Test that the replication plugin name is updated to the new name at
    server startup.

    :id: c2a7b7fb-6524-4391-8883-683b6af2a1cf
    :setup: Standalone Instance
    :steps:
        1. Stop Server
        2. Edit repl plugin in dse.ldif
        3. Add repl plugin dependency to retro changelog plugin.
        4. Start server
        5. Verify old plugin is not found
        6. Verify new plugin is found
        7. Verify plugin dependency was updated in retro changelog plugin
        8. Restart and repeat steps 5-7
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """

    REPL_PLUGIN_DN = "cn=Multisupplier Replication Plugin,cn=plugins,cn=config"
    REPL_PLUGIN_NAME = "Multisupplier Replication Plugin"
    REPL_PLUGIN_INIT_ATTR = "nsslapd-pluginInitfunc"
    REPL_PLUGIN_INIT_FUNC = "replication_multisupplier_plugin_init"
    REPL_DEPENDS_ATTR = "nsslapd-plugin-depends-on-named"
    OLD_REPL_PLUGIN_NAME = "Replication Plugin"
    OLD_REPL_PLUGIN_DN = "cn=Replication Plugin,cn=plugins,cn=config"
    RETROCL_PLUGIN_DN = "cn=Retro Changelog Plugin,cn=plugins,cn=config"
    RETROCL_PLUGIN_NAME = "Retro Changelog Plugin"

    MEP_PLUGIN_DN = "cn=Managed Entries,cn=plugins,cn=config"
    MEP_PLUGIN_NAME = "Managed Entries"

    # Stop the server
    topo.standalone.stop()

    # Edit repl plugin in dse.ldif
    dse_ldif = DSEldif(topo.standalone)
    dse_ldif.replace(REPL_PLUGIN_DN, REPL_PLUGIN_INIT_ATTR, "multi_old_init_function")
    dse_ldif.rename(REPL_PLUGIN_DN, OLD_REPL_PLUGIN_DN)

    # Add dependency for repl plugin in retro changelog plugin and managed entries
    dse_ldif.replace(RETROCL_PLUGIN_DN, REPL_DEPENDS_ATTR, OLD_REPL_PLUGIN_NAME)
    dse_ldif.replace(MEP_PLUGIN_DN, REPL_DEPENDS_ATTR, OLD_REPL_PLUGIN_NAME)
    # assert 0

    # Restart the server, loop twice to verify settings stick after restart
    for loop in [0, 1]:
        topo.standalone.restart()

        # Verify old plugin is deleted
        with pytest.raises(ldap.NO_SUCH_OBJECT):
            Plugins(topo.standalone).get(OLD_REPL_PLUGIN_NAME)

        # Verify repl plugin name was changed to the new name
        plugin = Plugins(topo.standalone).get(REPL_PLUGIN_NAME)
        assert plugin is not None
        assert plugin.get_attr_val_utf8_l(REPL_PLUGIN_INIT_ATTR) == REPL_PLUGIN_INIT_FUNC
        assert len(plugin.get_attr_vals_utf8_l(REPL_PLUGIN_INIT_ATTR)) == 1

        # Verify dependency was updated in retro changelog plugin
        plugin = Plugins(topo.standalone).get(RETROCL_PLUGIN_NAME)
        assert plugin is not None
        assert plugin.get_attr_val_utf8_l(REPL_DEPENDS_ATTR) == REPL_PLUGIN_NAME.lower()
        assert len(plugin.get_attr_vals_utf8_l(REPL_DEPENDS_ATTR)) == 1

        # Verify dependency was updated in MEP plugin
        plugin = Plugins(topo.standalone).get(MEP_PLUGIN_NAME)
        assert plugin is not None
        assert plugin.get_attr_val_utf8_l(REPL_DEPENDS_ATTR) == REPL_PLUGIN_NAME.lower()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

