# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 RED Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

import pytest, os, re, time
from lib389.tasks import *
from lib389.utils import *
from lib389 import Entry
from ldap import SCOPE_SUBTREE, ALREADY_EXISTS
from ldap.controls import SimplePagedResultsControl
from ldap.controls.sessiontrack import SessionTrackingControl, SESSION_TRACKING_CONTROL_OID
from lib389.topologies import topology_m2 as topo_m2
from  ldap.extop import ExtendedRequest

from lib389._constants import DEFAULT_SUFFIX, PW_DM, PLUGIN_MEMBER_OF
from lib389.topologies import topology_st
from lib389.plugins import MemberOfPlugin

from lib389.schema import Schema
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.account import Accounts
from lib389.idm.account import Anonymous

SESSION_SOURCE_IP = '10.0.0.10'
SESSION_SOURCE_NAME = 'host.example.com'
SESSION_TRACKING_FORMAT_OID = SESSION_TRACKING_CONTROL_OID + ".1234"

pytestmark = pytest.mark.tier0

def test_short_session_tracking_srch(topology_st, request):
    """Verify that a short session_tracking string
    is added (not truncate) during a search

    :id: c9efc1cc-03c7-42b7-801c-440f7a11ee13
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Do a search with a short session tracking string
        2. Restart the instance to flush the log
        3. Check the exact same string is present in the access log
    :expectedresults:
        1. Search should succeed
        2. success
        3. Log should contain one log with that session
    """


    SESSION_TRACKING_IDENTIFIER = "SRCH short"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    topology_st.standalone.search_ext_s(DEFAULT_SUFFIX,
                                        ldap.SCOPE_SUBTREE,
                                        '(uid=*)',
                                        serverctrls=[st_ctrl])
    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=101.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        pass

    request.addfinalizer(fin)

def test_short_session_tracking_add(topology_st, request):
    """Verify that a short session_tracking string
    is added (not truncate) during a add

    :id: 04afd3de-365e-485f-9e00-913d913af931
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry with a short session tracking
        2. Restart the instance to flush the log
        3. Check the exact same string is present in the access log
    :expectedresults:
        1. Add should succeed
        2. success
        3. Log should contain one log with that session
    """


    SESSION_TRACKING_IDENTIFIER = "ADD short"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_add," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_add'),
                                                    ('cn', b'test_add'),
                                                    ('userPassword', b'test_add'),
                                               ],
                                               serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_short_session_tracking_del(topology_st, request):
    """Verify that a short session_tracking string
    is added (not truncate) during a del

    :id: a1391fbc-2107-4474-aaaf-088c393767a6
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. Delete the test entry with a short session tracking
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the DEL
    :expectedresults:
        1. Add should succeed
        2. DEL should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the DEL
    """


    SESSION_TRACKING_IDENTIFIER = "DEL short"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_del," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_del'),
                                                    ('cn', b'test_del'),
                                                    ('userPassword', b'test_del'),
                                               ])
        topology_st.standalone.delete_ext_s(TEST_DN,
                                            serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=107.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_short_session_tracking_mod(topology_st, request):
    """Verify that a short session_tracking string
    is added (not truncate) during a MOD

    :id: 00c91efc-071d-4187-8185-6cca27b5bf63
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. Modify the test entry with a short session tracking
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the MOD
    :expectedresults:
        1. Add should succeed
        2. Mod should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the MOD
    """


    SESSION_TRACKING_IDENTIFIER = "MOD short"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_mod," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_del'),
                                                    ('cn', b'test_del'),
                                                    ('userPassword', b'test_del'),
                                               ])
        topology_st.standalone.modify_ext_s(TEST_DN,
                                            [(ldap.MOD_REPLACE, 'sn', b'new_sn')],
                                            serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=103.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_short_session_tracking_compare(topology_st, request):
    """Verify that a short session_tracking string
    is added (not truncate) during a compare

    :id: 6f2090fd-a960-48e5-b7f1-04ddef4a85af
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. compare an attribute with a short session tracking
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the COMPARE
    :expectedresults:
        1. Add should succeed
        2. Compare should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the COMPARE
    """


    SESSION_TRACKING_IDENTIFIER = "COMPARE short"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_compare," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_compare'),
                                                    ('cn', b'test_compare'),
                                                    ('userPassword', b'test_compare'),
                                               ])
        topology_st.standalone.compare_ext_s(TEST_DN, 'sn', b'test_compare', serverctrls=[st_ctrl])
        topology_st.standalone.compare_ext_s(TEST_DN, 'sn', b'test_fail_compare', serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*err=6 tag=111.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    access_log_lines = topology_st.standalone.ds_access_log.match('.*err=5 tag=111.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_short_session_tracking_abandon(topology_st, request):
    """Verify that a short session_tracking string
    is added (not truncate) during an abandon

    :id: 58f54ada-e05c-411b-a1c6-8b19fd99843c
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add 10 test entries
        2. Launch Page Search with a window of 3
        3. Abandon the Page Search with a short session tracking
        4. Restart the instance to flush the log
        5. Check the exact same string is not present in the access log for the ADD
        6. Check the exact same string is present in the access log for the ABANDON
    :expectedresults:
        1. Add should succeed
        2. success
        3. success
        4. success
        5. Log should not contain log with that session for the ADDs
        6. Log should contain one log with that session for the abandon
    """

    SESSION_TRACKING_IDENTIFIER = "ABANDON short"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    # provision more entries than the page search will fetch
    entries = []
    for i in range(10):
        TEST_DN = "cn=test_abandon_%d,%s" % (i, DEFAULT_SUFFIX)
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_abandon'),
                                                    ('cn', b'test_abandon_%d' % i),
                                                    ('userPassword', b'test_abandon'),
                                               ])
        entries.append(TEST_DN)

    # run a page search (with the session) using a small window. So we can abandon it.
    req_ctrl = SimplePagedResultsControl(True, size=3, cookie='')
    msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, r'(objectclass=*)', ['cn'], serverctrls=[req_ctrl])
    time.sleep(1)
    topology_st.standalone.abandon_ext(msgid, serverctrls=[st_ctrl])


    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*ABANDON.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        for ent in entries:
            try:
                topology_st.standalone.delete_s(ent)
            except:
                pass

    request.addfinalizer(fin)

