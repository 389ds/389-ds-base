# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import logging
import os
import pytest
import subprocess
from lib389._constants import DEFAULT_SUFFIX
from lib389.dseldif import DSEldif
from lib389.replica import Changelog, ReplicationManager
from lib389.topologies import topology_m2

pytestmark = pytest.mark.tier1

CMD_OUTPUT = 'No issues found.'
JSON_OUTPUT = '[]'

log = logging.getLogger(__name__)


def run_healthcheck_and_check_result(instance, searched_code, json=False, isnot=False):
    """Run healthcheck and verify the expected code is in the output"""
    cmd = ['dsctl']
    if json:
        cmd.append('--json')
        if searched_code == CMD_OUTPUT:
            searched_code = JSON_OUTPUT
    cmd.append(instance.serverid)
    cmd.extend(['healthcheck', '--check', 'dseldif:nsstate'])

    result = subprocess.run(cmd, capture_output=True, universal_newlines=True)
    log.info(f'Running: {cmd}')
    log.info(f'Stdout: {result.stdout}')
    log.info(f'Stderr: {result.stderr}')
    log.info(f'Return code: {result.returncode}')
    stdout = result.stdout

    # stdout should not be empty
    assert stdout is not None
    assert len(stdout) > 0

    if isnot:
        assert searched_code not in stdout, \
            f'{searched_code} should NOT be in healthcheck output but was found'
        log.info(f'Verified {searched_code} is NOT in healthcheck output')
    else:
        assert searched_code in stdout, \
            f'{searched_code} should be in healthcheck output but was not found'
        log.info(f'Verified {searched_code} is in healthcheck output')


def test_healthcheck_time_skew_extensive(topology_m2):
    """Check if HealthCheck returns DSSKEWLE0003 and DSSKEWLE0004 codes for extensive time skew

    :id: 7591cd2b-8d66-4d33-9f8d-babff0571086
    :setup: Two suppliers replication topology
    :steps:
        1. Create a replicated topology
        2. Stop supplier1
        3. Increase time skew on supplier1 to over 24 hours
        4. Start supplier1
        5. Use HealthCheck and verify DSSKEWLE0003 is reported
        6. Set nsslapd-ignore-time-skew to on
        7. Use HealthCheck and verify DSSKEWLE0003 is NOT reported
        8. Stop supplier1
        9. Increase time skew on supplier1 to over 365 days
        10. Start supplier1
        11. Use HealthCheck and verify only DSSKEWLE0004 is reported
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Healthcheck reports DSSKEWLE0003 code
        6. Success
        7. Healthcheck does not report DSSKEWLE0003 code
        8. Success
        9. Success
        10. Success
        11. Healthcheck reports DSSKEWLE0004 code and not DSSKEWLE0003
    """

    M1 = topology_m2.ms['supplier1']
    M2 = topology_m2.ms['supplier2']

    # Ensure replication is working first
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(M1, M2)

    # Step 2-4: Stop supplier1, increase time skew to over 24 hours, start supplier1
    log.info('Stop supplier1 to modify dse.ldif')
    M1.stop()

    # Set time skew to over 24 hours (86400 seconds)
    # Add a margin to be safely over the threshold
    time_skew_24h = 86400 + 3600  # 25 hours
    log.info(f'Increase time skew on supplier1 by {time_skew_24h} seconds')
    DSEldif(M1)._increaseTimeSkew(DEFAULT_SUFFIX, time_skew_24h)

    log.info('Start supplier1')
    M1.start()

    # Step 5: Verify DSSKEWLE0003 is reported
    log.info('Run healthcheck and verify DSSKEWLE0003 is reported')
    run_healthcheck_and_check_result(M1, 'DSSKEWLE0003', json=False)
    run_healthcheck_and_check_result(M1, 'DSSKEWLE0003', json=True)

    # Step 6: Set nsslapd-ignore-time-skew to on
    log.info('Set nsslapd-ignore-time-skew to on')
    M1.config.set('nsslapd-ignore-time-skew', 'on')

    # Step 7: Verify DSSKEWLE0003 is NOT reported when ignoring time skew
    log.info('Run healthcheck and verify DSSKEWLE0003 is NOT reported')
    run_healthcheck_and_check_result(M1, 'DSSKEWLE0003', json=False, isnot=True)
    run_healthcheck_and_check_result(M1, 'DSSKEWLE0003', json=True, isnot=True)

    # Step 8-10: Stop supplier1, increase time skew to over 365 days, start supplier1
    log.info('Stop supplier1 to modify dse.ldif')
    M1.stop()

    # Increase time skew to over 365 days (31536000 seconds)
    # We need to add enough to go from current ~25 hours to over 365 days
    time_skew_year = (86400 * 365) + 86400  # 366 days total additional
    log.info(f'Increase time skew on supplier1 by {time_skew_year} seconds')
    DSEldif(M1)._increaseTimeSkew(DEFAULT_SUFFIX, time_skew_year)

    log.info('Start supplier1')
    M1.start()

    # Step 11: Verify only DSSKEWLE0004 is reported (not DSSKEWLE0003)
    log.info('Run healthcheck and verify only DSSKEWLE0004 is reported')
    run_healthcheck_and_check_result(M1, 'DSSKEWLE0004', json=False)
    run_healthcheck_and_check_result(M1, 'DSSKEWLE0004', json=True)
    run_healthcheck_and_check_result(M1, 'DSSKEWLE0003', json=False, isnot=True)
    run_healthcheck_and_check_result(M1, 'DSSKEWLE0003', json=True, isnot=True)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
