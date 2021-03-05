# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
'''
Created on April 14, 2014

@author: tbordaz
'''
import logging
import re
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_m2
from lib389.utils import *

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

# set this flag to False so that it will assert on failure _status_entry_both_server
DEBUG_FLAG = False

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX

STAGING_CN = "staged user"
PRODUCTION_CN = "accounts"
EXCEPT_CN = "excepts"

STAGING_DN = "cn=%s,%s" % (STAGING_CN, SUFFIX)
PRODUCTION_DN = "cn=%s,%s" % (PRODUCTION_CN, SUFFIX)
PROD_EXCEPT_DN = "cn=%s,%s" % (EXCEPT_CN, PRODUCTION_DN)

STAGING_PATTERN = "cn=%s*,%s" % (STAGING_CN[:2], SUFFIX)
PRODUCTION_PATTERN = "cn=%s*,%s" % (PRODUCTION_CN[:2], SUFFIX)
BAD_STAGING_PATTERN = "cn=bad*,%s" % (SUFFIX)
BAD_PRODUCTION_PATTERN = "cn=bad*,%s" % (SUFFIX)

BIND_CN = "bind_entry"
BIND_DN = "cn=%s,%s" % (BIND_CN, SUFFIX)
BIND_PW = "password"

NEW_ACCOUNT = "new_account"
MAX_ACCOUNTS = 20

CONFIG_MODDN_ACI_ATTR = "nsslapd-moddn-aci"


def _bind_manager(server):
    server.log.info("Bind as %s " % DN_DM)
    server.simple_bind_s(DN_DM, PASSWORD)


def _bind_normal(server):
    server.log.info("Bind as %s " % BIND_DN)
    server.simple_bind_s(BIND_DN, BIND_PW)


def _header(topology_m2, label):
    topology_m2.ms["supplier1"].log.info("\n\n###############################################")
    topology_m2.ms["supplier1"].log.info("#######")
    topology_m2.ms["supplier1"].log.info("####### %s" % label)
    topology_m2.ms["supplier1"].log.info("#######")
    topology_m2.ms["supplier1"].log.info("###############################################")


def _status_entry_both_server(topology_m2, name=None, desc=None, debug=True):
    if not name:
        return
    topology_m2.ms["supplier1"].log.info("\n\n######################### Tombstone on M1 ######################\n")
    attr = 'description'
    found = False
    attempt = 0
    while not found and attempt < 10:
        ent_m1 = _find_tombstone(topology_m2.ms["supplier1"], SUFFIX, 'sn', name)
        if attr in ent_m1.getAttrs():
            found = True
        else:
            time.sleep(1)
            attempt = attempt + 1
    assert ent_m1

    topology_m2.ms["supplier1"].log.info("\n\n######################### Tombstone on M2 ######################\n")
    ent_m2 = _find_tombstone(topology_m2.ms["supplier2"], SUFFIX, 'sn', name)
    assert ent_m2

    topology_m2.ms["supplier1"].log.info("\n\n######################### Description ######################\n%s\n" % desc)
    topology_m2.ms["supplier1"].log.info("M1 only\n")
    for attr in ent_m1.getAttrs():

        if not debug:
            assert attr in ent_m2.getAttrs()

        if not attr in ent_m2.getAttrs():
            topology_m2.ms["supplier1"].log.info("    %s" % attr)
            for val in ent_m1.getValues(attr):
                topology_m2.ms["supplier1"].log.info("        %s" % val)

    topology_m2.ms["supplier1"].log.info("M2 only\n")
    for attr in ent_m2.getAttrs():

        if not debug:
            assert attr in ent_m1.getAttrs()

        if not attr in ent_m1.getAttrs():
            topology_m2.ms["supplier1"].log.info("    %s" % attr)
            for val in ent_m2.getValues(attr):
                topology_m2.ms["supplier1"].log.info("        %s" % val)

    topology_m2.ms["supplier1"].log.info("M1 differs M2\n")

    if not debug:
        assert ent_m1.dn == ent_m2.dn

    if ent_m1.dn != ent_m2.dn:
        topology_m2.ms["supplier1"].log.info("    M1[dn] = %s\n    M2[dn] = %s" % (ent_m1.dn, ent_m2.dn))

    for attr1 in ent_m1.getAttrs():
        if attr1 in ent_m2.getAttrs():
            for val1 in ent_m1.getValues(attr1):
                found = False
                for val2 in ent_m2.getValues(attr1):
                    if val1 == val2:
                        found = True
                        break

                if not debug:
                    assert found

                if not found:
                    topology_m2.ms["supplier1"].log.info("    M1[%s] = %s" % (attr1, val1))

    for attr2 in ent_m2.getAttrs():
        if attr2 in ent_m1.getAttrs():
            for val2 in ent_m2.getValues(attr2):
                found = False
                for val1 in ent_m1.getValues(attr2):
                    if val2 == val1:
                        found = True
                        break

                if not debug:
                    assert found

                if not found:
                    topology_m2.ms["supplier1"].log.info("    M2[%s] = %s" % (attr2, val2))


