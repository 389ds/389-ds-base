# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import time
from lib389._constants import *
from lib389.topologies import topology_st as topo, set_timeout

logging.basicConfig(format='%(asctime)s %(message)s', force=True)
log = logging.getLogger(__name__)
log.setLevel(logging.DEBUG)
# create console handler with a higher log level
ch = logging.StreamHandler()
ch.setLevel(logging.DEBUG)
# create formatter and add it to the handlers
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
ch.setFormatter(formatter)
# add the handlers to logger
log.addHandler(ch)

TEST_TIMEOUT = 150

@pytest.fixture(autouse=True, scope="module")
def init_timeout():
    set_timeout(TEST_TIMEOUT)

def test_timeout(topo):
    """Specify a test case purpose or name here

    :id: 4a2917d2-ad4c-44a7-aa5f-daad26d1d36e
    :setup: Standalone Instance
    :steps:
        1. Fill in test case steps here
        2. And indent them like this (RST format requirement)
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    with pytest.raises(TimeoutError):
        log.info("Start waiting %d seconds" % TEST_TIMEOUT )
        time.sleep(TEST_TIMEOUT)
        log.info("End waiting")
    for inst in topo:
        assert inst.status() is False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