def test_short_session_tracking_extop(topology_st, request):
    """Verify that a short session_tracking string
    is added (not truncate) during an extended operation

    :id: 65c2d014-d798-46f3-8168-b6f56b43d069
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. run whoami extop
        2. Check the exact same string is present in the access log for the EXTOP
    :expectedresults:
        1. success
        2. Log should contain one log with that session for the EXTOP
    """
    SESSION_TRACKING_IDENTIFIER = "Extop short"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    extop = ExtendedRequest(requestName = '1.3.6.1.4.1.4203.1.11.3', requestValue=None)
    (oid_response, res) = topology_st.standalone.extop_s(extop, serverctrls=[st_ctrl])

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=120.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        pass

    request.addfinalizer(fin)

def test_exact_max_lgth_session_tracking_srch(topology_st, request):
    """Verify that a exact max length session_tracking string
    is added (not truncate) during a search

    :id: 2c8c86f9-4896-4ccc-a727-6a4033f6f44a
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Do a search with a exact max length session tracking string
        2. Restart the instance to flush the log
        3. Check the exact same string is present in the access log (without '.')
    :expectedresults:
        1. Search should succeed
        2. success
        3. Log should contain one log with that session
    """


    SESSION_TRACKING_IDENTIFIER = "SRCH long ---->"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    topology_st.standalone.search_ext_s(DEFAULT_SUFFIX,
                                        ldap.SCOPE_SUBTREE,
                                        '(uid=*)',
                                        serverctrls=[st_ctrl])
    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=101.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        pass

    request.addfinalizer(fin)

