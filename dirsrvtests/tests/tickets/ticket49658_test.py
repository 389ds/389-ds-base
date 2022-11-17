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
import sys
print(sys.path)
from lib389 import Entry
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.topologies import topology_m3 as topo

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


MAX_EMPLOYEENUMBER_USER = 20
MAX_STANDARD_USER = 100
MAX_USER = MAX_STANDARD_USER + MAX_EMPLOYEENUMBER_USER
EMPLOYEENUMBER_RDN_START = 0

USER_UID='user_'
BASE_DISTINGUISHED = 'ou=distinguished,ou=people,%s' % (DEFAULT_SUFFIX)
BASE_REGULAR = 'ou=regular,ou=people,%s' % (DEFAULT_SUFFIX)

def _user_get_dn(no):
    uid = '%s%d' % (USER_UID, no)
    dn = 'uid=%s,%s' % (uid, BASE_REGULAR)
    return (uid, dn)

def add_user(server, no, init_val):
    (uid, dn) = _user_get_dn(no)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'organizationalPerson', 'inetOrgPerson'],
                             'uid': [uid],
                             'sn' : [uid],
                             'cn' : [uid],
                             'employeeNumber': init_val})))
    return dn

def _employeenumber_user_get_dn(no):
    employeeNumber = str(no)
    dn = 'employeeNumber=%s,%s' % (employeeNumber, BASE_DISTINGUISHED)
    return (employeeNumber, dn)

def add_employeenumber_user(server, no):
    (uid, dn) = _employeenumber_user_get_dn(EMPLOYEENUMBER_RDN_START + no)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'organizationalPerson', 'inetOrgPerson'],
                             'uid': [uid],
                             'sn' : [uid],
                             'cn' : [uid],
                             'employeeNumber': str(EMPLOYEENUMBER_RDN_START + no)})))
    return dn

def save_stuff():
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_11 = '11'.encode()
    value_1000 = '1000'.encode()
    value_13 = '13'.encode()
    value_14 = '14'.encode()

    # Step 2
    test_user_dn= add_user(M3, 0, value_11)
    log.info('Adding %s on M3' % test_user_dn)
    M3.modify_s(test_user_dn, [(ldap.MOD_DELETE, 'employeeNumber', value_11), (ldap.MOD_ADD, 'employeeNumber', value_1000)])
    ents = M3.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == 1


    # Step 3
    # Check the entry is replicated on M1
    for j in range(30):
            try:
                ent = M1.getEntry(test_user_dn, ldap.SCOPE_BASE,)
                if not ent.hasAttr('employeeNumber'):
                    # wait for the MOD
                    log.info('M1 waiting for employeeNumber')
                    time.sleep(1)
                    continue;
                break;
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                pass
    time.sleep(1)
    ents = M1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == 1

    # Check the entry is replicated on M2
    for j in range(30):
            try:
                ent = M2.getEntry(test_user_dn, ldap.SCOPE_BASE,)
                if not ent.hasAttr('employeeNumber'):
                    # wait for the MOD
                    log.info('M2 waiting for employeeNumber')
                    time.sleep(1)
                    continue;

                break;
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                pass
    time.sleep(1)
    ents = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == 1

