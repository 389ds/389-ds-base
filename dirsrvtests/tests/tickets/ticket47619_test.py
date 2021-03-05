# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
'''
Created on Nov 7, 2013

@author: tbordaz
'''
import logging
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.properties import *
from lib389.topologies import topology_m1c1

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
ENTRY_DN = "cn=test_entry, %s" % SUFFIX

OTHER_NAME = 'other_entry'
MAX_OTHERS = 100

ATTRIBUTES = ['street', 'countryName', 'description', 'postalAddress', 'postalCode', 'title', 'l', 'roomNumber']


def test_ticket47619_init(topology_m1c1):
    """
        Initialize the test environment
    """
    topology_m1c1.ms["supplier1"].plugins.enable(name=PLUGIN_RETRO_CHANGELOG)
    # topology_m1c1.ms["supplier1"].plugins.enable(name=PLUGIN_MEMBER_OF)
    # topology_m1c1.ms["supplier1"].plugins.enable(name=PLUGIN_REFER_INTEGRITY)
    topology_m1c1.ms["supplier1"].stop(timeout=10)
    topology_m1c1.ms["supplier1"].start(timeout=10)

    topology_m1c1.ms["supplier1"].log.info("test_ticket47619_init topology_m1c1 %r" % (topology_m1c1))
    # the test case will check if a warning message is logged in the
    # error log of the supplier
    topology_m1c1.ms["supplier1"].errorlog_file = open(topology_m1c1.ms["supplier1"].errlog, "r")

    # add dummy entries
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology_m1c1.ms["supplier1"].add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))

    topology_m1c1.ms["supplier1"].log.info(
        "test_ticket47619_init: %d entries ADDed %s[0..%d]" % (MAX_OTHERS, OTHER_NAME, MAX_OTHERS - 1))

    # Check the number of entries in the retro changelog
    time.sleep(2)
    ents = topology_m1c1.ms["supplier1"].search_s(RETROCL_SUFFIX, ldap.SCOPE_ONELEVEL, "(objectclass=*)")
    assert len(ents) == MAX_OTHERS


def test_ticket47619_create_index(topology_m1c1):
    args = {INDEX_TYPE: 'eq'}
    for attr in ATTRIBUTES:
        topology_m1c1.ms["supplier1"].index.create(suffix=RETROCL_SUFFIX, attr=attr, args=args)
    topology_m1c1.ms["supplier1"].restart(timeout=10)


def test_ticket47619_reindex(topology_m1c1):
    '''
    Reindex all the attributes in ATTRIBUTES
    '''
    args = {TASK_WAIT: True}
    for attr in ATTRIBUTES:
        rc = topology_m1c1.ms["supplier1"].tasks.reindex(suffix=RETROCL_SUFFIX, attrname=attr, args=args)
        assert rc == 0


def test_ticket47619_check_indexed_search(topology_m1c1):
    for attr in ATTRIBUTES:
        ents = topology_m1c1.ms["supplier1"].search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE, "(%s=hello)" % attr)
        assert len(ents) == 0


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
