# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
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
from lib389.cli_base import FakeArgs
from lib389.cli_conf.backend import backend_create_vlv, backend_create_vlv_index
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1
DEBUGGING = os.getenv("DEBUGGING", default=False)
logging.getLogger(__name__).setLevel(logging.DEBUG if DEBUGGING else logging.INFO)
log = logging.getLogger(__name__)

VLV_SEARCH_NAME = "vlv8103Srch"
VLV_INDEX_NAME = "vlv8103Idx"


def test_vlv_add_index_index_it_resolves_backend(topo):
    """dsconf vlv-index add-index --index-it must reindex using the suffix

    :id: 1838c61f-d3bb-46a4-9fe3-0d2270c43083
    :setup: Standalone instance
    :steps:
        1. Create a VLV search on userRoot
        2. Create a VLV sort index with --index-it
    :expectedresults:
        1. Success
        2. Reindex starts; it does not raise Failed to find backend matching the suffix
    """
    inst = topo.standalone
    args = FakeArgs()
    args.be_name = DEFAULT_BENAME
    args.name = VLV_SEARCH_NAME
    args.parent_name = VLV_SEARCH_NAME
    args.search_base = DEFAULT_SUFFIX
    args.search_scope = '2'
    args.search_filter = "objectClass=person"
    args.index_name = VLV_INDEX_NAME
    args.sort = "cn sn"
    args.index_it = True

    backend_create_vlv(inst, None, log, args)
    try:
        backend_create_vlv_index(inst, None, log, args)
    except ValueError as exc:
        pytest.fail("add-index --index-it failed to resolve backend: %s" % exc)