def test_exact_max_lgth_session_tracking_add(topology_st, request):
    """Verify that a exact max length of session_tracking string
    is added (not truncate) during a add

    :id: 41c0b4f3-5e75-404b-98af-98cc98b742c7
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry with a exact max lenght (15) session tracking
        2. Restart the instance to flush the log
        3. Check the exact same string is present in the access log
    :expectedresults:
        1. Add should succeed
        2. success
        3. Log should contain one log with that session
    """


    SESSION_TRACKING_IDENTIFIER = "ADD long ----->"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_add," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_add'),
                                                    ('cn', b'test_add'),
                                                    ('userPassword', b'test_add'),
                                               ],
                                               serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_exact_max_lgth_session_tracking_del(topology_st, request):
    """Verify that a exact max lgth session_tracking string
    is added (not truncate) during a del

    :id: b8dca6c9-7cd4-4950-bcb5-7e9e6bb9202f
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. Delete the test entry with a exact max length (15) session tracking
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the DEL
    :expectedresults:
        1. Add should succeed
        2. DEL should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the DEL
    """


    SESSION_TRACKING_IDENTIFIER = "DEL long ----->"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_del," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_del'),
                                                    ('cn', b'test_del'),
                                                    ('userPassword', b'test_del'),
                                               ])
        topology_st.standalone.delete_ext_s(TEST_DN,
                                            serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=107.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_exact_max_lgth_session_tracking_mod(topology_st, request):
    """Verify that an exact max length session_tracking string
    is added (not truncate) during a MOD

    :id: 3bd1205f-a035-48a7-94c2-f8774e24ae91
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. Modify the test entry with an exact max length (15) session tracking
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the MOD
    :expectedresults:
        1. Add should succeed
        2. Mod should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the MOD
    """


    SESSION_TRACKING_IDENTIFIER = "MOD long ----->"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_mod," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_del'),
                                                    ('cn', b'test_del'),
                                                    ('userPassword', b'test_del'),
                                               ])
        topology_st.standalone.modify_ext_s(TEST_DN,
                                            [(ldap.MOD_REPLACE, 'sn', b'new_sn')],
                                            serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=103.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_exact_max_lgth_session_tracking_compare(topology_st, request):
    """Verify that an exact max length session_tracking string
    is added (not truncate) during a compare

    :id: a6c8ad60-7edb-4ee4-b0e3-06727870687c
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. compare an attribute with an exact max length session tracking
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the COMPARE
    :expectedresults:
        1. Add should succeed
        2. Compare should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the COMPARE
    """


    SESSION_TRACKING_IDENTIFIER = "COMPARE long ->"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_compare," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_compare'),
                                                    ('cn', b'test_compare'),
                                                    ('userPassword', b'test_compare'),
                                               ])
        topology_st.standalone.compare_ext_s(TEST_DN, 'sn', b'test_compare', serverctrls=[st_ctrl])
        topology_st.standalone.compare_ext_s(TEST_DN, 'sn', b'test_fail_compare', serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*err=6 tag=111.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    access_log_lines = topology_st.standalone.ds_access_log.match('.*err=5 tag=111.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_exact_max_lgth_session_tracking_abandon(topology_st, request):
    """Verify that an exact max length session_tracking string
    is added (not truncate) during an abandon

    :id: 708554b9-8403-411c-90b5-d9ecc2d3830f
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add 10 test entries
        2. Launch Page Search with a window of 3
        3. Abandon the Page Search with an exact max length session tracking
        4. Restart the instance to flush the log
        5. Check the exact same string is not present in the access log for the ADD
        6. Check the exact same string is present in the access log for the ABANDON
    :expectedresults:
        1. Add should succeed
        2. success
        3. success
        4. success
        5. Log should not contain log with that session for the ADDs
        6. Log should contain one log with that session for the abandon
    """

    SESSION_TRACKING_IDENTIFIER = "ABANDON long ->"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    # provision more entries than the page search will fetch
    entries = []
    for i in range(10):
        TEST_DN = "cn=test_abandon_%d,%s" % (i, DEFAULT_SUFFIX)
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_abandon'),
                                                    ('cn', b'test_abandon_%d' % i),
                                                    ('userPassword', b'test_abandon'),
                                               ])
        entries.append(TEST_DN)

    # run a page search (with the session) using a small window. So we can abandon it.
    req_ctrl = SimplePagedResultsControl(True, size=3, cookie='')
    msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, r'(objectclass=*)', ['cn'], serverctrls=[req_ctrl])
    time.sleep(1)
    topology_st.standalone.abandon_ext(msgid, serverctrls=[st_ctrl])


    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*ABANDON.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        for ent in entries:
            try:
                topology_st.standalone.delete_s(ent)
            except:
                pass

    request.addfinalizer(fin)

