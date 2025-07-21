# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import time
import ldap
import pytest
from lib389._constants import DEFAULT_SUFFIX, PASSWORD, LOG_ACCESS_LEVEL, DN_DM
from lib389.properties import TASK_WAIT
from lib389.topologies import topology_m2 as topo_m2
from lib389.idm.group import Groups
from lib389.idm.user import UserAccounts
from lib389.dirsrv_log import DirsrvAccessJSONLog
from lib389.index import VLVSearch, VLVIndex
from lib389.tasks import Tasks
from lib389.config import CertmapLegacy
from lib389.nss_ssl import NssSsl
from ldap.controls.vlv import VLVRequestControl
from ldap.controls.sss import SSSRequestControl
from ldap.controls import SimplePagedResultsControl
from ldap.controls.sessiontrack import SessionTrackingControl, SESSION_TRACKING_CONTROL_OID
from ldap.controls.psearch import PersistentSearchControl,EntryChangeNotificationControl


log = logging.getLogger(__name__)

MAIN_KEYS = [
    "local_time",
    "key",
    "conn_id",
    "operation",
]
CONN = 'conn_id'
OP = 'op_id'


@pytest.fixture
def setup_test(topo_m2, request):
    """Configure log settings"""
    inst = topo_m2.ms["supplier1"]
    inst.config.replace('nsslapd-accesslog-logbuffering', 'off')
    inst.config.replace('nsslapd-accesslog-log-format', 'json')


def get_log_event(inst, op, key=None, val=None, key2=None, val2=None):
    """Get a specific access log event by operation and key/value
    """
    time.sleep(1)  # give a little time to flush to disk

    access_log = DirsrvAccessJSONLog(inst)
    log_lines = access_log.readlines()
    for line in log_lines:
        event = access_log.parse_line(line)
        if event is None or 'header' in event:
            # Skip non-json or header lines
            continue

        if event['operation'] == op:
            if key is not None and key2 is not None:
                if key in event and key2 in event:
                    val = str(val).lower()
                    val2 = str(val2).lower()
                    if val == str(event[key]).lower() and \
                       val2 == str(event[key2]).lower():
                        return event
            elif key is not None:
                if key in event:
                    val = str(val).lower()
                    if val == str(event[key]).lower():
                        return event
            else:
                return event

    return None


def create_vlv_search_and_index(inst):
    """
    Create VlvIndex
    """

    vlv_search = VLVSearch(inst)
    vlv_search_properties = {
        "objectclass": ["top", "vlvSearch"],
        "cn": "vlvSrch",
        "vlvbase": DEFAULT_SUFFIX,
        "vlvfilter": "(uid=*)",
        "vlvscope": str(ldap.SCOPE_SUBTREE),
    }
    vlv_search.create(
        basedn="cn=userroot,cn=ldbm database,cn=plugins,cn=config",
        properties=vlv_search_properties
    )

    vlv_index = VLVIndex(inst)
    vlv_index_properties = {
        "objectclass": ["top", "vlvIndex"],
        "cn": "vlvIdx",
        "vlvsort": "cn",
    }
    vlv_index.create(
        basedn="cn=vlvSrch,cn=userroot,cn=ldbm database,cn=plugins,cn=config",
        properties=vlv_index_properties
    )
    reindex_task = Tasks(inst)
    assert reindex_task.reindex(
        suffix=DEFAULT_SUFFIX,
        attrname=vlv_index.rdn,
        args={TASK_WAIT: True},
    ) == 0

    return vlv_search, vlv_index


def check_for_control(ctrl_list, oid, name):
    """
    Check if the oid and name of a control is present
    """
    for ctrl in ctrl_list:
        if ctrl['oid'] == oid and ctrl['oid_name'] == name:
            return True
    return False