def _pause_RAs(topology_m2):
    topology_m2.ms["supplier1"].log.info("\n\n######################### Pause RA M1<->M2 ######################\n")
    ents = topology_m2.ms["supplier1"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology_m2.ms["supplier1"].agreement.pause(ents[0].dn)

    ents = topology_m2.ms["supplier2"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology_m2.ms["supplier2"].agreement.pause(ents[0].dn)


def _resume_RAs(topology_m2):
    topology_m2.ms["supplier1"].log.info("\n\n######################### resume RA M1<->M2 ######################\n")
    ents = topology_m2.ms["supplier1"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology_m2.ms["supplier1"].agreement.resume(ents[0].dn)

    ents = topology_m2.ms["supplier2"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology_m2.ms["supplier2"].agreement.resume(ents[0].dn)


def _find_tombstone(instance, base, attr, value):
    #
    # we can not use a filter with a (&(objeclass=nsTombstone)(sn=name)) because
    # tombstone are not index in 'sn' so 'sn=name' will return NULL
    # and even if tombstone are indexed for objectclass the '&' will set
    # the candidate list to NULL
    #
    filt = '(objectclass=%s)' % REPLICA_OC_TOMBSTONE
    ents = instance.search_s(base, ldap.SCOPE_SUBTREE, filt)
    # found = False
    for ent in ents:
        if ent.hasAttr(attr):
            for val in ent.getValues(attr):
                if ensure_str(val) == value:
                    instance.log.debug("tombstone found: %r" % ent)
                    return ent
    return None


def _delete_entry(instance, entry_dn, name):
    instance.log.info("\n\n######################### DELETE %s (M1) ######################\n" % name)

    # delete the entry
    instance.delete_s(entry_dn)
    ent = _find_tombstone(instance, SUFFIX, 'sn', name)
    assert ent is not None


def _mod_entry(instance, entry_dn, attr, value):
    instance.log.info("\n\n######################### MOD %s (M2) ######################\n" % entry_dn)
    mod = [(ldap.MOD_REPLACE, attr, ensure_bytes(value))]
    instance.modify_s(entry_dn, mod)


def _modrdn_entry(instance=None, entry_dn=None, new_rdn=None, del_old=0, new_superior=None):
    assert instance is not None
    assert entry_dn is not None

    if not new_rdn:
        pattern = 'cn=(.*),(.*)'
        rdnre = re.compile(pattern)
        match = rdnre.match(entry_dn)
        old_value = match.group(1)
        new_rdn_val = "%s_modrdn" % old_value
        new_rdn = "cn=%s" % new_rdn_val

    instance.log.info("\n\n######################### MODRDN %s (M2) ######################\n" % new_rdn)
    if new_superior:
        instance.rename_s(entry_dn, new_rdn, newsuperior=new_superior, delold=del_old)
    else:
        instance.rename_s(entry_dn, new_rdn, delold=del_old)


def _check_entry_exists(instance, entry_dn):
    loop = 0
    ent = None
    while loop <= 10:
        try:
            ent = instance.getEntry(entry_dn, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    if ent is None:
        assert False


def _check_mod_received(instance, base, filt, attr, value):
    instance.log.info(
        "\n\n######################### Check MOD replicated on %s ######################\n" % instance.serverid)
    loop = 0
    while loop <= 10:
        ent = instance.getEntry(base, ldap.SCOPE_SUBTREE, filt)
        if ent.hasAttr(attr) and ent.getValue(attr) == value:
            break
        time.sleep(1)
        loop += 1
    assert loop <= 10


def _check_replication(topology_m2, entry_dn):
    # prepare the filter to retrieve the entry
    filt = entry_dn.split(',')[0]

    topology_m2.ms["supplier1"].log.info("\n######################### Check replicat M1->M2 ######################\n")
    loop = 0
    while loop <= 10:
        attr = 'description'
        value = 'test_value_%d' % loop
        mod = [(ldap.MOD_REPLACE, attr, ensure_bytes(value))]
        topology_m2.ms["supplier1"].modify_s(entry_dn, mod)
        _check_mod_received(topology_m2.ms["supplier2"], SUFFIX, filt, attr, value)
        loop += 1

    topology_m2.ms["supplier1"].log.info("\n######################### Check replicat M2->M1 ######################\n")
    loop = 0
    while loop <= 10:
        attr = 'description'
        value = 'test_value_%d' % loop
        mod = [(ldap.MOD_REPLACE, attr, ensure_bytes(value))]
        topology_m2.ms["supplier2"].modify_s(entry_dn, mod)
        _check_mod_received(topology_m2.ms["supplier1"], SUFFIX, filt, attr, value)
        loop += 1


def test_ticket47787_init(topology_m2):
    """
        Creates
            - a staging DIT
            - a production DIT
            - add accounts in staging DIT

    """

    topology_m2.ms["supplier1"].log.info("\n\n######################### INITIALIZATION ######################\n")

    # entry used to bind with
    topology_m2.ms["supplier1"].log.info("Add %s" % BIND_DN)
    topology_m2.ms["supplier1"].add_s(Entry((BIND_DN, {
        'objectclass': "top person".split(),
        'sn': BIND_CN,
        'cn': BIND_CN,
        'userpassword': BIND_PW})))

    # DIT for staging
    topology_m2.ms["supplier1"].log.info("Add %s" % STAGING_DN)
    topology_m2.ms["supplier1"].add_s(Entry((STAGING_DN, {
        'objectclass': "top organizationalRole".split(),
        'cn': STAGING_CN,
        'description': "staging DIT"})))

    # DIT for production
    topology_m2.ms["supplier1"].log.info("Add %s" % PRODUCTION_DN)
    topology_m2.ms["supplier1"].add_s(Entry((PRODUCTION_DN, {
        'objectclass': "top organizationalRole".split(),
        'cn': PRODUCTION_CN,
        'description': "production DIT"})))

    # enable replication error logging
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'8192')]
    topology_m2.ms["supplier1"].modify_s(DN_CONFIG, mod)
    topology_m2.ms["supplier2"].modify_s(DN_CONFIG, mod)

    # add dummy entries in the staging DIT
    for cpt in range(MAX_ACCOUNTS):
        name = "%s%d" % (NEW_ACCOUNT, cpt)
        topology_m2.ms["supplier1"].add_s(Entry(("cn=%s,%s" % (name, STAGING_DN), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))


def test_ticket47787_2(topology_m2):
    '''
    Disable replication so that updates are not replicated
    Delete an entry on M1. Modrdn it on M2 (chg rdn + delold=0 + same superior).
    update a test entry on M2
    Reenable the RA.
    checks that entry was deleted on M2 (with the modified RDN)
    checks that test entry was replicated on M1 (replication M2->M1 not broken by modrdn)
    '''

    _header(topology_m2, "test_ticket47787_2")
    _bind_manager(topology_m2.ms["supplier1"])
    _bind_manager(topology_m2.ms["supplier2"])

    # entry to test the replication is still working
    name = "%s%d" % (NEW_ACCOUNT, MAX_ACCOUNTS - 1)
    test_rdn = "cn=%s" % (name)
    testentry_dn = "%s,%s" % (test_rdn, STAGING_DN)

    name = "%s%d" % (NEW_ACCOUNT, MAX_ACCOUNTS - 2)
    test2_rdn = "cn=%s" % (name)
    testentry2_dn = "%s,%s" % (test2_rdn, STAGING_DN)

    # value of updates to test the replication both ways
    attr = 'description'
    value = 'test_ticket47787_2'

    # entry for the modrdn
    name = "%s%d" % (NEW_ACCOUNT, 1)
    rdn = "cn=%s" % (name)
    entry_dn = "%s,%s" % (rdn, STAGING_DN)

    # created on M1, wait the entry exists on M2
    _check_entry_exists(topology_m2.ms["supplier2"], entry_dn)
    _check_entry_exists(topology_m2.ms["supplier2"], testentry_dn)

    _pause_RAs(topology_m2)

    # Delete 'entry_dn' on M1.
    # dummy update is only have a first CSN before the DEL
    # else the DEL will be in min_csn RUV and make diagnostic a bit more complex
    _mod_entry(topology_m2.ms["supplier1"], testentry2_dn, attr, 'dummy')
    _delete_entry(topology_m2.ms["supplier1"], entry_dn, name)
    _mod_entry(topology_m2.ms["supplier1"], testentry2_dn, attr, value)

    time.sleep(1)  # important to have MOD.csn != DEL.csn

    # MOD 'entry_dn' on M1.
    # dummy update is only have a first CSN before the MOD entry_dn
    # else the DEL will be in min_csn RUV and make diagnostic a bit more complex
    _mod_entry(topology_m2.ms["supplier2"], testentry_dn, attr, 'dummy')
    _mod_entry(topology_m2.ms["supplier2"], entry_dn, attr, value)
    _mod_entry(topology_m2.ms["supplier2"], testentry_dn, attr, value)

    _resume_RAs(topology_m2)

    topology_m2.ms["supplier1"].log.info(
        "\n\n######################### Check DEL replicated on M2 ######################\n")
    loop = 0
    while loop <= 10:
        ent = _find_tombstone(topology_m2.ms["supplier2"], SUFFIX, 'sn', name)
        if ent:
            break
        time.sleep(1)
        loop += 1
    assert loop <= 10
    assert ent

    # the following checks are not necessary
    # as this bug is only for failing replicated MOD (entry_dn) on M1
    # _check_mod_received(topology_m2.ms["supplier1"], SUFFIX, "(%s)" % (test_rdn), attr, value)
    # _check_mod_received(topology_m2.ms["supplier2"], SUFFIX, "(%s)" % (test2_rdn), attr, value)
    #
    # _check_replication(topology_m2, testentry_dn)

    _status_entry_both_server(topology_m2, name=name, desc="DEL M1 - MOD M2", debug=DEBUG_FLAG)

    topology_m2.ms["supplier1"].log.info(
        "\n\n######################### Check MOD replicated on M1 ######################\n")
    loop = 0
    while loop <= 10:
        ent = _find_tombstone(topology_m2.ms["supplier1"], SUFFIX, 'sn', name)
        if ent:
            break
        time.sleep(1)
        loop += 1
    assert loop <= 10
    assert ent
    assert ent.hasAttr(attr)
    assert ensure_str(ent.getValue(attr)) == value


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