def test_exact_max_lgth_session_tracking_extop(topology_st, request):
    """Verify that an exact max length session_tracking string
    is added (not truncate) during an extended operation

    :id: 078d33c4-9124-4766-966e-2e3eebdf0e18
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. run whoami extop
        2. Check the exact same string (max length 15)
           is present in the access log for the EXTOP
    :expectedresults:
        1. success
        2. Log should contain one log with that session for the EXTOP
    """
    SESSION_TRACKING_IDENTIFIER = "Extop long --->"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    extop = ExtendedRequest(requestName = '1.3.6.1.4.1.4203.1.11.3', requestValue=None)
    (oid_response, res) = topology_st.standalone.extop_s(extop, serverctrls=[st_ctrl])

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=120.* sid="%s".*' % SESSION_TRACKING_IDENTIFIER)
    assert len(access_log_lines) == 1

    def fin():
        pass

    request.addfinalizer(fin)

def test_long_session_tracking_srch(topology_st, request):
    """Verify that a long session_tracking string
    is added (truncate) during a search

    :id: 56118d13-c0b1-401f-aaa4-6dc233156e36
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Do a search with a long session tracking string
        2. Restart the instance to flush the log
        3. Check the exact same string is present in the access log (with '.')
    :expectedresults:
        1. Search should succeed
        2. success
        3. Log should contain one log with that session
    """


    SESSION_TRACKING_IDENTIFIER_MAX = "SRCH long ---->"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_MAX + "xxxxxxxx"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    topology_st.standalone.search_ext_s(DEFAULT_SUFFIX,
                                        ldap.SCOPE_SUBTREE,
                                        '(uid=*)',
                                        serverctrls=[st_ctrl])
    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=101.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 1

    def fin():
        pass

    request.addfinalizer(fin)

