# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json

import pytest
from lib389.cli_base import FakeArgs
from lib389.cli_conf.plugins.pwstorage import (
    pbkdf2_get_accept_max_iterations,
    pbkdf2_set_accept_max_iterations,
)
from lib389.password_plugins import PBKDF2SHA256LegacyPlugin
from test389.topologies import topology_st

from . import check_value_in_log_and_reset


pytestmark = pytest.mark.tier1

LEGACY_VARIANT = 'pbkdf2-sha256-legacy'
ACCEPT_MAX_ATTR = 'nsslapd-pwdPBKDF2AcceptMaxIterations'
PBKDF2_CONFIG_OC = 'pwdPBKDF2PluginConfig'
ROUNDTRIP_ACCEPT_MAX = 60000


@pytest.fixture(scope='function')
def legacy_pbkdf2_plugin(topology_st, request):
    plugin = PBKDF2SHA256LegacyPlugin(topology_st.standalone)
    original_accept_max = plugin.get_attr_val_utf8(ACCEPT_MAX_ATTR)
    original_config_oc = plugin.present('objectClass', PBKDF2_CONFIG_OC)

    def fin():
        cleanup_plugin = PBKDF2SHA256LegacyPlugin(topology_st.standalone)
        if original_config_oc:
            cleanup_plugin.ensure_present('objectClass', PBKDF2_CONFIG_OC)
        if original_accept_max is None:
            cleanup_plugin.remove_all(ACCEPT_MAX_ATTR)
        else:
            cleanup_plugin.replace(ACCEPT_MAX_ATTR, original_accept_max)
        if not original_config_oc:
            cleanup_plugin.ensure_removed('objectClass', PBKDF2_CONFIG_OC)

    request.addfinalizer(fin)
    topology_st.logcap.flush()
    return plugin


def test_dsconf_legacy_pbkdf2_accept_max_get_default(topology_st,
                                                     legacy_pbkdf2_plugin):
    """Verify dsconf reports the legacy PBKDF2 default accepted maximum

    :id: 6a81d1f4-8437-47d4-8c6b-58d2c56412ee
    :setup: Standalone instance
    :steps:
        1. Remove the configured legacy PBKDF2 accepted maximum
        2. Get the accepted maximum through the dsconf handler
    :expectedresults:
        1. The plugin has no explicit accepted maximum
        2. The shipped default of 50,000 iterations is logged
    """
    legacy_pbkdf2_plugin.remove_all(ACCEPT_MAX_ATTR)
    assert legacy_pbkdf2_plugin.get_attr_val_utf8(ACCEPT_MAX_ATTR) is None

    args = FakeArgs()
    args.variant = LEGACY_VARIANT
    args.json = False
    pbkdf2_get_accept_max_iterations(
        topology_st.standalone, None, topology_st.logcap.log, args
    )

    check_value_in_log_and_reset(
        topology_st,
        check_value=(
            'Current accept max iterations for '
            f'{LEGACY_VARIANT}: {PBKDF2SHA256LegacyPlugin.ACCEPT_MAX_DEFAULT}'
        ),
    )


def test_dsconf_legacy_pbkdf2_accept_max_set_get_roundtrip(
        topology_st, legacy_pbkdf2_plugin, capsys):
    """Verify dsconf sets and gets the legacy PBKDF2 accepted maximum

    :id: 3962eea0-f70a-46f1-befd-7d3ad493830b
    :setup: Standalone instance
    :steps:
        1. Remove the PBKDF2 configuration object class
        2. Set the accepted maximum to 60,000 through the dsconf handler
        3. Inspect the command log message
        4. Read the plugin configuration through lib389
        5. Get the accepted maximum as JSON through the dsconf handler
    :expectedresults:
        1. The plugin no longer has the configuration object class
        2. The accepted maximum is stored successfully
        3. The success message says that a server restart is required
        4. The value is 60,000 and the configuration object class is restored
        5. The JSON entry contains the plugin DN and accepted maximum
    """
    legacy_pbkdf2_plugin.remove_all(ACCEPT_MAX_ATTR)
    legacy_pbkdf2_plugin.remove('objectClass', PBKDF2_CONFIG_OC)
    assert not legacy_pbkdf2_plugin.present('objectClass', PBKDF2_CONFIG_OC)

    args = FakeArgs()
    args.variant = LEGACY_VARIANT
    args.iterations = ROUNDTRIP_ACCEPT_MAX
    pbkdf2_set_accept_max_iterations(
        topology_st.standalone, None, topology_st.logcap.log, args
    )

    check_value_in_log_and_reset(
        topology_st,
        content_list=[
            f'Successfully set accept max iterations for {LEGACY_VARIANT} '
            f'to {ROUNDTRIP_ACCEPT_MAX}',
            'A server restart is required for the change to take effect',
        ],
    )
    assert legacy_pbkdf2_plugin.get_accept_max_iterations() == ROUNDTRIP_ACCEPT_MAX
    assert legacy_pbkdf2_plugin.present('objectClass', PBKDF2_CONFIG_OC)

    args.json = True
    capsys.readouterr()
    pbkdf2_get_accept_max_iterations(
        topology_st.standalone, None, topology_st.logcap.log, args
    )
    result = json.loads(capsys.readouterr().out)
    assert result == {
        'type': 'entry',
        'dn': legacy_pbkdf2_plugin._dn,
        'attrs': {
            'nsslapd-pwdpbkdf2acceptmaxiterations': [ROUNDTRIP_ACCEPT_MAX],
        },
    }


def test_dsconf_legacy_pbkdf2_accept_max_validation(topology_st,
                                                    legacy_pbkdf2_plugin):
    """Verify dsconf rejects legacy PBKDF2 accepted maxima outside its bounds

    :id: 998ced54-c46f-49cf-a909-34126ea0756f
    :setup: Standalone instance
    :steps:
        1. Store a valid accepted maximum of 60,000 iterations
        2. Try to set a value below the supported minimum
        3. Try to set a value above the supported maximum
        4. Read the stored accepted maximum
    :expectedresults:
        1. The valid accepted maximum is stored
        2. A ValueError is raised
        3. A ValueError is raised
        4. The stored value remains unchanged
    """
    legacy_pbkdf2_plugin.set_accept_max_iterations(ROUNDTRIP_ACCEPT_MAX)

    args = FakeArgs()
    args.variant = LEGACY_VARIANT
    for invalid_value in (
            PBKDF2SHA256LegacyPlugin.ACCEPT_MAX_MIN - 1,
            PBKDF2SHA256LegacyPlugin.ACCEPT_MAX_MAX + 1):
        args.iterations = invalid_value
        with pytest.raises(ValueError):
            pbkdf2_set_accept_max_iterations(
                topology_st.standalone, None, topology_st.logcap.log, args
            )
        assert legacy_pbkdf2_plugin.get_accept_max_iterations() == ROUNDTRIP_ACCEPT_MAX
