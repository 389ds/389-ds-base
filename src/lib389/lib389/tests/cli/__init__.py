# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest

from lib389 import DirSrv
from lib389.cli_base import LogCapture, FakeArgs

from lib389.instance.setup import SetupDs
from lib389.instance.options import General2Base, Slapd2Base
from lib389._constants import *

from lib389.topologies import create_topology, DEBUGGING

from lib389.utils import generate_ds_params
from lib389.configurations import get_sample_entries

@pytest.fixture(scope="module")
def topology(request):
    topology = create_topology({ReplicaRole.STANDALONE: 1}, None)
    def fin():
        if DEBUGGING:
            topology.standalone.stop()
        else:
            topology.standalone.delete()
    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology

@pytest.fixture(scope="module")
def topology_be_latest(request):
    topology = create_topology({ReplicaRole.STANDALONE: 1}, None)
    be = topology.standalone.backends.create(properties={
        'cn': 'userRoot',
        'suffix' : DEFAULT_SUFFIX,
    })
    # Now apply sample entries
    centries = get_sample_entries(INSTALL_LATEST_CONFIG)
    cent = centries(topology.standalone, DEFAULT_SUFFIX)
    cent.apply()

    def fin():
        if DEBUGGING:
            topology.standalone.stop()
        else:
            topology.standalone.delete()
    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology

@pytest.fixture(scope="module")
def topology_be_001003006(request):
    topology = create_topology({ReplicaRole.STANDALONE: 1}, None)
    be = topology.standalone.backends.create(properties={
        'cn': 'userRoot',
        'suffix' : DEFAULT_SUFFIX,
    })
    # Now apply sample entries
    centries = get_sample_entries('001003006')
    cent = centries(topology.standalone, DEFAULT_SUFFIX)
    cent.apply()

    def fin():
        if DEBUGGING:
            topology.standalone.stop()
        else:
            topology.standalone.delete()
    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


