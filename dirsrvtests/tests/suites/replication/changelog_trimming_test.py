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
from lib389._constants import *
from lib389.properties import *
from lib389.topologies import topology_m1 as topo
from lib389.topologies import topology_m1c1
from lib389.tasks import *
from lib389.replica import Changelog5
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccounts
from lib389.utils import ensure_bytes, ds_supports_new_changelog
from lib389.replica import ReplicationManager
from lib389._constants import DN_LDBM

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def do_mods(supplier, num):
    """Perform a num of mods on the default suffix
    """
    domain = Domain(supplier, DEFAULT_SUFFIX)
    for i in range(num):
        domain.replace('description', 'change %s' % i)

@pytest.fixture(scope="module")
def setup_max_entries(topo, request):
    """Configure logging and changelog max entries
    """
    supplier = topo.ms["supplier1"]

    supplier.config.loglevel((ErrorLog.REPLICA,), 'error')

    if ds_supports_new_changelog():
        set_value(supplier, MAXENTRIES, '2')
        set_value(supplier, TRIMINTERVAL, '300')
    else:
        cl = Changelog5(supplier)
        cl.set_trim_interval('300')

@pytest.fixture(scope="module")
def setup_max_age(topo, request):
    """Configure logging and changelog max age
    """
    supplier = topo.ms["supplier1"]
    supplier.config.loglevel((ErrorLog.REPLICA,), 'error')

    if ds_supports_new_changelog():
        set_value(supplier, MAXAGE, '5')
        set_value(supplier, TRIMINTERVAL, '300')
    else:
        cl = Changelog5(supplier)
        cl.set_max_age('5')
        cl.set_trim_interval('300')

def test_max_age(topo, setup_max_age):
    """Test changing the trimming interval works with max age

    :id: b5de04a5-4d92-49ea-a725-1d278a1c647c
    :setup: single supplier
    :steps:
        1. Perform modification to populate changelog
        2. Adjust the changelog trimming interval
        3. Check is trimming occurrs within the new interval

    :expectedresults:
        1. Modifications are successful
        2. The changelog trimming interval is correctly lowered
        3. Trimming occurs

    """
    log.info("Testing changelog triming interval with max age...")

    supplier = topo.ms["supplier1"]
    if not ds_supports_new_changelog():
        cl = Changelog5(supplier)

    # Do mods to build if cl entries
    do_mods(supplier, 10)

    time.sleep(1)  # Trimming should not have occurred
    if supplier.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    if ds_supports_new_changelog():
        set_value(supplier, TRIMINTERVAL, '5')
    else:
        cl.set_trim_interval('5')

    time.sleep(3)  # Trimming should not have occurred
    if supplier.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    time.sleep(3)  # Trimming should have occurred
    if supplier.searchErrorsLog("Trimmed") is False:
        log.fatal('Trimming event did not occur')
        assert False


def test_max_entries(topo, setup_max_entries):
    """Test changing the trimming interval works with max entries

    :id: b5de04a5-4d92-49ea-a725-1d278a1c647d
    :setup: single supplier
    :steps:
        1. Perform modification to populate changelog
        2. Adjust the changelog trimming interval
        3. Check is trimming occurrs within the new interval

    :expectedresults:
        1. Modifications are successful
        2. The changelog trimming interval is correctly lowered
        3. Trimming occurs

    """

    log.info("Testing changelog triming interval with max entries...")
    supplier = topo.ms["supplier1"]
    if not ds_supports_new_changelog():
        cl = Changelog5(supplier)

    # reset errors log
    supplier.deleteErrorLogs()

    # Do mods to build if cl entries
    do_mods(supplier, 10)

    time.sleep(1)  # Trimming should have occurred
    if supplier.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    if ds_supports_new_changelog():
        set_value(supplier, TRIMINTERVAL, '5')
    else:
        cl.set_trim_interval('5')

    time.sleep(6)  # Trimming should have occurred
    if supplier.searchErrorsLog("Trimmed") is False:
        log.fatal('Trimming event did not occur')
        assert False