def test_long_session_tracking_add(topology_st, request):
    """Verify that a long session_tracking string
    is added (truncate) during a add

    :id: ac97bc6b-f2c5-41e2-9ab6-df05afb2757c
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry with a long session tracking
        2. Restart the instance to flush the log
        3. Check the exact same string is present in the access log (with '.')
    :expectedresults:
        1. Add should succeed
        2. success
        3. Log should contain one log with that session
    """


    SESSION_TRACKING_IDENTIFIER_MAX = "ADD long ----->"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_MAX + "xxxxxxxx"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_add," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_add'),
                                                    ('cn', b'test_add'),
                                                    ('userPassword', b'test_add'),
                                               ],
                                               serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_long_session_tracking_del(topology_st, request):
    """Verify that a long session_tracking string
    is added (truncate) during a del

    :id: 283152b8-ba6b-4153-b2de-17070911bf18
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. Delete the test entry with a long session tracking
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the DEL (with '.')
    :expectedresults:
        1. Add should succeed
        2. DEL should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the DEL
    """


    SESSION_TRACKING_IDENTIFIER_MAX = "DEL long ----->"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_MAX + "xxxxxxxx"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_del," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_del'),
                                                    ('cn', b'test_del'),
                                                    ('userPassword', b'test_del'),
                                               ])
        topology_st.standalone.delete_ext_s(TEST_DN,
                                            serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=107.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_long_session_tracking_mod(topology_st, request):
    """Verify that a long session_tracking string
    is added (truncate) during a MOD

    :id: 6bfcca4b-40b4-4288-9b77-cfa0d4f15c14
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. Modify the test entry with an long session tracking
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the MOD (with '.')
    :expectedresults:
        1. Add should succeed
        2. Mod should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the MOD
    """


    SESSION_TRACKING_IDENTIFIER_MAX = "MOD long ----->"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_MAX + "xxxxxxxx"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_mod," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_del'),
                                                    ('cn', b'test_del'),
                                                    ('userPassword', b'test_del'),
                                               ])
        topology_st.standalone.modify_ext_s(TEST_DN,
                                            [(ldap.MOD_REPLACE, 'sn', b'new_sn')],
                                            serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=103.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_long_session_tracking_compare(topology_st, request):
    """Verify that a long session_tracking string
    is added (truncate) during a compare

    :id: 840ad60b-d2c5-4375-a50d-1553701d3c22
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. compare an attribute with an exact max length session tracking
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the COMPARE (with '.')
    :expectedresults:
        1. Add should succeed
        2. Compare should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the COMPARE
    """


    SESSION_TRACKING_IDENTIFIER_MAX = "COMPARE long ->"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_MAX + "xxxxxxxx"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_compare," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_compare'),
                                                    ('cn', b'test_compare'),
                                                    ('userPassword', b'test_compare'),
                                               ])
        topology_st.standalone.compare_ext_s(TEST_DN, 'sn', b'test_compare', serverctrls=[st_ctrl])
        topology_st.standalone.compare_ext_s(TEST_DN, 'sn', b'test_fail_compare', serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*err=6 tag=111.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 1

    access_log_lines = topology_st.standalone.ds_access_log.match('.*err=5 tag=111.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_long_session_tracking_abandon(topology_st, request):
    """Verify that long session_tracking string
    is added (truncate) during an abandon

    :id: bded1fbb-b123-42c5-8d28-9fcf9f19af94
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add 10 test entries
        2. Launch Page Search with a window of 3
        3. Abandon the Page Search with long session tracking
        4. Restart the instance to flush the log
        5. Check the exact same string is not present in the access log for the ADD
        6. Check the exact same string is present in the access log for the ABANDON (with '.')
    :expectedresults:
        1. Add should succeed
        2. success
        3. success
        4. success
        5. Log should not contain log with that session for the ADDs
        6. Log should contain one log with that session for the abandon
    """

    SESSION_TRACKING_IDENTIFIER_MAX = "ABANDON long ->"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_MAX + "xxxxxxxx"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    # provision more entries than the page search will fetch
    entries = []
    for i in range(10):
        TEST_DN = "cn=test_abandon_%d,%s" % (i, DEFAULT_SUFFIX)
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_abandon'),
                                                    ('cn', b'test_abandon_%d' % i),
                                                    ('userPassword', b'test_abandon'),
                                               ])
        entries.append(TEST_DN)

    # run a page search (with the session) using a small window. So we can abandon it.
    req_ctrl = SimplePagedResultsControl(True, size=3, cookie='')
    msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, r'(objectclass=*)', ['cn'], serverctrls=[req_ctrl])
    time.sleep(1)
    topology_st.standalone.abandon_ext(msgid, serverctrls=[st_ctrl])


    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*ABANDON.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 1

    def fin():
        for ent in entries:
            try:
                topology_st.standalone.delete_s(ent)
            except:
                pass

    request.addfinalizer(fin)

def test_long_session_tracking_extop(topology_st, request):
    """Verify that long session_tracking string
    is added (truncate) during an extended operation

    :id: a7aa65d2-eed8-4bdd-9786-2379997ff0b7
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. run whoami extop
        2. Check the truncated long session string
           is present in the access log for the EXTOP
    :expectedresults:
        1. success
        2. Log should contain one log with that session for the EXTOP
    """
    SESSION_TRACKING_IDENTIFIER_MAX = "Extop long --->"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_MAX + "xxxxxxxx"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    extop = ExtendedRequest(requestName = '1.3.6.1.4.1.4203.1.11.3', requestValue=None)
    (oid_response, res) = topology_st.standalone.extop_s(extop, serverctrls=[st_ctrl])

    topology_st.standalone.restart(timeout=10)
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=120.* sid="%s...".*' % SESSION_TRACKING_IDENTIFIER_MAX)
    assert len(access_log_lines) == 1

    def fin():
        pass

    request.addfinalizer(fin)

