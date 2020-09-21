# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import os
import time
from datetime import *
from lib389.agreement import Agreements
from lib389.idm.user import UserAccounts
from lib389.utils import *
from lib389._constants import *
from lib389.cli_base import FakeArgs
from lib389.topologies import topology_m3
from lib389.cli_ctl.health import health_check_run
from lib389.paths import Paths


ds_paths = Paths()
pytestmark = pytest.mark.skipif(ds_paths.perl_enabled and (os.getenv('PYINSTALL') is None),
                                reason="These tests need to use python installer")

log = logging.getLogger(__name__)


def run_healthcheck_and_flush_log(topology, instance, searched_code, json, searched_code2=None):
    args = FakeArgs()
    args.instance = instance.serverid
    args.verbose = instance.verbose
    args.list_errors = False
    args.list_checks = False
    args.check = ['replication']
    args.dry_run = False

    if json:
        log.info('Use healthcheck with --json option')
        args.json = json
        health_check_run(instance, topology.logcap.log, args)
        assert topology.logcap.contains(searched_code)
        log.info('Healthcheck returned searched code: %s' % searched_code)

        if searched_code2 is not None:
            assert topology.logcap.contains(searched_code2)
            log.info('Healthcheck returned searched code: %s' % searched_code2)
    else:
        log.info('Use healthcheck without --json option')
        args.json = json
        health_check_run(instance, topology.logcap.log, args)
        assert topology.logcap.contains(searched_code)
        log.info('Healthcheck returned searched code: %s' % searched_code)

        if searched_code2 is not None:
            assert topology.logcap.contains(searched_code2)
            log.info('Healthcheck returned searched code: %s' % searched_code2)

    log.info('Clear the log')
    topology.logcap.flush()


# This test is in separate file because it is timeout specific
@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_replication_out_of_sync_not_broken(topology_m3):
    """Check if HealthCheck returns DSREPLLE0003 code

    :id: 8305000d-ba4d-4c00-8331-be0e8bd92150
    :setup: 3 MMR topology
    :steps:
        1. Create a 3 masters full-mesh topology, all replicas being synchronized
        2. Stop M1
        3. Perform an update on M2 and M3.
        4. Check M2 and M3 are synchronized.
        5. From M2, reinitialize the M3 agreement
        6. Stop M2 and M3
        7. Restart M1
        8. Start M3
        9. Use HealthCheck without --json option
        10. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Healthcheck reports DSREPLLE0003 code and related details
        10. Healthcheck reports DSREPLLE0003 code and related details
    """

    RET_CODE = 'DSREPLLE0003'

    M1 = topology_m3.ms['master1']
    M2 = topology_m3.ms['master2']
    M3 = topology_m3.ms['master3']

    log.info('Stop master1')
    M1.stop()

    log.info('Perform update on master2 and master3')
    test_users_m2 = UserAccounts(M2, DEFAULT_SUFFIX)
    test_users_m3 = UserAccounts(M3, DEFAULT_SUFFIX)
    test_users_m2.create_test_user(1000, 2000)
    for user_num in range(1001, 3000):
        test_users_m3.create_test_user(user_num, 2000)
    time.sleep(2)

    log.info('Stop M2 and M3')
    M2.stop()
    M3.stop()

    log.info('Start M1 first, then M2, so that M2 acquires M1')
    M1.start()
    M2.start()
    time.sleep(2)

    log.info('Start M3 which should not be able to acquire M1 since M2 is updating it')
    M3.start()
    time.sleep(2)

    run_healthcheck_and_flush_log(topology_m3, M3, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_m3, M3, RET_CODE, json=True)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