def test_cl_trim_ignore_empty_ruv_element(topology_m1c1):
    """Test trimming is not done on RID not yet replicated

    :id: b4b42d8d-dfd1-482e-aca4-8f484761c541
    :setup: single supplier and single consumer
    :steps:
        1. Initialize S1 with new dataset (S1 and C1 are out of sync)
        2. Export LDIF the replicated dataset from S1. This LDIF does contain an empty RUV
        3. Do a dummy update on S1 (still not replicated to C1)
        4. Stop S1 so that it will not try to update C1
        5. Reinit C1 from LDIF from step 2
        6. Switch to readonly mode the default backend so that it will NOT apply any change (direct or replicated)
        7. Start S1, as C1 is in readlonly
        8. Adjust trim interval/maxage on S1
        9. Wait for maxage+5s for the trimming to occur
        10. Switch to not readlonly the default backend so that it will apply updates sent by S1
        11. Check that C1 catch up and a new update is also replicated
        12. Check S1 had not trim the starting point CSN as we can not find the message "Can't locate CSN"

    :expectedresults:
        1. successfull export/import on S1
        2. successfull export on S1
        3. successfull update
        4. success
        5. successfull init of C1
        6. successfull enable readonly
        7. success
        8. successfull setting for trimming
        9. success
        10. successfull disable readonly
        11. success
        12. message not found

    """
    log.info("Test trimming is not done on RID not yet replicated...")

    s1 = topology_m1c1.ms['supplier1']
    s1_tasks = Tasks(s1)
    s1_tasks.log = log
    c1 = topology_m1c1.cs['consumer1']
    c1_tasks = Tasks(c1)
    c1_tasks.log = log

    # Step 1 - reinit (export/import) S1 with an empty RUV
    ldif_file = "{}/export.ldif".format(s1.get_ldif_dir())
    args = {EXPORT_REPL_INFO: False, # to get a ldif without RUV
            TASK_WAIT: True}
    s1_tasks.exportLDIF(DEFAULT_SUFFIX, None, ldif_file, args)
    args = {TASK_WAIT: True}
    s1_tasks.importLDIF(DEFAULT_SUFFIX, None, ldif_file, args)

    # Step 2 - At this point S1 and C1 are out of sync as we reinited S1
    # with new dataset
    # Export the DB (with empty RUV) to be used to reinit C1
    ldif_file = "{}/export_empty_ruv.ldif".format(s1.get_ldif_dir())
    args = {EXPORT_REPL_INFO: True,
            TASK_WAIT: True}
    s1_tasks.exportLDIF(DEFAULT_SUFFIX, None, ldif_file, args)

    # - Step 3 - Dummy update on s1 to kick the RUV
    test_users_s1 = UserAccounts(s1, DEFAULT_SUFFIX, rdn=None)
    user_1 = test_users_s1.create_test_user(uid=1000)

    # - Step 4 - Stop supplier1 to prevent it to update C1 once this one
    s1.stop()

    # - Step 5 - reinit C1 with empty RUV ldif file
    args = {TASK_WAIT: True}
    c1_tasks.importLDIF(DEFAULT_SUFFIX, None, ldif_file, args)

    # - Step 6 - Put default backend of C1 into read only mode
    c1.modify_s('cn=%s,%s' % (DEFAULT_BENAME, DN_LDBM), [(ldap.MOD_REPLACE, 'nsslapd-readonly', b'on')])

    # - Step 7 - Now it is safe to start S1, no update will apply on C1
    # and C1 RUV will stay empty
    s1.start()

    # - Step 8 - set Triming interval to 2sec to have frequent trimming
    # and max-age to 8sec to not wait too long
    if ds_supports_new_changelog():
        set_value(s1, MAXAGE, '8')
        set_value(s1, TRIMINTERVAL, '2')
    else:
        cl = Changelog5(s1)
        cl.set_max_age('8')
        cl.set_trim_interval('2')

    # - Step 9 - Wait for the trimming to occur
    time.sleep(30)

    # - Step 10 - disable read-only mode on the default backend of C1
    c1.modify_s('cn=%s,%s' % (DEFAULT_BENAME, DN_LDBM), [(ldap.MOD_REPLACE, 'nsslapd-readonly', b'off')])

    # - Step 11 - Check that M1 and M2 are in sync
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(s1, c1, timeout=20)

    # Recheck after another update
    user_2 = test_users_s1.create_test_user(uid=2000)
    repl.wait_for_replication(s1, c1, timeout=20)

    # - Step 12 - check in infamous message when a CSN
    # is missing from the changelog
    assert not s1.ds_error_log.match('.*Can.t locate CSN}.*')

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