def test_escaped_session_tracking_srch(topology_st, request):
    """Verify that a session_tracking string containing escaped character
    is added (not truncate) during a search

    :id: dce83631-7a3f-4af8-a79a-ee81df4b0595
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Do a search with a session tracking string containing escaped character
        2. Restart the instance to flush the log
        3. Check the exact same string is present in the access log
    :expectedresults:
        1. Search should succeed
        2. success
        3. Log should contain one log with that session
    """


    SESSION_TRACKING_IDENTIFIER_START = "SRCH"
    SESSION_TRACKING_IDENTIFIER_ORIGINAL = "  "
    SESSION_TRACKING_IDENTIFIER_ESCAPED = " \\\\06 "
    SESSION_TRACKING_IDENTIFIER_END = "escape"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ORIGINAL + SESSION_TRACKING_IDENTIFIER_END
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    topology_st.standalone.search_ext_s(DEFAULT_SUFFIX,
                                        ldap.SCOPE_SUBTREE,
                                        '(uid=*)',
                                        serverctrls=[st_ctrl])
    topology_st.standalone.restart(timeout=10)
    sid_escaped = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ESCAPED + SESSION_TRACKING_IDENTIFIER_END
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=101.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 1

    def fin():
        pass

    request.addfinalizer(fin)

def test_escaped_session_tracking_add(topology_st, request):
    """Verify that a session_tracking string containing escaped character
    is added (not truncate) during a add

    :id: df40e5b3-20d9-4a85-a7ad-246e3ec25f4f
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry with a session tracking containing escaped character
        2. Restart the instance to flush the log
        3. Check the exact same string is present in the access log
    :expectedresults:
        1. Add should succeed
        2. success
        3. Log should contain one log with that session
    """


    SESSION_TRACKING_IDENTIFIER_START = "ADD"
    SESSION_TRACKING_IDENTIFIER_ORIGINAL = "  "
    SESSION_TRACKING_IDENTIFIER_ESCAPED = " \\\\07 "
    SESSION_TRACKING_IDENTIFIER_END = "escape"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ORIGINAL + SESSION_TRACKING_IDENTIFIER_END
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_add," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_add'),
                                                    ('cn', b'test_add'),
                                                    ('userPassword', b'test_add'),
                                               ],
                                               serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    sid_escaped = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ESCAPED + SESSION_TRACKING_IDENTIFIER_END
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_escaped_session_tracking_del(topology_st, request):
    """Verify that a session_tracking string containing escaped character
    is added (not truncate) during a DEL

    :id: 561c75fc-ae24-42ed-b062-9c994f71e3fc
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. Delete the test entry with a session tracking containing escaped character
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the DEL
    :expectedresults:
        1. Add should succeed
        2. DEL should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the DEL
    """


    SESSION_TRACKING_IDENTIFIER_START = "DEL"
    SESSION_TRACKING_IDENTIFIER_ORIGINAL = "  "
    SESSION_TRACKING_IDENTIFIER_ESCAPED = " \\\\14 "
    SESSION_TRACKING_IDENTIFIER_END = "escape"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ORIGINAL + SESSION_TRACKING_IDENTIFIER_END
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_del," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_del'),
                                                    ('cn', b'test_del'),
                                                    ('userPassword', b'test_del'),
                                               ])
        topology_st.standalone.delete_ext_s(TEST_DN,
                                            serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    sid_escaped = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ESCAPED + SESSION_TRACKING_IDENTIFIER_END
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=107.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_escaped_session_tracking_mod(topology_st, request):
    """Verify that a session_tracking string containing escaped character
    is added (not truncate) during a MOD

    :id: ca2ca411-32b4-4a9f-845d-e596f08a849c
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. Modify the test entry with a session tracking containing escaped character
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the MOD
    :expectedresults:
        1. Add should succeed
        2. Mod should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the MOD
    """


    SESSION_TRACKING_IDENTIFIER_START = "MOD"
    SESSION_TRACKING_IDENTIFIER_ORIGINAL = "  "
    SESSION_TRACKING_IDENTIFIER_ESCAPED = " \\\\10 "
    SESSION_TRACKING_IDENTIFIER_END = "escape"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ORIGINAL + SESSION_TRACKING_IDENTIFIER_END
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_mod," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_del'),
                                                    ('cn', b'test_del'),
                                                    ('userPassword', b'test_del'),
                                               ])
        topology_st.standalone.modify_ext_s(TEST_DN,
                                            [(ldap.MOD_REPLACE, 'sn', b'new_sn')],
                                            serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)
    sid_escaped = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ESCAPED + SESSION_TRACKING_IDENTIFIER_END
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=103.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)

