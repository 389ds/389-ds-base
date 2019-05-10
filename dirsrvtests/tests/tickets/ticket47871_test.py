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
from lib389.topologies import topology_m1c1

logging.getLogger(__name__).setLevel(logging.DEBUG)
from lib389.utils import *

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.2'), reason="Not implemented")]
log = logging.getLogger(__name__)

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
ENTRY_DN = "cn=test_entry, %s" % SUFFIX

OTHER_NAME = 'other_entry'
MAX_OTHERS = 10

ATTRIBUTES = ['street', 'countryName', 'description', 'postalAddress', 'postalCode', 'title', 'l', 'roomNumber']


def test_ticket47871_init(topology_m1c1):
    """
        Initialize the test environment
    """
    topology_m1c1.ms["master1"].plugins.enable(name=PLUGIN_RETRO_CHANGELOG)
    mod = [(ldap.MOD_REPLACE, 'nsslapd-changelogmaxage', b"10s"),  # 10 second triming
           (ldap.MOD_REPLACE, 'nsslapd-changelog-trim-interval', b"5s")]
    topology_m1c1.ms["master1"].modify_s("cn=%s,%s" % (PLUGIN_RETRO_CHANGELOG, DN_PLUGIN), mod)
    # topology_m1c1.ms["master1"].plugins.enable(name=PLUGIN_MEMBER_OF)
    # topology_m1c1.ms["master1"].plugins.enable(name=PLUGIN_REFER_INTEGRITY)
    topology_m1c1.ms["master1"].stop(timeout=10)
    topology_m1c1.ms["master1"].start(timeout=10)

    topology_m1c1.ms["master1"].log.info("test_ticket47871_init topology_m1c1 %r" % (topology_m1c1))
    # the test case will check if a warning message is logged in the
    # error log of the supplier
    topology_m1c1.ms["master1"].errorlog_file = open(topology_m1c1.ms["master1"].errlog, "r")


def test_ticket47871_1(topology_m1c1):
    '''
    ADD entries and check they are all in the retrocl
    '''
    # add dummy entries
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology_m1c1.ms["master1"].add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))

    topology_m1c1.ms["master1"].log.info(
        "test_ticket47871_init: %d entries ADDed %s[0..%d]" % (MAX_OTHERS, OTHER_NAME, MAX_OTHERS - 1))

    # Check the number of entries in the retro changelog
    time.sleep(1)
    ents = topology_m1c1.ms["master1"].search_s(RETROCL_SUFFIX, ldap.SCOPE_ONELEVEL, "(objectclass=*)")
    assert len(ents) == MAX_OTHERS
    topology_m1c1.ms["master1"].log.info("Added entries are")
    for ent in ents:
        topology_m1c1.ms["master1"].log.info("%s" % ent.dn)


def test_ticket47871_2(topology_m1c1):
    '''
    Wait until there is just a last entries
    '''
    MAX_TRIES = 10
    TRY_NO = 1
    while TRY_NO <= MAX_TRIES:
        time.sleep(6)  # at least 1 trimming occurred
        ents = topology_m1c1.ms["master1"].search_s(RETROCL_SUFFIX, ldap.SCOPE_ONELEVEL, "(objectclass=*)")
        assert len(ents) <= MAX_OTHERS
        topology_m1c1.ms["master1"].log.info("\nTry no %d it remains %d entries" % (TRY_NO, len(ents)))
        for ent in ents:
            topology_m1c1.ms["master1"].log.info("%s" % ent.dn)
        if len(ents) > 1:
            TRY_NO += 1
        else:
            break
    assert TRY_NO <= MAX_TRIES
    assert len(ents) <= 1


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
