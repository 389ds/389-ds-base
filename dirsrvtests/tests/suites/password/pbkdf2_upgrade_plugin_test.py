# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.topologies import topology_st
from lib389.password_plugins import PBKDF2SHA256Plugin
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1

@pytest.mark.skipif(ds_is_older('1.4.1'), reason="Not implemented")
def test_pbkdf2_upgrade(topology_st):
    """On upgrade pbkdf2 doesn't ship. We need to be able to
    provide this on upgrade to make sure default hashes work.
    However, password plugins are special - they need really
    early bootstap so that setting the default has specs work.

    This tests that the removal of the pbkdf2 plugin causes
    it to be re-bootstrapped and added.

    :id: c2198692-7c02-433b-af5b-3be54920571a
    :setup: Single instance
    :steps: 1. Remove the PBKDF2 plugin
            2. Restart the server
            3. Restart the server
    :expectedresults:
            1. Plugin is removed (IE pre-upgrade state)
            2. The plugin is bootstrapped and added
            3. No change (already bootstrapped)

    """
    # Remove the pbkdf2 plugin config
    p1 = PBKDF2SHA256Plugin(topology_st.standalone)
    assert(p1.exists())
    p1._protected = False
    p1.delete()
    # Restart
    topology_st.standalone.restart()
    # check it's been readded.
    p2 = PBKDF2SHA256Plugin(topology_st.standalone)
    assert(p2.exists())
    # Now restart to make sure we still work from the non-bootstrap form
    topology_st.standalone.restart()
    p3 = PBKDF2SHA256Plugin(topology_st.standalone)
    assert(p3.exists())


