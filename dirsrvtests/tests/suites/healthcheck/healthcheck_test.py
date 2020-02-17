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
import subprocess
from lib389.utils import *
from lib389._constants import *
from lib389.cli_base import FakeArgs
from lib389.topologies import topology_st, topology_no_sample
from lib389.cli_ctl.health import health_check_run
from lib389.paths import Paths


ds_paths = Paths()
pytestmark = pytest.mark.skipif(ds_paths.perl_enabled and (os.getenv('PYINSTALL') is None),
                                reason="These tests need to use python installer")

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_standalone(topology_st):
    """Check functionality of HealthCheck Tool on standalone instance with no errors

    :id: 4844b446-3939-4fbd-b14b-293b20bb8be0
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Use HealthCheck without --json option
        3. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    cmd_output = 'No issues found.'
    json_ouput = '[]'

    args = FakeArgs()
    args.instance = standalone.serverid
    args.verbose = standalone.verbose

    log.info("Use healthcheck without --json option")
    args.json = False
    health_check_run(standalone, topology_st.logcap.log, args)
    assert topology_st.logcap.contains(cmd_output)

    log.info('Use healthcheck with --json option')
    args.json = True
    health_check_run(standalone, topology_st.logcap.log, args)
    assert topology_st.logcap.contains(json_ouput)


@pytest.mark.ds50873
@pytest.mark.bz1796343
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_health_check_database_not_initialized(topology_no_sample):
    """Check if HealthCheck returns DSBLE0003 code

    :id: 716b1ff1-94bd-4780-98b8-96ff8ef21e30
    :setup: Standalone instance
    :steps:
        1. Create DS instance without example entries
        2. Use HealthCheck without --json option
        3. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. HealthCheck should return code DSBLE0003
        3. HealthCheck should return code DSBLE0003
    """

    ret_code = 'DSBLE0003'
    standalone = topology_no_sample.standalone

    args = FakeArgs()
    args.instance = standalone.serverid
    args.verbose = standalone.verbose

    log.info("Use healthcheck without --json option")
    args.json = False
    health_check_run(standalone, topology_no_sample.logcap.log, args)
    assert topology_no_sample.logcap.contains(ret_code)
    log.info("HealthCheck returned DSBLE0003")

    log.info('Use healthcheck with --json option')
    args.json = True
    health_check_run(standalone, topology_no_sample.logcap.log, args)
    assert topology_no_sample.logcap.contains(ret_code)
    log.info("HealthCheck with --json argument returned DSBLE0003")


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_replication(request):
    """Check functionality of HealthCheck Tool on replication instance with no errors

    :id: 9ee6d491-d6d7-4c2c-ac78-70d08f054166
    :setup: 2 MM topology
    :steps:
        1. Create a two masters replication topology
        2. Use HealthCheck without --json option
        3. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_backend_missing_mapping_tree(request):
    """Check if HealthCheck returns DSBLE0001 and DSBLE0002 code

    :id: 4c83ffcf-01a4-4ec8-a3d2-01022b566225
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
        2. Disable the dc=example,dc=com backend suffix entry in the mapping tree
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. Enable the dc=example,dc=com backend suffix entry in the mapping tree
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSBLE0001 and DSBLE0002 codes and related details
        4. Healthcheck reports DSBLE0001 and DSBLE0002 codes and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_virtual_attr_incorrectly_indexed(request):
    """Check if HealthCheck returns DSVIRTLE0001 code

    :id: 1055173b-21aa-4aaa-9e91-4dc6c5e0c01f
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
        2. Create a CoS definition entry
        3. Create the matching CoS template entry, with postalcode as virtual attribute
        4. Create an index for postalcode
        5. Use HealthCheck without --json option
        6. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Healthcheck reports DSVIRTLE0001 code and related details
        6. Healthcheck reports DSVIRTLE0001 code and related details
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_logging_format_should_be_revised(request):
    """Check if HealthCheck returns DSCLE0001 code

    :id: 277d7980-123b-481b-acba-d90921b9f5ac
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
        2. Set nsslapd-logging-hr-timestamps-enabled to ‘off’
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. Set nsslapd-logging-hr-timestamps-enabled to ‘on’
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSCLE0001 code and related details
        4. Healthcheck reports DSCLE0001 code and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_insecure_pwd_hash_configured(request):
    """Check if HealthCheck returns DSCLE0002 code

    :id: 6baf949c-a5eb-4f4e-83b4-8302e677758a
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
        2. Configure an insecure passwordStorageScheme (as SHA) for the instance
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. Set passwordStorageScheme and nsslapd-rootpwstoragescheme to PBKDF2_SHA256
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


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_min_allowed_tls_version_too_low(request):
    """Check if HealthCheck returns DSELE0001 code

    :id: a4be3390-9508-4827-8f82-e4e21081caab
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
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


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_RI_plugin_is_misconfigured(request):
    """Check if HealthCheck returns DSRILE0001 code

    :id: de2e90a2-89fe-472c-acdb-e13cbca5178d
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
        2. Configure the instance with Integrity Plugin
        3. Set the referint-update-delay attribute of the RI plugin, to a value upper than 0
        4. Use HealthCheck without --json option
        5. Use HealthCheck with --json option
        6. Set the referint-update-delay attribute to 0
        7. Use HealthCheck without --json option
        8. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Healthcheck reports DSRILE0001 code and related details
        5. Healthcheck reports DSRILE0001 code and related details
        6. Success
        7. Healthcheck reports no issue found
        8. Healthcheck reports no issue found
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_RI_plugin_missing_indexes(request):
    """Check if HealthCheck returns DSRILE0002 code

    :id: 05c55e37-bb3e-48d1-bbe8-29c980f94f10
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
        2. Configure the instance with Integrity Plugin
        3. Change the index type of the member attribute index to ‘approx’
        4. Use HealthCheck without --json option
        5. Use HealthCheck with --json option
        6. Set the index type of the member attribute index to ‘eq’
        7. Use HealthCheck without --json option
        8. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Healthcheck reports DSRILE0002 code and related details
        5. Healthcheck reports DSRILE0002 code and related details
        6. Success
        7. Healthcheck reports no issue found
        8. Healthcheck reports no issue found
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_replication_out_of_sync_broken(request):
    """Check if HealthCheck returns DSREPLLE0001 code

    :id: b5ae7cae-de0f-4206-95a4-f81538764bea
    :setup: 3 MMR topology
    :steps:
        1. Create a 3 masters full-mesh topology, on M2 and M3 don’t set nsds5BeginReplicaRefresh:start
        2. Perform modifications on M1
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSREPLLE0001 code and related details
        4. Healthcheck reports DSREPLLE0001 code and related details
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_replication_presence_of_conflict_entries(request):
    """Check if HealthCheck returns DSREPLLE0002 code

    :id: 43abc6c6-2075-42eb-8fa3-aa092ff64cba
    :setup: Replicated topology
    :steps:
        1. Create a replicated topology
        2. Create conflict entries : different entries renamed to the same dn
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSREPLLE0002 code and related details
        4. Healthcheck reports DSREPLLE0002 code and related details
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_replication_out_of_sync_not_broken(request):
    """Check if HealthCheck returns DSREPLLE0003 code

    :id: 8305000d-ba4d-4c00-8331-be0e8bd92150
    :setup: 3 MMR topology
    :steps:
        1. Create a 3 masters full-mesh topology, all replicas being synchronized
        2. stop M1
        3. Perform an update on M2 and M3.
        4. Check M2 and M3 are synchronized.
        5. From M2, reinitialize the M3 agreement
        6. Stop M2
        7. Restart M1
        8. Use HealthCheck without --json option
        9. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Healthcheck reports DSREPLLE0003 code and related details
        9. Healthcheck reports DSREPLLE0003 code and related details
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_replication_presence_of_conflict_entries(request):
    """Check if HealthCheck returns DSREPLLE0005 code

    :id: d452a564-7b82-4c1a-b331-a71abbd82a10
    :setup: Replicated topology
    :steps:
        1. Create a replicated topology
        2. On M1, set nsds5replicaport for the replication agreement to an unreachable port on the replica
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. On M1, set nsds5replicaport for the replication agreement to a reachable port number
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSREPLLE0005 code and related details
        4. Healthcheck reports DSREPLLE0005 code and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_changelog_trimming_not_configured(request):
    """Check if HealthCheck returns DSCLLE0001 code

    :id: c2165032-88ba-4978-a4ca-2fecfd8c35d8
    :setup: Replicated topology
    :steps:
        1. Create a replicated topology
        2. On M1, remove nsslapd-changelogmaxage from cn=changelog5,cn=config
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. On M1, set nsslapd-changelogmaxage to 30d
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSCLLE0001 code and related details
        4. Healthcheck reports DSCLLE0001 code and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_certif_expiring_within_30d(request):
    """Check if HealthCheck returns DSCERTLE0001 code

    :id: c2165032-88ba-4978-a4ca-2fecfd8c35d8
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
        2. Use libfaketime to tell the process the date is within 30 days before certificate expiration
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSCERTLE0001 code and related details
        4. Healthcheck reports DSCERTLE0001 code and related details
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_certif_expired(request):
    """Check if HealthCheck returns DSCERTLE0002 code

    :id: ceff2c22-62c0-4fd9-b737-930a88458d68
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
        2. Use libfaketime to tell the process the date is after certificate expiration
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSCERTLE0002 code and related details
        4. Healthcheck reports DSCERTLE0002 code and related details
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_low_disk_space(request):
    """Check if HealthCheck returns DSDSLE0001 code

    :id: 144b335d-077e-430c-9c0e-cd6b0f2f73c1
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
        2. Get the free disk space for /
        3. Use fallocate -l to create a file large enough for the use % be up 90%
        4. Use HealthCheck without --json option
        5. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. free disk space value
        3. Success
        3. Healthcheck reports DSDSLE0001 code and related details
        4. Healthcheck reports DSDSLE0001 code and related details
    """


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_resolvconf_bad_file_perm(request):
    """Check if HealthCheck returns DSPERMLE0001 code

    :id: 8572b9e9-70e7-49e9-b745-864f6f2468a8
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
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


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skip(reason="Not implemented")
def test_healthcheck_security_bad_file_perm(request):
    """Check if HealthCheck returns DSPERMLE0002 code

    :id: ec137d66-bad6-4eed-90bd-fc1d572bbe1f
    :setup: Standalone instance
    :steps:
        1. Create DS instance from template file
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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