def test_ticket49658_init(topo):
    """Specify a test case purpose or name here

    :id: f8d43cef-c385-46a2-b32b-fdde2114b45e
    :setup: 3 Supplier Instances
    :steps:
        1. Create 3 suppliers
        2. Create on M3 MAX_USER test entries having a single-value attribute employeeNumber=11
            and update it MOD_DEL 11 + MOD_ADD 1000
        3. Check they are replicated on M1 and M2
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_11 = '11'.encode()
    value_1000 = '1000'.encode()

    # Step 2
    M3.add_s(Entry((BASE_DISTINGUISHED, {'objectclass': ['top', 'organizationalUnit'],
                             'ou': ['distinguished']})))
    for i in range(MAX_EMPLOYEENUMBER_USER):
        test_user_dn= add_employeenumber_user(M3, i)
        log.info('Adding %s on M3' % test_user_dn)
        ents = M3.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
        assert len(ents) == (i + 1)

    M3.add_s(Entry((BASE_REGULAR, {'objectclass': ['top', 'organizationalUnit'],
                             'ou': ['regular']})))
    for i in range(MAX_STANDARD_USER):
        test_user_dn= add_user(M3, i, value_11)
        log.info('Adding %s on M3' % test_user_dn)
        M3.modify_s(test_user_dn, [(ldap.MOD_DELETE, 'employeeNumber', value_11), (ldap.MOD_ADD, 'employeeNumber', value_1000)])
        ents = M3.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
        assert len(ents) == (MAX_EMPLOYEENUMBER_USER + i + 1)


    # Step 3
    # Check the last entry is replicated on M1
    (uid, test_user_dn) = _user_get_dn(MAX_STANDARD_USER - 1)
    for j in range(30):
            try:
                ent = M1.getEntry(test_user_dn, ldap.SCOPE_BASE,)
                if not ent.hasAttr('employeeNumber'):
                    # wait for the MOD
                    log.info('M1 waiting for employeeNumber')
                    time.sleep(1)
                    continue;
                break;
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                pass
    time.sleep(1)
    ents = M1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_USER

    # Check the last entry is replicated on M2
    for j in range(30):
            try:
                ent = M2.getEntry(test_user_dn, ldap.SCOPE_BASE,)
                if not ent.hasAttr('employeeNumber'):
                    # wait for the MOD
                    log.info('M2 waiting for employeeNumber')
                    time.sleep(1)
                    continue;

                break;
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                pass
    time.sleep(1)
    ents = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_USER

def test_ticket49658_0(topo):
    """Do MOD(DEL+ADD) and replicate MOST RECENT first
        M1: MOD(DEL+ADD)      -> V1
        M2: MOD(DEL+ADD)      -> V1
        expected: V1

    :id: 5360b304-9b33-4d37-935f-ab73e0baa1aa
    :setup: 3 Supplier Instances
        1. using user_0 where employNumber=1000
    :steps:
        1. Create 3 suppliers
        2. Isolate M1 and M2 by pausing the replication agreements
        3. On M1 do MOD_DEL 1000 + MOD_ADD_13
        4. On M2 do MOD_DEL 1000 + MOD_ADD_13
        5. Enable replication agreement M2 -> M3, so that update step 6 is replicated first
        6. Enable replication agreement M1 -> M3, so that update step 5 is replicated second
        7. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'.encode()
    last = '0'
    value_end = last.encode()
    theFilter = '(employeeNumber=%s)' % last
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 2
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 3
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    M1.modify_s(test_user_dn, [(ldap.MOD_DELETE, 'employeeNumber', value_1000), (ldap.MOD_ADD, 'employeeNumber', value_end)])
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1
    time.sleep(1)

    # Step 4
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    M2.modify_s(test_user_dn, [(ldap.MOD_DELETE, 'employeeNumber', value_1000), (ldap.MOD_ADD, 'employeeNumber', value_end)])
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

def test_ticket49658_1(topo):
    """Do MOD(DEL+ADD) and replicate OLDEST first
        M2: MOD(DEL+ADD)      -> V1
        M1: MOD(DEL+ADD)      -> V1
        expected: V1

    :id: bc6620d9-eae1-48af-8a4f-bc14405ea6b6
    :setup: 3 Supplier Instances
        1. using user_1 where employNumber=1000
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M2 do MOD_DEL 1000 + MOD_ADD_13
        3. On M1 do MOD_DEL 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'.encode()
    last = '1'
    value_end = last.encode()
    theFilter = '(employeeNumber=%s)' % last

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(1))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M2 contains employeeNumber=<value_end>
    M2.modify_s(test_user_dn, [(ldap.MOD_DELETE, 'employeeNumber', value_1000), (ldap.MOD_ADD, 'employeeNumber', value_end)])
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    M1.modify_s(test_user_dn, [(ldap.MOD_DELETE, 'employeeNumber', value_1000), (ldap.MOD_ADD, 'employeeNumber', value_end)])
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

def test_ticket49658_2(topo):
    """Do MOD(ADD+DEL) and replicate OLDEST first
        M2: MOD(ADD+DEL)      -> V1
        M1: MOD(ADD+DEL)      -> V1
        expected: V1

    :id: 672ff689-5b76-4107-92be-fb95d08400b3
    :setup: 3 Supplier Instances
        1. using user_2 where employNumber=1000
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M2 do MOD_DEL 1000 + MOD_ADD_13
        3. On M1 do MOD_DEL 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'.encode()
    last = '2'
    value_end = last.encode()
    theFilter = '(employeeNumber=%s)' % last

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M2 contains employeeNumber=<value_end>
    M2.modify_s(test_user_dn, [(ldap.MOD_ADD, 'employeeNumber', value_end),(ldap.MOD_DELETE, 'employeeNumber', value_1000)])
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    M1.modify_s(test_user_dn, [(ldap.MOD_ADD, 'employeeNumber', value_end), (ldap.MOD_DELETE, 'employeeNumber', value_1000)])
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

