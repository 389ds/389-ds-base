
# --- BEGIN COPYRIGHT BLOCK ---
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
from lib389.topologies import topology_st as topology
from lib389.utils import ds_is_older
from lib389.paths import Paths
from lib389.plugins import MemberOfPlugin

default_paths = Paths()
pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

@pytest.mark.skipif(ds_is_older('1.4.4.0'), reason="Notice not generated in older versions")
def test_notice_when_dynamic_not_enabled(topology):
    """ Test to show the logged noticed when dynamic plugins is disabled.

    :id: e4923789-c187-44b0-8734-34f26cbae06e

    :setup: Standalone instance

    :steps:
        1. Ensure Dynamic Plugins is disabled
        2. Enable a plugin

    :expectedresults:
        1. Success
        2. Notice generated
    """
    st = topology.standalone

    st.config.set("nsslapd-dynamic-plugins", "off")
    st.restart()

    mo = MemberOfPlugin(st)
    mo.enable()
    # Now check the error log.
    pattern = ".*nsslapd-dynamic-plugins is off.*"
    assert st.ds_error_log.match(pattern)


