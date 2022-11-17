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
import time
import ldap
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389 import Entry

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


USER_CN='user_'
def _user_get_dn(no):
    cn = '%s%d' % (USER_CN, no)
    dn = 'cn=%s,ou=people,%s' % (cn, SUFFIX)
    return (cn, dn)

def add_user(server, no, desc='dummy', sleep=True):
    (cn, dn) = _user_get_dn(no)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'inetuser', 'userSecurityInformation'],
                             'cn': [cn],
                             'description': [desc],
                             'sn': [cn],
                             'description': ['add on that host']})))
    if sleep:
        time.sleep(2)

def test_ticket49471(topo):
    """Specify a test case purpose or name here

    :id: 457ab172-9455-4eb2-89a0-150e3de5993f
    :setup: Fill in set up configuration here
    :steps:
        1. Fill in test case steps here
        2. And indent them like this (RST format requirement)
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    S1 = topo.standalone
    add_user(S1, 1)

    Filter = "(description:2.16.840.1.113730.3.3.2.1.1.6:=\*on\*)"
    ents = S1.search_s(SUFFIX, ldap.SCOPE_SUBTREE, Filter)
    assert len(ents) == 1

    #
    # The following is for the test 49491
    # skipped here else it crashes in ASAN
    #Filter = "(description:2.16.840.1.113730.3.3.2.1.1.6:=\*host)"
    #ents = S1.search_s(SUFFIX, ldap.SCOPE_SUBTREE, Filter)
    #assert len(ents) == 1

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

