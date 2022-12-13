# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import os
import subprocess
import distro


from datetime import *
from lib389.config import Encryption
from lib389.utils import *
from lib389._constants import *
from lib389.cli_base import FakeArgs
from lib389.topologies import topology_st
from lib389.cli_ctl.health import health_check_run
from lib389.paths import Paths

CMD_OUTPUT = 'No issues found.'
JSON_OUTPUT = '[]'

ds_paths = Paths()
pytestmark = pytest.mark.tier1

libfaketime = pytest.importorskip('libfaketime')
libfaketime.reexec_if_needed()

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def run_healthcheck_and_flush_log(topology, instance, searched_code, json, searched_code2=None):
    args = FakeArgs()
    args.instance = instance.serverid
    args.verbose = instance.verbose
    args.list_errors = False
    args.list_checks = False
    args.check = None
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


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_insecure_pwd_hash_configured(topology_st):
    """Check if HealthCheck returns DSCLE0002 code

    :id: 6baf949c-a5eb-4f4e-83b4-8302e677758a
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Configure an insecure passwordStorageScheme (as SHA) for the instance
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. Set passwordStorageScheme and nsslapd-rootpwstoragescheme to PBKDF2-SHA512
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSCLE0002 code and related details
        4. Healthcheck reports DSCLE0002 code and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """

    RET_CODE = 'DSCLE0002'

    standalone = topology_st.standalone

    log.info('Configure an insecure passwordStorageScheme (SHA)')
    standalone.config.set('passwordStorageScheme', 'SHA')

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    if is_fips():
        log.info('Set passwordStorageScheme and nsslapd-rootpwstoragescheme to SSHA512 in FIPS mode')
        standalone.config.set('passwordStorageScheme', 'SSHA512')
        standalone.config.set('nsslapd-rootpwstoragescheme', 'SSHA512')
    else:
        log.info('Set passwordStorageScheme and nsslapd-rootpwstoragescheme to PBKDF2_SHA256')
        standalone.config.set('passwordStorageScheme', 'PBKDF2_SHA256')
        standalone.config.set('nsslapd-rootpwstoragescheme', 'PBKDF2_SHA256')

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_min_allowed_tls_version_too_low(topology_st):
    """Check if HealthCheck returns DSELE0001 code

    :id: a4be3390-9508-4827-8f82-e4e21081caab
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Set the TLS minimum version to TLS1.0
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. Set the TLS minimum version to TLS1.2
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSELE0001 code and related details
        4. Healthcheck reports DSELE0001 code and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """

    RET_CODE = 'DSELE0001'
    HIGHER_VS = 'TLS1.2'
    SMALL_VS = 'TLS1.0'
    RHEL = 'Red Hat Enterprise Linux'

    standalone = topology_st.standalone

    standalone.enable_tls()

    # We have to update-crypto-policies to LEGACY, otherwise we can't set TLS1.0
    log.info('Updating crypto policies')
    assert subprocess.check_call(['update-crypto-policies', '--set', 'LEGACY']) == 0

    log.info('Set the TLS minimum version to TLS1.0')
    enc = Encryption(standalone)
    enc.replace('sslVersionMin', SMALL_VS)
    standalone.restart()

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info('Set the TLS minimum version to TLS1.2')
    enc.replace('sslVersionMin', HIGHER_VS)
    standalone.restart()

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)

    if RHEL in distro.linux_distribution():
        log.info('Set crypto-policies back to DEFAULT')
        assert subprocess.check_call(['update-crypto-policies', '--set', 'DEFAULT']) == 0


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_resolvconf_bad_file_perm(topology_st):
    """Check if HealthCheck returns DSPERMLE0001 code

    :id: 8572b9e9-70e7-49e9-b745-864f6f2468a8
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Change the /etc/resolv.conf file permissions to 444
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. set /etc/resolv.conf permissions to 644
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSPERMLE0001 code and related details
        4. Healthcheck reports DSPERMLE0001 code and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """

    RET_CODE = 'DSPERMLE0001'

    standalone = topology_st.standalone

    log.info('Change the /etc/resolv.conf file permissions to 444')
    os.chmod('/etc/resolv.conf', 0o444)

    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)

    log.info('Change the /etc/resolv.conf file permissions to 644')
    os.chmod('/etc/resolv.conf', 0o644)

    run_healthcheck_and_flush_log(topology_st, standalone, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_pwdfile_bad_file_perm(topology_st):
    """Check if HealthCheck returns DSPERMLE0002 code

    :id: ec137d66-bad6-4eed-90bd-fc1d572bbe1f
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Change the /etc/dirsrv/slapd-xxx/pwdfile.txt permissions to 000
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. Change the /etc/dirsrv/slapd-xxx/pwdfile.txt permissions to 400
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSPERMLE0002 code and related details
        4. Healthcheck reports DSPERMLE0002 code and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """

    RET_CODE = 'DSPERMLE0002'

    standalone = topology_st.standalone
    cert_dir = standalone.ds_paths.cert_dir

    log.info('Change the /etc/dirsrv/slapd-{}/pwdfile.txt permissions to 000'.format(standalone.serverid))
    os.chmod('{}/pwdfile.txt'.format(cert_dir), 0o000)

    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)

    log.info('Change the /etc/dirsrv/slapd-{}/pwdfile.txt permissions to 400'.format(standalone.serverid))
    os.chmod('{}/pwdfile.txt'.format(cert_dir), 0o400)

    run_healthcheck_and_flush_log(topology_st, standalone, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_certif_expiring_within_30d(topology_st):
    """Check if HealthCheck returns DSCERTLE0001 code

    :id: c2165032-88ba-4978-a4ca-2fecfd8c35d8
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Use libfaketime to tell the process the date is within 30 days before certificate expiration
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSCERTLE0001 code and related details
        4. Healthcheck reports DSCERTLE0001 code and related details
    """

    RET_CODE = 'DSCERTLE0001'

    standalone = topology_st.standalone

    standalone.enable_tls()

    # Cert is valid two years from today, so we count the date that is within 30 days before certificate expiration
    date_future = datetime.now() + timedelta(days=701)

    with libfaketime.fake_time(date_future):
        run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
        run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)

    # Try again with real time just to make sure no issues were found
    run_healthcheck_and_flush_log(topology_st, standalone, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_certif_expired(topology_st):
    """Check if HealthCheck returns DSCERTLE0002 code

    :id: ceff2c22-62c0-4fd9-b737-930a88458d68
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Use libfaketime to tell the process the date is after certificate expiration
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSCERTLE0002 code and related details
        4. Healthcheck reports DSCERTLE0002 code and related details
    """

    RET_CODE = 'DSCERTLE0002'

    standalone = topology_st.standalone

    standalone.enable_tls()

    # Cert is valid two years from today, so we count the date that is after expiration
    date_future = datetime.now() + timedelta(days=731)

    with libfaketime.fake_time(date_future):
        run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
        run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)

    # Try again with real time just to make sure no issues were found
    run_healthcheck_and_flush_log(topology_st, standalone, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, JSON_OUTPUT, json=True)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
