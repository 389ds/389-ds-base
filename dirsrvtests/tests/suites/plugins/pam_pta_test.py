# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import logging
from lib389.plugins import PAMPassThroughAuthPlugin, PAMPassThroughAuthConfigs
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_default_config_entry(topology_st):
    """Verify that both default PAM PTA config entry and child entries can be searched.

    :id: e1f3a04b-deed-42b0-a98d-768f5b7f3fdf
    :setup: Standalone instance
    :steps:
        1. Create custom PAM PTA config entry
        2. Read attributes of the custom config entry
        3. Read attributes of the default config entry
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    inst = topology_st.standalone
    pam_pta = PAMPassThroughAuthPlugin(inst)
    configs = PAMPassThroughAuthConfigs(inst)
    default_name = "PAM Pass Through Auth"
    custom_name = "Custom PAM PTA Config"

    # Create a new PAM PTA config entry
    props = {"cn": custom_name}
    configs.create(properties=props)

    assert configs.get(custom_name)
    assert configs.get(default_name)
