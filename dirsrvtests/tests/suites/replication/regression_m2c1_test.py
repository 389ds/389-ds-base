# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
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
from lib389.topologies import topology_m2c1 as topo_m2c1
from lib389._constants import DEFAULT_SUFFIX, DN_USERROOT_LDBM
from lib389.replica import Changelog5
from lib389.idm.user import TEST_USER_PROPERTIES, UserAccounts
from lib389.utils import ensure_bytes, ds_supports_new_changelog

pytestmark = pytest.mark.tier1

CHANGELOG = 'cn=changelog,{}'.format(DN_USERROOT_LDBM)
MAXAGE_ATTR = 'nsslapd-changelogmaxage'
MAXAGE_VALUE = '5'
TRIMINTERVAL = 'nsslapd-changelogtrim-interval'
TRIMINTERVAL_VALUE = '300'
MAX_USERS = 10

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def set_value(supplier, attr, val):
    """
    Helper function to add/replace attr: val and check the added value
    """
    try:
        supplier.modify_s(CHANGELOG, [(ldap.MOD_REPLACE, attr, ensure_bytes(val))])
    except ldap.LDAPError as e:
        log.error(f'Failed to add {attr}: {val} to {CHANGELOG}: error {str(e)}')
        assert False


def setup_max_age(supplier):
    """Configure logging and changelog max age
    """
    if ds_supports_new_changelog():
        set_value(supplier, MAXAGE_ATTR, MAXAGE_VALUE)
        set_value(supplier, TRIMINTERVAL, TRIMINTERVAL_VALUE)
    else:
        cl = Changelog5(supplier)
        cl.set_max_age(MAXAGE_VALUE)
        cl.set_trim_interval(TRIMINTERVAL_VALUE)


def test_changelog_trimming(topo_m2c1):
    """Test changelog trimming with replication

    :id: c0b357ed-07a4-4000-bbe0-2fbbf90232c9
    :setup: 2 Suppliers, 1 Consumer
    :steps:
        1. Configure changelog trimming (maxage=5 and interval=300)
        2. Creates updates on S1 and S2 (that will be also replicated to C1)
        3. Wait for maxage so any previous updates is older than maxage
        4. Pause (disable) the replica agreement S1->S2
        5. Do updates on S1, and add a TEST_ENTRY on S1
        6. Wait until we are sure trimming thread have completed
        7. Resume (enable) the replica agreement S1->S2
        8. Check that TEST_ENTRY is present on S1, S2 and C1
    :expectedresults:
        1. Changelog trimming should be configured successfully
        2. Updates should be created and replicated
        3. Wait should complete successfully
        4. Agreement should be paused successfully
        5. Updates and new entry should be added to S1
        6. Wait should complete successfully
        7. Agreement should be resumed successfully
        8. TEST_ENTRY should be present on all instances
    """

    S1 = topo_m2c1.ms["supplier1"]
    S2 = topo_m2c1.ms["supplier2"]
    C1 = topo_m2c1.cs["consumer1"]

    log.info("Configure changelog trimming")
    for supplier in (S1, S2):
        setup_max_age(supplier)

    log.info("Create updates on S1 and S2")
    users_s1 = UserAccounts(S1, DEFAULT_SUFFIX)
    users_s2 = UserAccounts(S2, DEFAULT_SUFFIX)
    
    for idx in range(1, MAX_USERS):
        user_properties = TEST_USER_PROPERTIES.copy()
        user_properties.update({'uid': f'user_{idx}'})
        users_s1.create(properties=user_properties)

    time.sleep(5)

    for idx in range(1, MAX_USERS):
        user = users_s2.get(f'user_{idx}')
        user.replace('description', 'value from S2')

    log.info("Wait for maxage")
    time.sleep(int(MAXAGE_VALUE))

    log.info("Pause replica agreement S1->S2")
    agreement_s1_s2 = S1.agreement.list(suffix=DEFAULT_SUFFIX, consumer_host=S2.host, consumer_port=S2.port)[0]
    S1.agreement.pause(agreement_s1_s2.dn)

    log.info("Do updates on S1 and add TEST_ENTRY")
    for idx in range(1, MAX_USERS):
        user = users_s1.get(f'user_{idx}')
        user.replace('description', 'value from S1')

    test_user_properties = TEST_USER_PROPERTIES.copy()
    test_user_properties.update({'uid': 'last_user'})
    test_user = users_s1.create(properties=test_user_properties)

    log.info("Wait for trimming thread to complete")
    time.sleep(int(TRIMINTERVAL_VALUE) + 10)

    log.info("Resume replica agreement S1->S2")
    S1.agreement.resume(agreement_s1_s2.dn)

    log.info("Check TEST_ENTRY on all instances")
    for instance in (S1, S2, C1):
        success = 0
        for i in range(0, 5):
            users = UserAccounts(instance, DEFAULT_SUFFIX)
            try:
                user = users.get('last_user')
            except ldap.NO_SUCH_OBJECT:
                time.sleep(1)
                continue
            log.info(f"User 'last_user' found on {instance.serverid}")
            success = 1
            break

        assert success, f"User 'last_user' not found on {instance.serverid}"


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
