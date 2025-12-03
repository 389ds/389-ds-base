# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import time
import logging
import ldap
import pytest
from lib389.idm.user import TEST_USER_PROPERTIES, UserAccounts, UserAccount
from lib389.utils import *
from lib389._constants import *
from lib389.replica import Changelog5
from lib389.dseldif import *
from lib389.topologies import topology_m3 as topo_m3
from lib389.agreement import Agreements
from lib389.replica import ReplicationManager, Replicas
from lib389.cli_ctl.dblib import DbscanHelper


pytestmark = pytest.mark.tier1

NEW_SUFFIX_NAME = 'test_repl'
NEW_SUFFIX = 'o={}'.format(NEW_SUFFIX_NAME)
NEW_BACKEND = 'repl_base'
CHANGELOG = 'cn=changelog,{}'.format(DN_USERROOT_LDBM)
MAXAGE_ATTR = 'nsslapd-changelogmaxage'
MAXAGE_STR = '30'
TRIMINTERVAL_STR = '5'
TRIMINTERVAL = 'nsslapd-changelogtrim-interval'

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

count = 0

def test_cleanallruv_repl(topo_m3):
    """Test that cleanallruv could not break replication if anchor csn in ruv originated
    in deleted replica

    :id: 46faba9a-897e-45b8-98dc-aec7fa8cec9a
    :setup: 3 Suppliers
    :steps:
        1. Configure error log level to 8192 in all suppliers
        2. Modify nsslapd-changelogmaxage=30 and nsslapd-changelogtrim-interval=5 for M1 and M2
        3. Add test users to 3 suppliers
        4. Launch ClearRuv but withForce
        5. Check the users after CleanRUV, because of changelog trimming, it will effect the CLs
    :expectedresults:
        1. Error logs should be configured successfully
        2. Modify should be successful
        3. Test users should be added successfully
        4. ClearRuv should be launched successfully
        5. Users should be present according to the changelog trimming effect
    """

    M1 = topo_m3.ms["supplier1"]
    M2 = topo_m3.ms["supplier2"]
    M3 = topo_m3.ms["supplier3"]

    log.info("Change the error log levels for all suppliers")
    for s in (M1, M2, M3):
        s.config.replace('nsslapd-errorlog-level', "8192")

    log.info("Get the replication agreements for all 3 suppliers")
    m1_m2 = M1.agreement.list(suffix=SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    m1_m3 = M1.agreement.list(suffix=SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    m3_m1 = M3.agreement.list(suffix=SUFFIX, consumer_host=M1.host, consumer_port=M1.port)

    log.info("Modify nsslapd-changelogmaxage=30 and nsslapd-changelogtrim-interval=5 for M1 and M2")
    if ds_supports_new_changelog():
        CHANGELOG = 'cn=changelog,{}'.format(DN_USERROOT_LDBM)

        # set_value(M1, MAXAGE_ATTR, MAXAGE_STR)
        try:
            M1.modify_s(CHANGELOG, [(ldap.MOD_REPLACE, MAXAGE_ATTR, ensure_bytes(MAXAGE_STR))])
        except ldap.LDAPError as e:
            log.error('Failed to add ' + MAXAGE_ATTR, + ': ' + MAXAGE_STR + ' to ' + CHANGELOG + ': error {}'.format(get_ldap_error_msg(e,'desc')))
            assert False

        # set_value(M2, TRIMINTERVAL, TRIMINTERVAL_STR)
        try:
            M2.modify_s(CHANGELOG, [(ldap.MOD_REPLACE, TRIMINTERVAL, ensure_bytes(TRIMINTERVAL_STR))])
        except ldap.LDAPError as e:
            log.error('Failed to add ' + TRIMINTERVAL, + ': ' + TRIMINTERVAL_STR + ' to ' + CHANGELOG + ': error {}'.format(get_ldap_error_msg(e,'desc')))
            assert False
    else:
        log.info("Get the changelog enteries for M1 and M2")
        changelog_m1 = Changelog5(M1)
        changelog_m1.set_max_age(MAXAGE_STR)
        changelog_m1.set_trim_interval(TRIMINTERVAL_STR)

    log.info("Add test users to 3 suppliers")
    users_m1 = UserAccounts(M1, DEFAULT_SUFFIX)
    users_m2 = UserAccounts(M2, DEFAULT_SUFFIX)
    users_m3 = UserAccounts(M3, DEFAULT_SUFFIX)
    user_props = TEST_USER_PROPERTIES.copy()

    user_props.update({'uid': "testuser10"})
    user10 = users_m1.create(properties=user_props)

    user_props.update({'uid': "testuser20"})
    user20 = users_m2.create(properties=user_props)

    user_props.update({'uid': "testuser30"})
    user30 = users_m3.create(properties=user_props)

    # ::important:: the testuser31 is the oldest csn in M2,
    # because it will be cleared by changelog trimming
    user_props.update({'uid': "testuser31"})
    user31 = users_m3.create(properties=user_props)

    user_props.update({'uid': "testuser11"})
    user11 = users_m1.create(properties=user_props)

    user_props.update({'uid': "testuser21"})
    user21 = users_m2.create(properties=user_props)
    # this is to trigger changelog trim and interval values
    time.sleep(40)

    # Here M1, M2, M3 should have 11,21,31 and 10,20,30 are CL cleared
    M2.stop()
    M1.agreement.pause(m1_m2[0].dn)
    user_props.update({'uid': "testuser32"})
    user32 = users_m3.create(properties=user_props)

    user_props.update({'uid': "testuser33"})
    user33 = users_m3.create(properties=user_props)

    user_props.update({'uid': "testuser12"})
    user12 = users_m1.create(properties=user_props)

    M3.agreement.pause(m3_m1[0].dn)
    M3.agreement.resume(m3_m1[0].dn)
    time.sleep(40)

    # Here because of changelog trimming testusers 31 and 32 are CL cleared
    # ClearRuv is launched but with Force
    M3.stop()
    M1.tasks.cleanAllRUV(suffix=SUFFIX, replicaid='3',
                         force=True, args={TASK_WAIT: False})

    # here M1 should clear 31
    M2.start()
    M1.agreement.pause(m1_m2[0].dn)
    M1.agreement.resume(m1_m2[0].dn)
    time.sleep(10)

    # Check the users after CleanRUV
    expected_m1_users = [user31.dn, user11.dn, user21.dn, user32.dn, user33.dn, user12.dn]
    expected_m1_users = [x.lower() for x in expected_m1_users]
    expected_m2_users = [user31.dn, user11.dn, user21.dn, user12.dn]
    expected_m2_users = [x.lower() for x in expected_m2_users]

    current_m1_users = [user.dn for user in users_m1.list()]
    current_m1_users = [x.lower() for x in current_m1_users]
    current_m2_users = [user.dn for user in users_m2.list()]
    current_m2_users = [x.lower() for x in current_m2_users]

    assert set(expected_m1_users).issubset(current_m1_users)
    assert set(expected_m2_users).issubset(current_m2_users)


def get_agmt(inst, cn):
    for agmt in Agreements(inst).list():
        if agmt.get_attr_val_utf8("cn") == cn:
            return agmt
    return None


def wait_for_val(user, val, timeout=60):
    val = val.encode()
    for _ in range(timeout):
        time.sleep(1)
        vals = user.get_attr_vals('description')
        if val in vals:
            return
    assert False, f"Value {val} not found in {repr(user)} after {timeout} seconds"


def perform_updates(inst, replicas):
    global count
    repl = ReplicationManager(DEFAULT_SUFFIX)
    dn = 'uid=demo_user,ou=people,dc=example,dc=com'
    user = UserAccount(inst, dn)
    for i in range(4):
        val = f'Test {count}'
        user.add('description', val)
        count += 1
    for r in replicas:
        user = UserAccount(r, dn)
        wait_for_val(user, val)


def check_updates(inst):
    global count
    dn = 'uid=demo_user,ou=people,dc=example,dc=com'
    user = UserAccount(inst, dn)
    vals = user.get_attr_vals('description')
    for i in range(count):
        assert f'Test {i}'.encode() in vals


def test_offline_import(topo_m3):
    """Test that keep alive is not updated too soon after an offline import

    :id: 493a4810-cc5b-11f0-8c70-c85309d5c3e3
    :setup: 3 Suppliers
    :steps:
        1. Configure Topology in single loop S1 --> S2 --> S3 --> S1
        2. Set keep alive interrval to 60 seconds
        3. Perform some updates on S1
        4. Wait until changes are replicated to S2 and S3
        5. Stop S3
        6. Export S3 to ldif with replication metadata
        7. Perform some updates on S1
        8. Wait until changes are replicated to S2
        9. Stop S2
        10. Perform bulk import S3->S1
        11. Start S3
        12. Wait 40s
        13. Start S2
        14. Perform some updates on S2
        15. Wait until changes are replicated to S3 and S1
        16. Check that all changes are present on S1
    :expectedresults:
        1. No error
        2. No error
        3. No error
        4. No error
        5. No error
        6. No error
        7. No error
        8. No error
        9. No error
        10. No error
        11. No error
        12. No error
        13. No error
        14. No error
        15. No error
        16. No error if RHEL-129675 is fixed
    """

    S1 = topo_m3.ms["supplier1"]
    S2 = topo_m3.ms["supplier2"]
    S3 = topo_m3.ms["supplier3"]

    r1 = Replicas(S1).get(DEFAULT_SUFFIX)
    # 60 is the smallest accepted value for the interval time
    r1.replace('nsds5ReplicaKeepAliveUpdateInterval', '60')
    # Need to restart to reset repeated timer
    S1.restart()

    get_agmt(S1, "003").delete()
    get_agmt(S2, "001").delete()
    get_agmt(S3, "002").delete()
    perform_updates(S1, (S2, S3))
    S3.stop()
    ldif_file = '%s/supplier3.ldif' % S3.get_ldif_dir()
    S3.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX],
               excludeSuffixes=None, repl_data=True,
               outputfile=ldif_file, encrypt=False)
    perform_updates(S1, (S2,))
    S2.stop()
    S1.stop()
    S3.start()
    S1.ldif2db(DEFAULT_BENAME, None, None, None, ldif_file)
    S1.start()
    # wait more than 30s to update keep alive
    time.sleep(40)
    S2.start()
    perform_updates(S2, (S3,S1))
    check_updates(S1)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