def test_ticket49658_3(topo):
    """Do MOD(ADD+DEL) and replicate MOST RECENT first
        M1: MOD(ADD+DEL)      -> V1
        M2: MOD(ADD+DEL)      -> V1
        expected: V1
    :id: b25e508a-8bf2-4351-88f6-3b6c098ccc44
    :setup: 3 Supplier Instances
        1. using user_2 where employNumber=1000
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do MOD_DEL 1000 + MOD_ADD_13
        3. On M2 do MOD_DEL 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'.encode()
    last = '3'
    value_end = last.encode()
    theFilter = '(employeeNumber=%s)' % last

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    M1.modify_s(test_user_dn, [(ldap.MOD_ADD, 'employeeNumber', value_end),(ldap.MOD_DELETE, 'employeeNumber', value_1000)])
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    M2.modify_s(test_user_dn, [(ldap.MOD_ADD, 'employeeNumber', value_end), (ldap.MOD_DELETE, 'employeeNumber', value_1000)])
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

def test_ticket49658_4(topo):
    """Do MOD(ADD+DEL) MOD(REPL) and replicate MOST RECENT first
        M1: MOD(ADD+DEL)      -> V1
        M2: MOD(REPL)      -> V1
        expected: V1

    :id: 8f7ce9ff-e36f-48cd-b0ed-b7077a3e7341
    :setup: 3 Supplier Instances
        1. using user_2 where employNumber=1000
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do MOD_DEL 1000 + MOD_ADD_13
        3. On M2 do MOD_REPL _13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'.encode()
    last = '4'
    value_end = last.encode()
    theFilter = '(employeeNumber=%s)' % last

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    M1.modify_s(test_user_dn, [(ldap.MOD_ADD, 'employeeNumber', value_end),(ldap.MOD_DELETE, 'employeeNumber', value_1000)])
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    M2.modify_s(test_user_dn, [(ldap.MOD_REPLACE, 'employeeNumber', value_end)])
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