def test_escaped_session_tracking_compare(topology_st, request):
    """Verify that a session_tracking string containing escaped character
    is added (not truncate) during a COMPARE

    :id: 93c13457-5c51-4bec-8e8a-0c6320cd970b
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add a test entry
        2. compare an attribute with a session tracking containing escaped character
        3. Restart the instance to flush the log
        4. Check the exact same string is not present in the access log for the ADD
        5. Check the exact same string is present in the access log for the COMPARE
    :expectedresults:
        1. Add should succeed
        2. Compare should succeed
        3. success
        4. Log should not contain a log with that session for the ADD
        5. Log should contain one log with that session for the COMPARE
    """


    # be careful that the complete string is less than 15 chars
    SESSION_TRACKING_IDENTIFIER_START = "COMPARE"
    SESSION_TRACKING_IDENTIFIER_ORIGINAL = "  "
    SESSION_TRACKING_IDENTIFIER_ESCAPED = " \\\\11 "
    SESSION_TRACKING_IDENTIFIER_END = "esc"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ORIGINAL + SESSION_TRACKING_IDENTIFIER_END
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    TEST_DN = "cn=test_compare," + DEFAULT_SUFFIX
    try:
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_compare'),
                                                    ('cn', b'test_compare'),
                                                    ('userPassword', b'test_compare'),
                                               ])
        topology_st.standalone.compare_ext_s(TEST_DN, 'sn', b'test_compare', serverctrls=[st_ctrl])
        topology_st.standalone.compare_ext_s(TEST_DN, 'sn', b'test_fail_compare', serverctrls=[st_ctrl])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    topology_st.standalone.restart(timeout=10)

    sid_escaped = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ESCAPED + SESSION_TRACKING_IDENTIFIER_END
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*err=6 tag=111.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 1

    access_log_lines = topology_st.standalone.ds_access_log.match('.*err=5 tag=111.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 1

    def fin():
        try:
            topology_st.standalone.delete_s(TEST_DN)
        except:
            pass

    request.addfinalizer(fin)


def test_escaped_session_tracking_abandon(topology_st, request):
    """Verify that a session_tracking string containing escaped character
    is added (not truncate) during an abandon

    :id: 37a9d1db-ec19-4381-88f3-48eae477cb81
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. Add 10 test entries
        2. Launch Page Search with a window of 3
        3. Abandon the Page Search with a session tracking containing escaped character
        4. Restart the instance to flush the log
        5. Check the exact same string is not present in the access log for the ADD
        6. Check the exact same string is present in the access log for the ABANDON
    :expectedresults:
        1. Add should succeed
        2. success
        3. success
        4. success
        5. Log should not contain log with that session for the ADDs
        6. Log should contain one log with that session for the abandon
    """

    # be careful that the complete string is less than 15 chars
    SESSION_TRACKING_IDENTIFIER_START = "ABANDON"
    SESSION_TRACKING_IDENTIFIER_ORIGINAL = "  "
    SESSION_TRACKING_IDENTIFIER_ESCAPED = " \\\\12 "
    SESSION_TRACKING_IDENTIFIER_END = "es"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ORIGINAL + SESSION_TRACKING_IDENTIFIER_END
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    # provision more entries than the page search will fetch
    entries = []
    for i in range(10):
        TEST_DN = "cn=test_abandon_%d,%s" % (i, DEFAULT_SUFFIX)
        ent = topology_st.standalone.add_ext_s(TEST_DN,
                                               [
                                                    ('objectClass', b'person'),
                                                    ('sn', b'test_abandon'),
                                                    ('cn', b'test_abandon_%d' % i),
                                                    ('userPassword', b'test_abandon'),
                                               ])
        entries.append(TEST_DN)

    # run a page search (with the session) using a small window. So we can abandon it.
    req_ctrl = SimplePagedResultsControl(True, size=3, cookie='')
    msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, r'(objectclass=*)', ['cn'], serverctrls=[req_ctrl])
    time.sleep(1)
    topology_st.standalone.abandon_ext(msgid, serverctrls=[st_ctrl])


    topology_st.standalone.restart(timeout=10)
    sid_escaped = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ESCAPED + SESSION_TRACKING_IDENTIFIER_END
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=105.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 0

    access_log_lines = topology_st.standalone.ds_access_log.match('.*ABANDON.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 1

    def fin():
        for ent in entries:
            try:
                topology_st.standalone.delete_s(ent)
            except:
                pass

    request.addfinalizer(fin)

def test_escaped_session_tracking_extop(topology_st, request):
    """Verify that a session_tracking string containing escaped character
    is added (not truncate) during an extended operation

    :id: fd3afce9-86c9-4d87-ab05-9d19f2d733c3
    :customerscenario: False
    :setup: Standalone instance, default backend
    :steps:
        1. run whoami extop
        2. Check the exact same string is present in the access log for the EXTOP
    :expectedresults:
        1. success
        2. Log should contain one log with that session for the EXTOP
    """

    # be careful that the complete string is less than 15 chars
    SESSION_TRACKING_IDENTIFIER_START = "EXTOP"
    SESSION_TRACKING_IDENTIFIER_ORIGINAL = "  "
    SESSION_TRACKING_IDENTIFIER_ESCAPED = " \\\\13 "
    SESSION_TRACKING_IDENTIFIER_END = "escap"
    SESSION_TRACKING_IDENTIFIER = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ORIGINAL + SESSION_TRACKING_IDENTIFIER_END
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )

    extop = ExtendedRequest(requestName = '1.3.6.1.4.1.4203.1.11.3', requestValue=None)
    (oid_response, res) = topology_st.standalone.extop_s(extop, serverctrls=[st_ctrl])

    topology_st.standalone.restart(timeout=10)
    sid_escaped = SESSION_TRACKING_IDENTIFIER_START + SESSION_TRACKING_IDENTIFIER_ESCAPED + SESSION_TRACKING_IDENTIFIER_END
    access_log_lines = topology_st.standalone.ds_access_log.match('.*tag=120.* sid="%s".*' % sid_escaped)
    assert len(access_log_lines) == 1

    def fin():
        pass

    request.addfinalizer(fin)

