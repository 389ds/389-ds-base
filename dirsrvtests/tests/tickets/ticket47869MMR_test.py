# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_m2
from lib389.replica import ReplicationManager
from lib389.utils import *

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
ENTRY_NAME = 'test_entry'
MAX_ENTRIES = 10

BIND_NAME = 'bind_entry'
BIND_DN = 'cn=%s, %s' % (BIND_NAME, SUFFIX)
BIND_PW = 'password'

def replication_check(topology_m2):
    repl = ReplicationManager(SUFFIX)
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    return repl.test_replication(supplier1, supplier2)

def test_ticket47869_init(topology_m2):
    """
        It adds an entry ('bind_entry') and 10 test entries
        It sets the anonymous aci

    """
    # enable acl error logging
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', ensure_bytes(str(8192)))]  # REPL
    topology_m2.ms["supplier1"].modify_s(DN_CONFIG, mod)
    topology_m2.ms["supplier2"].modify_s(DN_CONFIG, mod)

    # entry used to bind with
    topology_m2.ms["supplier1"].log.info("Add %s" % BIND_DN)
    topology_m2.ms["supplier1"].add_s(Entry((BIND_DN, {
        'objectclass': "top person".split(),
        'sn': BIND_NAME,
        'cn': BIND_NAME,
        'userpassword': BIND_PW})))
    replication_check(topology_m2)
    ent = topology_m2.ms["supplier2"].getEntry(BIND_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent
    # keep anonymous ACI for use 'read-search' aci in SEARCH test
    ACI_ANONYMOUS = "(targetattr!=\"userPassword\")(version 3.0; acl \"Enable anonymous access\"; allow (read, search, compare) userdn=\"ldap:///anyone\";)"
    mod = [(ldap.MOD_REPLACE, 'aci', ensure_bytes(ACI_ANONYMOUS))]
    topology_m2.ms["supplier1"].modify_s(SUFFIX, mod)
    topology_m2.ms["supplier2"].modify_s(SUFFIX, mod)

    # add entries
    for cpt in range(MAX_ENTRIES):
        name = "%s%d" % (ENTRY_NAME, cpt)
        mydn = "cn=%s,%s" % (name, SUFFIX)
        topology_m2.ms["supplier1"].add_s(Entry((mydn,
                                               {'objectclass': "top person".split(),
                                                'sn': name,
                                                'cn': name})))
        replication_check(topology_m2)
        ent = topology_m2.ms["supplier2"].getEntry(mydn, ldap.SCOPE_BASE, "(objectclass=*)")
        assert ent

def test_ticket47869_check(topology_m2):
    '''
    On Supplier 1 and 2:
      Bind as Directory Manager.
      Search all specifying nscpEntryWsi in the attribute list.
      Check nscpEntryWsi is returned.
    On Supplier 1 and 2:
      Bind as Bind Entry.
      Search all specifying nscpEntryWsi in the attribute list.
      Check nscpEntryWsi is not returned.
    On Supplier 1 and 2:
      Bind as anonymous.
      Search all specifying nscpEntryWsi in the attribute list.
      Check nscpEntryWsi is not returned.
    '''
    topology_m2.ms["supplier1"].log.info("\n\n######################### CHECK nscpentrywsi ######################\n")

    topology_m2.ms["supplier1"].log.info("##### Supplier1: Bind as %s #####" % DN_DM)
    topology_m2.ms["supplier1"].simple_bind_s(DN_DM, PASSWORD)

    topology_m2.ms["supplier1"].log.info("Supplier1: Calling search_ext...")
    msgid = topology_m2.ms["supplier1"].search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology_m2.ms["supplier1"].result2(msgid)
    topology_m2.ms["supplier1"].log.info("%d results" % len(rdata))

    topology_m2.ms["supplier1"].log.info("Results:")
    for dn, attrs in rdata:
        topology_m2.ms["supplier1"].log.info("dn: %s" % dn)
        if 'nscpentrywsi' in attrs:
            nscpentrywsicnt += 1

    topology_m2.ms["supplier1"].log.info("Supplier1: count of nscpentrywsi: %d" % nscpentrywsicnt)

    topology_m2.ms["supplier2"].log.info("##### Supplier2: Bind as %s #####" % DN_DM)
    topology_m2.ms["supplier2"].simple_bind_s(DN_DM, PASSWORD)

    topology_m2.ms["supplier2"].log.info("Supplier2: Calling search_ext...")
    msgid = topology_m2.ms["supplier2"].search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology_m2.ms["supplier2"].result2(msgid)
    topology_m2.ms["supplier2"].log.info("%d results" % len(rdata))

    topology_m2.ms["supplier2"].log.info("Results:")
    for dn, attrs in rdata:
        topology_m2.ms["supplier2"].log.info("dn: %s" % dn)
        if 'nscpentrywsi' in attrs:
            nscpentrywsicnt += 1

    topology_m2.ms["supplier2"].log.info("Supplier2: count of nscpentrywsi: %d" % nscpentrywsicnt)

    # bind as bind_entry
    topology_m2.ms["supplier1"].log.info("##### Supplier1: Bind as %s #####" % BIND_DN)
    topology_m2.ms["supplier1"].simple_bind_s(BIND_DN, BIND_PW)

    topology_m2.ms["supplier1"].log.info("Supplier1: Calling search_ext...")
    msgid = topology_m2.ms["supplier1"].search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology_m2.ms["supplier1"].result2(msgid)
    topology_m2.ms["supplier1"].log.info("%d results" % len(rdata))

    for dn, attrs in rdata:
        if 'nscpentrywsi' in attrs:
            nscpentrywsicnt += 1
    assert nscpentrywsicnt == 0
    topology_m2.ms["supplier1"].log.info("Supplier1: count of nscpentrywsi: %d" % nscpentrywsicnt)

    # bind as bind_entry
    topology_m2.ms["supplier2"].log.info("##### Supplier2: Bind as %s #####" % BIND_DN)
    topology_m2.ms["supplier2"].simple_bind_s(BIND_DN, BIND_PW)

    topology_m2.ms["supplier2"].log.info("Supplier2: Calling search_ext...")
    msgid = topology_m2.ms["supplier2"].search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology_m2.ms["supplier2"].result2(msgid)
    topology_m2.ms["supplier2"].log.info("%d results" % len(rdata))

    for dn, attrs in rdata:
        if 'nscpentrywsi' in attrs:
            nscpentrywsicnt += 1
    assert nscpentrywsicnt == 0
    topology_m2.ms["supplier2"].log.info("Supplier2: count of nscpentrywsi: %d" % nscpentrywsicnt)

    # bind as anonymous
    topology_m2.ms["supplier1"].log.info("##### Supplier1: Bind as anonymous #####")
    topology_m2.ms["supplier1"].simple_bind_s("", "")

    topology_m2.ms["supplier1"].log.info("Supplier1: Calling search_ext...")
    msgid = topology_m2.ms["supplier1"].search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology_m2.ms["supplier1"].result2(msgid)
    topology_m2.ms["supplier1"].log.info("%d results" % len(rdata))

    for dn, attrs in rdata:
        if 'nscpentrywsi' in attrs:
            nscpentrywsicnt += 1
    assert nscpentrywsicnt == 0
    topology_m2.ms["supplier1"].log.info("Supplier1: count of nscpentrywsi: %d" % nscpentrywsicnt)

    # bind as bind_entry
    topology_m2.ms["supplier2"].log.info("##### Supplier2: Bind as anonymous #####")
    topology_m2.ms["supplier2"].simple_bind_s("", "")

    topology_m2.ms["supplier2"].log.info("Supplier2: Calling search_ext...")
    msgid = topology_m2.ms["supplier2"].search_ext(SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    nscpentrywsicnt = 0
    rtype, rdata, rmsgid = topology_m2.ms["supplier2"].result2(msgid)
    topology_m2.ms["supplier2"].log.info("%d results" % len(rdata))

    for dn, attrs in rdata:
        if 'nscpentrywsi' in attrs:
            nscpentrywsicnt += 1
    assert nscpentrywsicnt == 0
    topology_m2.ms["supplier2"].log.info("Supplier2: count of nscpentrywsi: %d" % nscpentrywsicnt)

    topology_m2.ms["supplier1"].log.info("##### ticket47869 was successfully verified. #####")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