def _run_psearch(inst, msg_id):
    """
    Run a search with EntryChangeNotificationControl
    """
    while True:
        try:
            _, data, _, _, _, _ = inst.result4(msgid=msg_id, all=0,
                                               timeout=1.0, add_ctrls=1,
                                               add_intermediates=1,
                                               resp_ctrl_classes={EntryChangeNotificationControl.controlType:EntryChangeNotificationControl})
        except ldap.TIMEOUT:
            # There are no more results, so we timeout.
            return


def test_access_json_format(topo_m2, setup_test):
    """Specify a test case purpose or name here

    :id: 30ca307b-e0e0-4aa1-ae07-31a349e87a69
    :setup: Two suppliers replication setup
    :steps:
        1. Test ADD is logged correctly
        2. Test SEARCH is logged correctly
        3. Test STAT is logged correctly
        4. Test MODIFY is logged correctly
        5. Test MODRDN is logged correctly
        6. Test CONNECTION is logged correctly
        7. Test BIND is logged correctly
        8. Test DISCONNECT is logged correctly
        9. Test SESSION TRACKING is logged correctly
        10. Test ENTRY is logged correctly
        11. Test VLV is logged correctly
        12. Test UNINDEXED is logged correctly
        13. Test DELETE is logged correctly
        14. Test PAGED SEARCH is logged correctly
        15. Test PERSISTENT SEARCH is logged correctly
        16. Test EXTENDED OP
        17. Test TLS_INFO is logged correctly
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
        17. Success
    """

    inst = topo_m2.ms["supplier1"]

    # Need to make sure internal logging is off or the test will fail
    inst.config.set("nsslapd-accesslog-level", "256")

    #
    # Add entry
    #
    log.info("Test ADD")
    DN = "uid=test_access_log,ou=people," + DEFAULT_SUFFIX
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'test_access_log',
        'cn': 'test',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '1000',
        'homeDirectory': '/home/test',
        'userPassword': PASSWORD
    })

    # Check all the expected keys are present
    event = get_log_event(inst, "ADD", "target_dn", user.dn)
    for key in MAIN_KEYS:
        assert key in event and event[key] != ""
    assert event['target_dn'].lower() == DN

    # Check result
    event = get_log_event(inst, "RESULT",
                          CONN, event['conn_id'],
                          OP, event['op_id'])
    assert event['err'] == 0

    #
    # Compare
    #
    log.info("Test COMPARE")
    inst.compare_ext_s(DN, 'sn', b'test_compare')
    event = get_log_event(inst, "COMPARE")
    assert event is not None

    #
    # Search entry
    #
    log.info("Test SEARCH")
    user.get_attr_val("cn")
    event = get_log_event(inst, "SEARCH", 'base_dn', user.dn)
    for key in MAIN_KEYS:
        assert key in event and event[key] != ""
    assert event['base_dn'] == user.dn
    assert event['scope'] == 0
    assert event['filter'] == "(objectClass=*)"

    # Check result
    event = get_log_event(inst, "RESULT", CONN, event['conn_id'],
                          OP, event['op_id'])
    assert event['err'] == 0

    #
    # Stat
    #
    log.info("Test STAT")
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    users_set = []
    for i in range(20):
        name = 'user_%d' % i
        last_user = users.create(properties={
            'uid': name,
            'sn': name,
            'cn': name,
            'uidNumber': '1000',
            'gidNumber': '1000',
            'homeDirectory': '/home/%s' % name,
            'mail': '%s@example.com' % name,
            'userpassword': 'pass%s' % name,
        })
        users_set.append(last_user)
    inst.config.set("nsslapd-statlog-level", "1")
    inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "cn=user_*")
    event = get_log_event(inst, "STAT", CONN, "1")
    assert event['stat_attr'] == 'ancestorid'
    assert event['stat_key'] == 'eq'
    assert event['stat_key_value'] == '1'
    assert event['stat_count'] == 39
    inst.config.set("nsslapd-statlog-level", "0")

    #
    # Modify entry
    #
    log.info("Test MODIFY")
    user.replace('sn', 'new sn')
    event = get_log_event(inst, "ADD")
    for key in MAIN_KEYS:
        assert key in event and event[key] != ""
    assert event['target_dn'].lower() == DN

    # Check result
    event = get_log_event(inst, "RESULT", CONN, event['conn_id'],
                          OP, event['op_id'])
    assert event['err'] == 0

    #
    # ModRDN entry
    #
    log.info("Test MODRDN")
    NEW_SUP = "ou=groups," + DEFAULT_SUFFIX
    user.rename('uid=test_modrdn', newsuperior=NEW_SUP, deloldrdn=True)
    event = get_log_event(inst, "MODRDN")
    for key in MAIN_KEYS:
        assert key in event and event[key] != ""
    assert event['target_dn'].lower() == DN
    assert event['newrdn'] == "uid=test_modrdn"
    assert event['newsup'] == NEW_SUP
    assert event['deleteoldrdn']

    # Check result
    event = get_log_event(inst, "RESULT", CONN, event['conn_id'],
                          OP, event['op_id'])
    assert event['err'] == 0

    #
    # Test CONNECTION
    #
    log.info("Test CONNECTION")
    user_conn = user.bind(PASSWORD)
    event = get_log_event(inst, "CONNECTION")
    for key in MAIN_KEYS:
        assert key in event and event[key] != ""
    assert event['fd']
    assert event['slot']

    #
    # Test BIND
    #
    log.info("Test BIND")
    event = get_log_event(inst, "BIND")
    for key in MAIN_KEYS:
        assert key in event and event[key] != ""
    assert event['bind_dn'] == user.dn

    # Check result
    event = get_log_event(inst, "RESULT", CONN, event['conn_id'],
                          OP, event['op_id'])
    assert event['err'] == 0
    assert event['bind_dn'] == user.dn

    #
    # Test UNBIND and DISCONNECT
    #
    log.info("Test UNBIND & DISCONNECT")
    user_conn.close()
    event = get_log_event(inst, "UNBIND", CONN, event['conn_id'])
    assert event
    event = get_log_event(inst, "DISCONNECT", CONN, event['conn_id'])
    for key in MAIN_KEYS:
        assert key in event and event[key] != ""
    assert event['close_reason']

    #
    # Session tracking
    #
    log.info("Test SESSION TRACKING")
    SESSION_TRACKING_IDENTIFIER = "SRCH short"
    SESSION_SOURCE_IP = '10.0.0.10'
    SESSION_SOURCE_NAME = 'host.example.com'
    SESSION_TRACKING_FORMAT_OID = SESSION_TRACKING_CONTROL_OID + ".1234"
    st_ctrl = SessionTrackingControl(
      SESSION_SOURCE_IP,
      SESSION_SOURCE_NAME,
      SESSION_TRACKING_FORMAT_OID,
      SESSION_TRACKING_IDENTIFIER
    )
    inst.search_ext_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)',
                      serverctrls=[st_ctrl])
    event = get_log_event(inst, "RESULT", "sid", SESSION_TRACKING_IDENTIFIER)
    assert event is not None

    #
    # ENTRY
    #
    log.info("Test ENTRY")
    inst.config.set(LOG_ACCESS_LEVEL, str(512 + 4))
    inst.search_ext_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    event = get_log_event(inst, "ENTRY")
    inst.config.set(LOG_ACCESS_LEVEL, "256")
    assert event is not None

    #
    # VLV & SORT
    #
    log.info("Test VLV")
    vlv_control = VLVRequestControl(criticality=True,
                                    before_count="1",
                                    after_count="3",
                                    offset="5",
                                    content_count=0,
                                    greater_than_or_equal=None,
                                    context_id=None)

    sss_control = SSSRequestControl(criticality=True, ordering_rules=['cn'])
    inst.search_ext_s(base=DEFAULT_SUFFIX,
                      scope=ldap.SCOPE_SUBTREE,
                      filterstr='(uid=*)',
                      serverctrls=[vlv_control, sss_control])
    event = get_log_event(inst, "VLV")
    assert event is not None

    # VLV Request
    assert check_for_control(event['request_controls'],
                             "2.16.840.1.113730.3.4.9",
                             "LDAP_CONTROL_VLVREQUEST")
    assert check_for_control(event['request_controls'],
                             "1.2.840.113556.1.4.473",
                             "LDAP_CONTROL_SORTREQUEST")
    assert check_for_control(event['response_controls'],
                             "1.2.840.113556.1.4.474",
                             "LDAP_CONTROL_SORTRESPONSE")
    assert check_for_control(event['response_controls'],
                             "2.16.840.1.113730.3.4.10",
                             "LDAP_CONTROL_VLVRESPONSE")
    vlvRequest = event['vlv_request']
    assert vlvRequest['request_before_count'] == 1
    assert vlvRequest['request_after_count'] == 3
    assert vlvRequest['request_index'] == 4
    assert vlvRequest['request_content_count'] == 0
    assert vlvRequest['request_value_len'] == 0
    assert "SORT cn" in vlvRequest['request_sort']

    vlvResponse = event['vlv_response']
    assert vlvResponse['response_target_position'] == 5
    assert vlvResponse['response_content_count'] == 23
    assert vlvResponse['response_result'] == 0

    # VLV Result
    event = get_log_event(inst, "RESULT", CONN, event['conn_id'],
                          OP, event['op_id'])
    assert check_for_control(event['request_controls'],
                             "2.16.840.1.113730.3.4.9",
                             "LDAP_CONTROL_VLVREQUEST")
    assert check_for_control(event['request_controls'],
                             "1.2.840.113556.1.4.473",
                             "LDAP_CONTROL_SORTREQUEST")
    assert check_for_control(event['response_controls'],
                             "1.2.840.113556.1.4.474",
                             "LDAP_CONTROL_SORTRESPONSE")
    assert check_for_control(event['response_controls'],
                             "2.16.840.1.113730.3.4.10",
                             "LDAP_CONTROL_VLVRESPONSE")

    #
    # Test unindexed search detauils (using previous vlv search)
    #
    log.info("Test UNINDEXED")
    assert event['client_ip'] is not None
    note = event['notes'][0]
    assert note['note'] == 'U'
    assert note['description'] == 'Partially Unindexed Filter'
    assert note['base_dn'] == 'dc=example,dc=com'
    assert note['filter'] == '(uid=*)'

    #
    # Delete entry
    #
    log.info("Test DELETE")
    DN = "uid=test_modrdn," + NEW_SUP
    user.delete()
    event = get_log_event(inst, "DELETE")
    assert event['target_dn'] == DN

    # Check result
    event = get_log_event(inst, "RESULT", CONN, event['conn_id'],
                          OP, event['op_id'])
    assert event['err'] == 0

    #
    # Paged search
    #
    log.info("Test PAGED SEARCH")
    req_ctrl = SimplePagedResultsControl(True, size=3, cookie='')
    pages = 0
    pctrls = []
    search_filter = "sn=user_*"

    msgid = inst.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, search_filter,
                            ['cn'], serverctrls=[req_ctrl])
    while True:
        try:
            rtype, rdata, rmsgid, rctrls = inst.result3(msgid, timeout=0.001)
        except ldap.TIMEOUT:
            if pages > 1:
                inst.abandon(msgid)
                break
            continue
        pages += 1
        pctrls = [
            c
            for c in rctrls
            if c.controlType == SimplePagedResultsControl.controlType
            ]

        if pctrls:
            if pctrls[0].cookie:
                # Copy cookie from response control to request control
                req_ctrl.cookie = pctrls[0].cookie
                msgid = inst.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                        search_filter, ['cn'],
                                        serverctrls=[req_ctrl])
            else:
                break  # No more pages available
        else:
            break

    # Check log
    event = get_log_event(inst, "SEARCH", "filter", "(sn=user_*)")
    assert event is not None
    assert check_for_control(event['request_controls'],
                             "1.2.840.113556.1.4.319",
                             "LDAP_CONTROL_PAGEDRESULTS")

    event = get_log_event(inst, "RESULT", CONN, event['conn_id'],
                          OP, event['op_id'])
    assert event is not None
    assert 'pr_idx' in event
    assert 'pr_cookie' in event
    assert check_for_control(event['response_controls'],
                             "1.2.840.113556.1.4.319",
                             "LDAP_CONTROL_PAGEDRESULTS")
    note = event['notes'][0]
    assert note['note'] == 'P'
    assert note['description'] == 'Paged Search'

    event = get_log_event(inst, "ABANDON", CONN, "1")
    assert event is not None
    assert 'target_op' in event

    #
    # Persistent search
    #
    log.info("Test PERSISTENT SEARCH")
    # Create the search control
    psc = PersistentSearchControl()
    # do a search extended with the control
    msg_id = inst.search_ext(base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE,
                             attrlist=['*'], serverctrls=[psc])
    # Get the result for the message id with result4
    _run_psearch(inst, msg_id)
    # Change an entry / add one
    groups = Groups(inst, DEFAULT_SUFFIX)
    groups.create(properties={'cn': 'group1',
                              'description': 'testgroup'})
    time.sleep(.5)
    # Now run the result again and see what's there.
    _run_psearch(inst, msg_id)

    # Check the log
    event = get_log_event(inst, "SEARCH", CONN, "1",
                          "psearch", "true")
    assert event is not None
    assert check_for_control(event['request_controls'],
                             "2.16.840.1.113730.3.4.3",
                             "LDAP_CONTROL_PERSISTENTSEARCH")

    #
    # TLS INFO/TLS CLIENT INFO
    #
    RDN_TEST_USER = 'testuser'
    RDN_TEST_USER_WRONG = 'testuser_wrong'
    inst.enable_tls()
    inst.restart()

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': RDN_TEST_USER,
        'cn': RDN_TEST_USER,
        'sn': RDN_TEST_USER,
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': f'/home/{RDN_TEST_USER}',
        'userpassword': 'password'
    })

    ssca_dir = inst.get_ssca_dir()
    ssca = NssSsl(dbpath=ssca_dir)
    ssca.create_rsa_user(RDN_TEST_USER)
    ssca.create_rsa_user(RDN_TEST_USER_WRONG)

    # Get the details of where the key and crt are.
    tls_locs = ssca.get_rsa_user(RDN_TEST_USER)
    tls_locs_wrong = ssca.get_rsa_user(RDN_TEST_USER_WRONG)

    user.enroll_certificate(tls_locs['crt_der_path'])

    # Turn on the certmap.
    cm = CertmapLegacy(inst)
    certmaps = cm.list()
    certmaps['default']['DNComps'] = ''
    certmaps['default']['FilterComps'] = ['cn']
    certmaps['default']['VerifyCert'] = 'off'
    cm.set(certmaps)

    # Check that EXTERNAL is listed in supported mechns.
    assert (inst.rootdse.supports_sasl_external())

    # Restart to allow certmaps to be re-read: Note, we CAN NOT use post_open
    # here, it breaks on auth. see lib389/__init__.py
    inst.restart(post_open=False)

    # Attempt a bind with TLS external
    inst.open(saslmethod='EXTERNAL', connOnly=True, certdir=ssca_dir,
              userkey=tls_locs['key'], usercert=tls_locs['crt'])
    inst.restart()

    event = get_log_event(inst, "TLS_INFO")
    assert event is not None
    assert 'tls_version' in event
    assert 'keysize' in event
    assert 'cipher' in event

    event = get_log_event(inst, "TLS_CLIENT_INFO",
                          "subject",
                          "CN=testuser,O=testing,L=389ds,ST=Queensland,C=AU")
    assert event is not None
    assert 'tls_version' in event
    assert 'keysize' in event
    assert 'issuer' in event

    event = get_log_event(inst, "TLS_CLIENT_INFO",
                          "client_dn",
                          "uid=testuser,ou=People,dc=example,dc=com")
    assert event is not None
    assert 'tls_version' in event
    assert event['msg'] == "client bound"

    # Check for failed certmap error
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        inst.open(saslmethod='EXTERNAL', connOnly=True, certdir=ssca_dir,
                  userkey=tls_locs_wrong['key'],
                  usercert=tls_locs_wrong['crt'])

    event = get_log_event(inst, "TLS_CLIENT_INFO", "err", -185)
    assert event is not None
    assert 'tls_version' in event
    assert event['msg'] == "failed to map client certificate to LDAP DN"
    assert event['err_msg'] == "Certificate couldn't be mapped to an ldap entry"

    #
    # Extended op
    #
    log.info("Test EXTENDED_OP")
    event = get_log_event(inst, "EXTENDED_OP", "oid",
                          "2.16.840.1.113730.3.5.12")
    assert event is not None
    assert event['oid_name'] == "REPL_START_NSDS90_REPLICATION_REQUEST_OID"
    assert event['name'] == "replication-multisupplier-extop"

    event = get_log_event(inst, "EXTENDED_OP", "oid",
                          "2.16.840.1.113730.3.5.5")
    assert event is not None
    assert event['oid_name'] == "REPL_END_NSDS50_REPLICATION_REQUEST_OID"
    assert event['name'] == "replication-multisupplier-extop"

    #
    # Extended op info
    #
    log.info("Test EXTENDED_OP_INFO")
    OLD_PASSWD = 'password'
    NEW_PASSWD = 'newpassword'

    assert inst.simple_bind_s(DN_DM, PASSWORD)

    assert inst.passwd_s(user.dn, OLD_PASSWD, NEW_PASSWD)
    event = get_log_event(inst, "EXTENDED_OP_INFO", "name",
                          "passwd_modify_plugin")
    assert event is not None
    assert event['bind_dn'] == "cn=directory manager"
    assert event['target_dn'] == user.dn.lower()
    assert event['msg'] == "success"

    # Test no such object
    BAD_DN = user.dn + ",dc=not"
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        inst.passwd_s(BAD_DN, OLD_PASSWD, NEW_PASSWD)

    event = get_log_event(inst, "EXTENDED_OP_INFO", "target_dn", BAD_DN)
    assert event is not None
    assert event['bind_dn'] == "cn=directory manager"
    assert event['target_dn'] == BAD_DN.lower()
    assert event['msg'] == "No such entry exists."

    # Test invalid old password
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        inst.passwd_s(user.dn, "not_the_old_pw", NEW_PASSWD)
    event = get_log_event(inst, "EXTENDED_OP_INFO", "err", 49)
    assert event is not None
    assert event['bind_dn'] == "cn=directory manager"
    assert event['target_dn'] == user.dn.lower()
    assert event['msg'] == "Invalid oldPasswd value."

    # Test user without permissions
    user2 = users.create(properties={
        'uid': RDN_TEST_USER + "2",
        'cn': RDN_TEST_USER + "2",
        'sn': RDN_TEST_USER + "2",
        'uidNumber': '1001',
        'gidNumber': '2001',
        'homeDirectory': f'/home/{RDN_TEST_USER + "2"}',
        'userpassword': 'password'
    })
    inst.simple_bind_s(user2.dn, 'password')
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        inst.passwd_s(user.dn, NEW_PASSWD, OLD_PASSWD)
    event = get_log_event(inst, "EXTENDED_OP_INFO", "err", 50)
    assert event is not None
    assert event['bind_dn'] == user2.dn.lower()
    assert event['target_dn'] == user.dn.lower()
    assert event['msg'] == "Insufficient access rights"


    # Reset bind
    inst.simple_bind_s(DN_DM, PASSWORD)



if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
