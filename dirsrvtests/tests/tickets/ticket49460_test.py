# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import ldap
import logging
import pytest
import os
import re
from lib389._constants import *
from lib389.config import Config
from lib389 import DirSrv, Entry
from lib389.topologies import topology_m3 as topo

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

USER_CN="user"

def add_user(server, no, desc='dummy', sleep=True):
    cn = '%s%d' % (USER_CN, no)
    dn = 'cn=%s,ou=people,%s' % (cn, SUFFIX)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'inetuser', 'userSecurityInformation'],
                             'sn': ['_%s' % cn],
                             'description': [desc]})))
    time.sleep(1)

def check_user(server, no, timeout=10):

    cn = '%s%d' % (USER_CN, no)
    dn = 'cn=%s,ou=people,%s' % (cn, SUFFIX)
    found = False
    cpt = 0
    while cpt < timeout:
        try:
            server.getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
            found = True
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            cpt += 1
    return found

def pattern_errorlog(server, log_pattern):
    file_obj = open(server.errlog, "r")

    found = None
    # Use a while true iteration because 'for line in file: hit a
    while True:
        line = file_obj.readline()
        found = log_pattern.search(line)
        if ((line == '') or (found)):
            break

    return found

def test_ticket_49460(topo):
    """Specify a test case purpose or name here

    :id: d1aa2e8b-e6ab-4fc6-9c63-c6f622544f2d
    :setup: Fill in set up configuration here
    :steps:
        1. Enable replication logging
        2. Do few updates to generatat RUV update
    :expectedresults:
        1. No report of failure when the RUV is updated
    """

    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]

    for i in (M1, M2, M3):
        i.config.loglevel(vals=[256 + 4], service='access')
        i.config.loglevel(vals=[LOG_REPLICA, LOG_DEFAULT], service='error')

    add_user(M1, 11, desc="add to M1")
    add_user(M2, 21, desc="add to M2")
    add_user(M3, 31, desc="add to M3")

    for i in (M1, M2, M3):
            assert check_user(i, 11)
            assert check_user(i, 21)
            assert check_user(i, 31)

    time.sleep(10)

    #M1.tasks.cleanAllRUV(suffix=SUFFIX, replicaid='3',
    #                    force=False, args={TASK_WAIT: True})
    #time.sleep(10)
    regex = re.compile(".*Failed to update RUV tombstone.*LDAP error - 0")
    assert not pattern_errorlog(M1, regex)
    assert not pattern_errorlog(M2, regex)
    assert not pattern_errorlog(M3, regex)

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

