# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import ldap
import time
from lib389._constants import *
from lib389.topologies import topology_m1c1 as topo
from lib389._constants import *
from lib389 import Entry

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)
CHANGELOG = 'cn=changelog5,cn=config'
MAXAGE_ATTR = 'nsslapd-changelogmaxage'
TRIMINTERVAL = 'nsslapd-changelogtrim-interval'



def test_ticket49412(topo):
    """Specify a test case purpose or name here

    :id: 4c7681ff-0511-4256-9589-bdcad84c13e6
    :setup: Fill in set up configuration here
    :steps:
        1. Fill in test case steps here
        2. And indent them like this (RST format requirement)
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    M1 = topo.ms["supplier1"]

    # wrong call with invalid value (should be str(60)
    # that create replace with NULL value
    # it should fail with UNWILLING_TO_PERFORM
    try:
        M1.modify_s(CHANGELOG, [(ldap.MOD_REPLACE, MAXAGE_ATTR, 60),
                                (ldap.MOD_REPLACE, TRIMINTERVAL, 10)])
        assert(False)
    except ldap.UNWILLING_TO_PERFORM:
        pass

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

