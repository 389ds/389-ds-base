# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
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
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389.mappingTree import MappingTrees

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_invalid_mt(topo):
    """Test that you can not add a new suffix/mapping tree
    that does not already have the backend entry created.

    :id: caabd407-f541-4695-b13f-8f92af1112a0
    :setup: Standalone Instance
    :steps:
        1. Create a new suffix that specifies an existing backend which has a
           different suffix.
        2. Create a suffix that has no backend entry at all.
    :expectedresults:
        1. Should fail with UNWILLING_TO_PERFORM
        1. Should fail with UNWILLING_TO_PERFORM
    """

    bad_suffix = 'dc=does,dc=not,dc=exist'
    mts = MappingTrees(topo.standalone)
    
    properties = {
        'cn': bad_suffix,
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userroot',
    }
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        mts.create(properties=properties)

    properties = {
        'cn': bad_suffix,
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'notCreatedRoot',
    }
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        mts.create(properties=properties)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