def test_ticket49658_5(topo):
    """Do MOD(REPL) MOD(ADD+DEL) and replicate MOST RECENT first
        M1: MOD(REPL)         -> V1
        M2: MOD(ADD+DEL)      -> V1
        expected: V1
    :id: d6b88e3c-a509-4d3e-8e5d-849237993f47
    :setup: 3 Supplier Instances
        1. using user_2 where employNumber=1000
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do MOD_DEL 1000 + MOD_ADD_13
        3. On M2 do MOD_REPL _13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'.encode()
    last = '5'
    value_end = last.encode()
    theFilter = '(employeeNumber=%s)' % last

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    M1.modify_s(test_user_dn, [(ldap.MOD_REPLACE, 'employeeNumber', value_end)])
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    M2.modify_s(test_user_dn, [(ldap.MOD_ADD, 'employeeNumber', value_end),(ldap.MOD_DELETE, 'employeeNumber', value_1000)])
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, theFilter)
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), value_end))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == value_end

def test_ticket49658_6(topo):
    """Do
        M1: MOD(REPL)    -> V1
        M2: MOD(ADD+DEL) -> V2
        expected: V2

    :id: 5eb67db1-2ff2-4c17-85af-e124b45aace3
    :setup: 3 Supplier Instances
        1. using user_2 where employNumber=1000
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do MOD_DEL 1000 + MOD_ADD_13
        3. On M2 do MOD_REPL _13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'
    last = '6'
    value_S1 = '6.1'
    value_S2 = '6.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_ADD, 'employeeNumber', value_S2.encode()),(ldap.MOD_DELETE, 'employeeNumber', value_1000.encode())],
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].modify_s(test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].modify_s(test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_7(topo):
    """Do
        M1: MOD(ADD+DEL)    -> V1
        M2: MOD(REPL)       -> V2
        expected: V2

    :id: a79036ca-0e1b-453e-9524-fb44e1d7c929
    :setup: 3 Supplier Instances
    :steps:
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'
    last = '7'
    value_S1 = '7.1'
    value_S2 = '7.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MOD": [(ldap.MOD_ADD, 'employeeNumber', value_S1.encode()),(ldap.MOD_DELETE, 'employeeNumber', value_1000.encode())],
    "S2_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S2.encode())],
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].modify_s(test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].modify_s(test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_8(topo):
    """Do
        M1: MOD(DEL+ADD)    -> V1
        M2: MOD(REPL)       -> V2
        expected: V2

    :id: 06acb988-b735-424a-9886-b0557ee12a9a
    :setup: 3 Supplier Instances
    :steps:
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'
    last = '8'
    value_S1 = '8.1'
    value_S2 = '8.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_1000.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S2.encode())],
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].modify_s(test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].modify_s(test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()


def test_ticket49658_9(topo):
    """Do
        M1: MOD(REPL)         -> V1
        M2: MOD(DEL+ADD)      -> V2
        expected: V2

    :id: 3a4c1be3-e3b9-44fe-aa5a-72a3b1a8985c
    :setup: 3 Supplier Instances
    :steps:
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'
    last = '9'
    value_S1 = '9.1'
    value_S2 = '9.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_1000.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S2.encode())],
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].modify_s(test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].modify_s(test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()



def test_ticket49658_10(topo):
    """Do
        M1: MOD(REPL)         -> V1
        M2: MOD(REPL)         -> V2
        expected: V2

    :id: 1413341a-45e6-422a-b6cc-9fde6fc9bb15
    :setup: 3 Supplier Instances
    :steps:
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'
    last = '10'
    value_S1 = '10.1'
    value_S2 = '10.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S2.encode())],
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].modify_s(test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].modify_s(test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()



def test_ticket49658_11(topo):
    """Do
        M2: MOD(REPL)         -> V2
        M1: MOD(REPL)         -> V1
        expected: V1

    :id: a2810403-418b-41d7-948c-6f8ca46e2f29
    :setup: 3 Supplier Instances
    :steps:
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'
    last = '11'
    value_S1 = '11.1'
    value_S2 = '11.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S2.encode())],
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S2"].modify_s(test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S1"].modify_s(test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_12(topo):
    """Do
        M2: MOD(ADD+DEL) -> V2
        M1: MOD(REPL)    -> V1
        expected: V1

    :id: daba6f3c-e060-4d3f-8f9c-25ea4c1bca48
    :setup: 3 Supplier Instances
        1. using user_2 where employNumber=1000
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do MOD_DEL 1000 + MOD_ADD_13
        3. On M2 do MOD_REPL _13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'
    last = '12'
    value_S1 = '12.1'
    value_S2 = '12.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_ADD, 'employeeNumber', value_S2.encode()),(ldap.MOD_DELETE, 'employeeNumber', value_1000.encode())],
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S2"].modify_s(test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S1"].modify_s(test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_13(topo):
    """Do
        M2: MOD(DEL+ADD) -> V2
        M1: MOD(REPL)    -> V1
        expected: V1

    :id: 50006b1f-d17c-47a1-86a5-4d78b2a6eab1
    :setup: 3 Supplier Instances
        1. using user_2 where employNumber=1000
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do MOD_DEL 1000 + MOD_ADD_13
        3. On M2 do MOD_REPL _13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'
    last = '13'
    value_S1 = '13.1'
    value_S2 = '13.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_1000.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S2.encode())],
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S2"].modify_s(test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S1"].modify_s(test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()


def test_ticket49658_14(topo):
    """Do
        M2: MOD(DEL+ADD)    -> V2
        M1: MOD(DEL+ADD)    -> V1
        expected: V1

    :id: d45c58f1-c95e-4314-9cdd-53a2dd391218
    :setup: 3 Supplier Instances
        1. using user_2 where employNumber=1000
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do MOD_DEL 1000 + MOD_ADD_13
        3. On M2 do MOD_REPL _13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'
    last = '14'
    value_S1 = '14.1'
    value_S2 = '14.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_1000.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_1000.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S2.encode())],
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S2"].modify_s(test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S1"].modify_s(test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_15(topo):
    """Do
        M2: MOD(ADD+DEL)    -> V2
        M1: MOD(DEL+ADD)    -> V1
        expected: V1

    :id: e077f312-e0af-497a-8a31-3395873512d8
    :setup: 3 Supplier Instances
        1. using user_2 where employNumber=1000
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do MOD_DEL 1000 + MOD_ADD_13
        3. On M2 do MOD_REPL _13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_1000 = '1000'
    last = '15'
    value_S1 = '15.1'
    value_S2 = '15.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_1000.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_ADD, 'employeeNumber', value_S2.encode()),(ldap.MOD_DELETE, 'employeeNumber', value_1000.encode())],
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S2"].modify_s(test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S1"].modify_s(test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1

    #time.sleep(60)
    # Step 7
    # Renable M2 before M1 so that on M3, the most recent update is replicated before
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    # Step 8
    # Renable M1 so that on M3 oldest update is now replicated
    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_STANDARD_USER
    ents = M3.search_s(BASE_REGULAR, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()


def _resume_ra_M1_then_M2(M1, M2, M3):
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)

    time.sleep(4)
    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)
    time.sleep(4)

def _resume_ra_M2_then_M1(M1, M2, M3):
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    for ra in agreement_m2_m1, agreement_m2_m3:
        M2.agreement.resume(ra[0].dn)

    time.sleep(4)
    for ra in agreement_m1_m2, agreement_m1_m3:
        M1.agreement.resume(ra[0].dn)
    time.sleep(4)


def test_ticket49658_16(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V1
        expected: V1
        resume order: M2, M1

    :id: 131b4e4c-0a6d-45df-88aa-cb26a1cd6fa6
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '1'
    last = '1'
    value_S1 = '1.1'
    value_S2 = value_S1

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S2_MODRDN": value_S2,
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1

    _resume_ra_M2_then_M1(M1, M2, M3)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()


def test_ticket49658_17(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        expected: V2
        resume order: M2 then M1

    :id: 1d3423ec-a2f3-4c03-9765-ec0924f03cb2
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '2'
    last = '2'
    value_S1 = '2.1'
    value_S2 = '2.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S2_MODRDN": value_S2,
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1

    _resume_ra_M2_then_M1(M1, M2, M3)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_18(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        expected: V2
        resume order: M1 then M2

    :id: c50ea634-ba35-4943-833b-0524a446214f
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '2'
    last = '3'
    value_S1 = '3.1'
    value_S2 = '3.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S2_MODRDN": value_S2,
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1

    _resume_ra_M1_then_M2(M1, M2, M3)

    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_19(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(REPL) -> V1
        Replicate order: M2 then M1
        expected: V1

    :id: 787db943-fc95-4fbb-b066-5e8895cfd296
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '3'
    last = '4'
    value_S1 = '4.1'
    value_S2 = '4.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1

    _resume_ra_M2_then_M1(M1, M2, M3)


    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_20(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(REPL) -> V1
        Replicate order: M1 then M2
        expected: V1

    :id: a3df2f72-b8b1-4bb8-b0ca-ebd306539c8b
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '3'
    last = '5'
    value_S1 = '5.1'
    value_S2 = '5.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1

    _resume_ra_M1_then_M2(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_21(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(DEL/ADD) -> V1
        Replicate order: M2 then M1
        expected: V1

    :id: f338188c-6877-4a2e-bbb1-14b81ac7668a
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '3'
    last = '6'
    value_S1 = '6.1'
    value_S2 = '6.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_S1.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S1.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1

    _resume_ra_M2_then_M1(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_22(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(DEL/ADD) -> V1
        Replicate: M1 then M2
        expected: V1

    :id: f3b33f52-d5c7-4b49-89cf-3cbe4b060674
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '3'
    last = '7'
    value_S1 = '7.1'
    value_S2 = '7.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_S1.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S1.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1

    _resume_ra_M1_then_M2(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_23(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(REPL) -> V1
        M2: MOD(REPL) -> V2
        Replicate order: M2 then M1
        expected: V2

    :id: 2c550174-33a0-4666-8abf-f3362e19ae29
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '8'
    value_S1 = '8.1'
    value_S2 = '8.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S2.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN"])
    description["S2"].modify_s(new_test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    _resume_ra_M2_then_M1(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_24(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(REPL) -> V1
        M2: MOD(REPL) -> V2
        Replicate order: M1 then M2
        expected: V2

    :id: af6a472c-29e3-4833-a5dc-d96c684d33f9
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '9'
    value_S1 = '9.1'
    value_S2 = '9.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S2.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN"])
    description["S2"].modify_s(new_test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    _resume_ra_M1_then_M2(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_25(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(REPL) -> V1
        M2: MOD(DEL/ADD) -> V2
        Replicate order: M1 then M2
        expected: V2

    :id: df2cba7c-7afa-44b3-b1df-261e8bf0c9b4
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '10'
    value_S1 = '10.1'
    value_S2 = '10.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_S2.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S2.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN"])
    description["S2"].modify_s(new_test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    _resume_ra_M1_then_M2(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_26(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(REPL) -> V1
        M2: MOD(DEL/ADD) -> V2
        Replicate order: M2 then M1
        expected: V2

    :id: 8e9f85d3-22cc-4a84-a828-cec29202821f
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '11'
    value_S1 = '11.1'
    value_S2 = '11.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_S2.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S2.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN"])
    description["S2"].modify_s(new_test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    _resume_ra_M2_then_M1(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_27(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(DEL/ADD) -> V1
        M2: MOD(REPL) -> V2
        Replicate order: M1 then M2
        expected: V2

    :id: d85bd9ef-b257-4027-a29c-dfba87c0bf51
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '12'
    value_S1 = '12.1'
    value_S2 = '12.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_S1.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S2.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN"])
    description["S2"].modify_s(new_test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    _resume_ra_M1_then_M2(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_28(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(DEL/ADD) -> V1
        M2: MOD(REPL) -> V2
        Replicate order: M2 then M1
        expected: V2

    :id: 286cd17e-225e-490f-83c9-20618b9407a9
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '13'
    value_S1 = '13.1'
    value_S2 = '13.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_S1.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S2.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN"])
    description["S2"].modify_s(new_test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    _resume_ra_M2_then_M1(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()


def test_ticket49658_29(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(DEL/ADD) -> V1
        M2: MOD(DEL/ADD) -> V2
        Replicate order: M1 then M2
        expected: V2

    :id: b81f3885-7965-48fe-8dbf-692d1150d061
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '14'
    value_S1 = '14.1'
    value_S2 = '14.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_S1.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_S2.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S2.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN"])
    description["S2"].modify_s(new_test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    _resume_ra_M1_then_M2(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_30(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(DEL/ADD) -> V1
        M2: MOD(DEL/ADD) -> V2
        Replicate order: M2 then M1
        expected: V2

    :id: 4dce88f8-31db-488b-aeb4-fce4173e3f12
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '15'
    value_S1 = '15.1'
    value_S2 = '15.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_S1.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_DELETE, 'employeeNumber', value_S2.encode()),(ldap.MOD_ADD, 'employeeNumber', value_S2.encode())],
    "S2_MODRDN": value_S2,
    "expected": value_S2}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN"])
    description["S2"].modify_s(new_test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    _resume_ra_M2_then_M1(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_31(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(REPL) -> V1
        M2: MOD(REPL) -> V2
        M2: MODRDN    -> V1
        Replicate order: M2 then M1
        expected: V1

    :id: 2791a3df-25a2-4e6e-a5e9-514d76af43fb
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '16'
    value_S1 = '16.1'
    value_S2 = '16.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S2.encode())],
    "S2_MODRDN_1": value_S2,
    "S2_MODRDN_2": value_S1,
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN_1"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN_1"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)


    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN_1"])
    description["S2"].modify_s(new_test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    description["S2"].rename_s(new_test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN_2"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN_2"])
    assert len(ents) == 1
    time.sleep(1)

    _resume_ra_M2_then_M1(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()


def test_ticket49658_32(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(REPL) -> V1
        M2: MOD(REPL) -> V2
        M2: MODRDN    -> V1
        Replicate order: M1 then M2
        expected: V1

    :id: 6af57e2e-a325-474a-9c9d-f07cd2244657
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '17'
    value_S1 = '17.1'
    value_S2 = '17.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S2.encode())],
    "S2_MODRDN_1": value_S2,
    "S2_MODRDN_2": value_S1,
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN_1"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN_1"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)


    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN_1"])
    description["S2"].modify_s(new_test_user_dn, description["S2_MOD"])
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S2)
    assert len(ents) == 1

    description["S2"].rename_s(new_test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN_2"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN_2"])
    assert len(ents) == 1
    time.sleep(1)

    _resume_ra_M1_then_M2(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_33(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(REPL) -> V1
        M2: MODRDN    -> V1
        Replicate order: M2 then M1
        expected: V1

    :id: 81100b04-d3b6-47df-90eb-d96ef14a3722
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '18'
    value_S1 = '18.1'
    value_S2 = '18.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MODRDN_1": value_S2,
    "S2_MODRDN_2": value_S1,
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN_1"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN_1"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN_1"])
    description["S2"].rename_s(new_test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN_2"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN_2"])
    assert len(ents) == 1
    time.sleep(1)

    _resume_ra_M2_then_M1(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

def test_ticket49658_34(topo):
    """Do
        M1: MODRDN    -> V1
        M2: MODRDN    -> V2
        M1: MOD(REPL) -> V1
        M2: MODRDN    -> V1
        Replicate order: M1 then M2
        expected: V1

    :id: 796d3d77-2401-49f5-89fa-80b231d3e758
    :setup: 3 Supplier Instances
        1. Use employeenumber=1000,ou=distinguished,ou=people,<suffix>
    :steps:
        1. Isolate M1 and M2 by pausing the replication agreements
        2. On M1 do DEL+ADD 1000 + MOD_ADD_13
        3. On M2 do DEL+ADD 1000 + MOD_ADD_13
        4. Enable replication agreement M2 -> M3, so that update step 2 is replicated first
        5. Enable replication agreement M1 -> M3, so that update step 3 is replicated second
        6. Check that the employeeNumber is 13 on all servers
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)



    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    value_init = '7'
    last = '19'
    value_S1 = '19.1'
    value_S2 = '19.2'

    description = {
    "S1": M1,
    "S2": M2,
    "S1_MODRDN": value_S1,
    "S1_MOD": [(ldap.MOD_REPLACE, 'employeeNumber', value_S1.encode())],
    "S2_MODRDN_1": value_S2,
    "S2_MODRDN_2": value_S1,
    "expected": value_S1}

    # This test takes the user_1
    (uid, test_user_dn) = _employeenumber_user_get_dn(int(last))

    #
    # Step 4
    #
    # disable all RA from M1 and M2
    # only M3 can replicate the update
    #
    agreement_m1_m2 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    agreement_m1_m3 = M1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    agreement_m2_m1 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M1.host, consumer_port=M1.port)
    agreement_m2_m3 = M2.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    M1.agreement.pause(agreement_m1_m2[0].dn)
    M1.agreement.pause(agreement_m1_m3[0].dn)
    M2.agreement.pause(agreement_m2_m1[0].dn)
    M2.agreement.pause(agreement_m2_m3[0].dn)

    # Step 5
    # Oldest update
    # check that the entry on M1 contains employeeNumber=<value_end>
    description["S1"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S1_MODRDN"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S1_MODRDN"])
    assert len(ents) == 1
    time.sleep(1)

    # Step 6
    # More recent update
    # check that the entry on M2 contains employeeNumber=<value_end>
    description["S2"].rename_s(test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN_1"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN_1"])
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S1_MODRDN"])
    description["S1"].modify_s(new_test_user_dn, description["S1_MOD"])
    ents = description["S1"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % value_S1)
    assert len(ents) == 1
    time.sleep(1)

    (no, new_test_user_dn) = _employeenumber_user_get_dn(description["S2_MODRDN_1"])
    description["S2"].rename_s(new_test_user_dn, 'employeeNumber=%s' % description["S2_MODRDN_2"], newsuperior=BASE_DISTINGUISHED, delold=1)
    ents = description["S2"].search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["S2_MODRDN_2"])
    assert len(ents) == 1
    time.sleep(1)

    _resume_ra_M1_then_M2(M1, M2, M3)

    #time.sleep(3600)
    # Step 9
    # Check that M1 still contains employeeNumber=<value_end>
    ents = M1.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M1 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M2 still contains employeeNumber=<value_end>
    ents = M2.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M2 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()

    # Check that M3 still contain employeeNumber and it contains employeeNumber=<value_end>
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=*)')
    assert len(ents) == MAX_EMPLOYEENUMBER_USER
    ents = M3.search_s(BASE_DISTINGUISHED, ldap.SCOPE_SUBTREE, '(employeeNumber=%s)' % description["expected"])
    log.info('Search M3 employeeNumber=%s (vs. %s)' % (ents[0].getValue('employeeNumber'), description["expected"]))
    assert len(ents) == 1
    assert ents[0].hasAttr('employeeNumber') and ents[0].getValue('employeeNumber') == description["expected"].encode()
if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

