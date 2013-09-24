# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from lib389._constants import *
from lib389 import DirSrv, Entry
import pytest
from lib389.utils import ensure_bytes, ensure_str

from lib389.mappingTree import MappingTrees, MappingTree

import sys
MAJOR, MINOR, _, _, _ = sys.version_info

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'

DEBUGGING = True

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    standalone = DirSrv(verbose=DEBUGGING)
    standalone.log.debug("Instance allocated")
    args = {SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
            # SER_DEPLOYED_DIR:  INSTANCE_PREFIX,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    standalone.allocate(args)
    if standalone.exists():
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        if not DEBUGGING:
            standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)

def test_mappingtree(topology):

    mts = MappingTrees(topology.standalone)
    mt = mts.create(properties={
        'cn': ["dc=newexample,dc=com",],
        'nsslapd-state' : 'backend',
        'nsslapd-backend' : 'someRoot',
        })

    rmt = mts.get('someRoot')
    rmt.delete()



