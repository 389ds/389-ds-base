# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import threading
import pytest
import random
from lib389 import DirSrv
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m4, topology_m2
from lib389._constants import *

pytestmark = pytest.mark.tier1

@pytest.mark.skipif(ds_is_older("1.4.1.6"), reason="Not implemented")
def test_max_tasks(topology_m4):
    """Test we can not create more than 64 cleaning tasks

    This test needs to be a standalone test becuase there is no easy way to 
    "restore" the instance after running this test

    :id: c34d0b40-3c3e-4f53-8656-5e4c2a310a1f
    :setup: Replication setup with four suppliers
    :steps:
        1. Stop suppliers 3 & 4
        2. Create over 64 tasks between m1 and m2
        3. Check logs to see if (>64) tasks were rejected

    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    # Stop suppliers 3 & 4
    m1 = topology_m4.ms["supplier1"]
    m2 = topology_m4.ms["supplier2"]
    m3 = topology_m4.ms["supplier3"]
    m4 = topology_m4.ms["supplier4"]
    m3.stop()
    m4.stop()

    # Add over 64 tasks between supplier1 & 2 to try to exceed the 64 task limit
    for i in range(1, 64):
        cruv_task = CleanAllRUVTask(m1)
        cruv_task.create(properties={
            'replica-id': str(i),
            'replica-base-dn': DEFAULT_SUFFIX,
            'replica-force-cleaning': 'no',  # This forces these tasks to stick around
        })
        cruv_task = CleanAllRUVTask(m2)
        cruv_task.create(properties={
            'replica-id': "10" + str(i),
            'replica-base-dn': DEFAULT_SUFFIX,
            'replica-force-cleaning': 'yes',  # This allows the tasks to propagate
        })

    # Check the errors log for our error message in supplier 1
    assert m1.searchErrorsLog('Exceeded maximum number of active CLEANALLRUV tasks')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

