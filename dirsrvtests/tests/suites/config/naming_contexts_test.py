# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import logging
import os
import pytest
from lib389._constants import *
from lib389.rootdse import RootDSE
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1
DEBUGGING = os.getenv("DEBUGGING", default=False)
logging.getLogger(__name__).setLevel(logging.DEBUG if DEBUGGING else logging.INFO)
log = logging.getLogger(__name__)


def test_namingcontexts_includes_internal_suffixes(topo):
    """The root DSE namingContexts must advertise cn=config, cn=monitor
    and cn=dynamiccertificates

    :id: 68abccd5-ec49-4be4-a34c-1a48f9a2ec75
    :setup: Standalone instance
    :steps:
        1. Read the namingContexts attribute from the root DSE
        2. Assert the regular suffix is present
        3. Assert cn=config is present
        4. Assert cn=monitor is present
        5. Assert cn=dynamiccertificates is present
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    root_dse = RootDSE(topo.standalone)
    contexts = root_dse.get_attr_vals_utf8('namingcontexts')
    log.info("namingContexts: %s", contexts)

    # Compare case-insensitively since DN case is not significant.
    contexts_lc = [c.lower() for c in contexts]

    assert DEFAULT_SUFFIX.lower() in contexts_lc
    assert 'cn=config' in contexts_lc
    assert 'cn=monitor' in contexts_lc
    assert 'cn=dynamiccertificates' in contexts_lc


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
