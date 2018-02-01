# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import pytest
from lib389.topologies import create_topology
from lib389._constants import ReplicaRole

DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


# Redefine some fixtures so we can use them with class scope
@pytest.fixture(scope="class")
def topology_m2(request):
    """Create Replication Deployment with two masters"""

    topology = create_topology({ReplicaRole.MASTER: 2})

    def fin():
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            [inst.delete() for inst in topology]
    request.addfinalizer(fin)

    return topology


@pytest.fixture(scope="class")
def topology_m3(request):
    """Create Replication Deployment with three masters"""

    topology = create_topology({ReplicaRole.MASTER: 3})

    def fin():
        if DEBUGGING:
            [inst.stop() for inst in topology]
        else:
            [inst.delete() for inst in topology]
    request.addfinalizer(fin)

    return topology