def test_sid_replication(topo_m2, request):
    """Check that session ID are logged on
       supplier side (errorLog) and consumer side (accessLog)

    :id: e716b04c-152b-4964-ae7a-b18fb4655cb9
    :setup: 2 Supplier Instances
    :steps:
        1. Initialize replication
        2. Enable replication debug logging
        3. Create and update a test user
        4. Stop instances
        5. Check that 'sid=' exist both on supplier/consumer sides
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    m1 = topo_m2.ms["supplier1"]
    m2 = topo_m2.ms["supplier2"]

    m1.config.loglevel((ErrorLog.REPLICA,))
    m2.config.loglevel((ErrorLog.REPLICA,))

    TEST_ENTRY_NAME = 'sid_test'
    TEST_ENTRY_DN = 'uid={},{}'.format(TEST_ENTRY_NAME, DEFAULT_SUFFIX)
    test_user = UserAccount(m1, TEST_ENTRY_DN)
    test_user.create(properties={
        'uid': TEST_ENTRY_NAME,
        'cn': TEST_ENTRY_NAME,
        'sn': TEST_ENTRY_NAME,
        'userPassword': TEST_ENTRY_NAME,
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/sid_test',
    })

    # create a large value set so that it is sorted
    for i in range(1,20):
        test_user.add('description', 'value {}'.format(str(i)))

    time.sleep(2)
    m1.stop()
    m2.stop()
    log_lines = m1.ds_access_log.match('.* sid=".*')
    assert len(log_lines) > 0
    log_lines = m2.ds_error_log.match('.* - sid=".*')
    assert len(log_lines) > 0
    m1.start()
    m2.start()

    def fin():
        log.info('Deleting entry {}'.format(TEST_ENTRY_DN))
        test_user.delete()
        m1.config.loglevel((ErrorLog.DEFAULT,), service='error')
        m2.config.loglevel((ErrorLog.DEFAULT,), service='error')

    request.addfinalizer(fin)

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
