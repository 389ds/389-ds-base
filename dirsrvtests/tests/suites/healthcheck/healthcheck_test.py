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

from lib389.backend import Backends
from lib389.mappingTree import MappingTrees
from lib389.replica import Changelog5
from lib389.utils import *
from lib389._constants import *
from lib389.cli_base import FakeArgs
from lib389.topologies import topology_st, topology_no_sample, topology_m2
from lib389.cli_ctl.health import health_check_run
from lib389.paths import Paths

CMD_OUTPUT = 'No issues found.'
JSON_OUTPUT = '[]'

ds_paths = Paths()
pytestmark = pytest.mark.skipif(ds_paths.perl_enabled and (os.getenv('PYINSTALL') is None),
                                reason="These tests need to use python installer")

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


def set_changelog_trimming(instance):
    log.info('Get the changelog enteries')
    inst_changelog = Changelog5(instance)

    log.info('Set nsslapd-changelogmaxage to 30d')
    inst_changelog.add('nsslapd-changelogmaxage', '30')


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

    run_healthcheck_and_flush_log(topology_st, standalone, CMD_OUTPUT,json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_standalone_tls(topology_st):
    """Check functionality of HealthCheck Tool on TLS enabled standalone instance with no errors

    :id: 4844b446-3939-4fbd-b14b-293b20bb8be0
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Enable TLS
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    standalone = topology_st.standalone
    standalone.enable_tls()

    run_healthcheck_and_flush_log(topology_st, standalone, CMD_OUTPUT,json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_replication(topology_m2):
    """Check functionality of HealthCheck Tool on replication instance with no errors

    :id: 9ee6d491-d6d7-4c2c-ac78-70d08f054166
    :setup: 2 MM topology
    :steps:
        1. Create a two masters replication topology
        2. Set nsslapd-changelogmaxage to 30d
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    M1 = topology_m2.ms['master1']
    M2 = topology_m2.ms['master2']

    # If we don't set changelog trimming, we will get error DSCLLE0001
    set_changelog_trimming(M1)
    set_changelog_trimming(M2)

    log.info('Run healthcheck for master1')
    run_healthcheck_and_flush_log(topology_m2, M1, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2, M1, JSON_OUTPUT, json=True)

    log.info('Run healthcheck for master2')
    run_healthcheck_and_flush_log(topology_m2, M2, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2, M2, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_replication_tls(topology_m2):
    """Check functionality of HealthCheck Tool on replication instance with no errors

    :id: 9ee6d491-d6d7-4c2c-ac78-70d08f054166
    :setup: 2 MM topology
    :steps:
        1. Create a two masters replication topology
        2. Enable TLS
        3. Set nsslapd-changelogmaxage to 30d
        4. Use HealthCheck without --json option
        5. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    M1 = topology_m2.ms['master1']
    M2 = topology_m2.ms['master2']

    M1.enable_tls()
    M2.enable_tls()

    log.info('Run healthcheck for master1')
    run_healthcheck_and_flush_log(topology_m2, M1, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2, M1, JSON_OUTPUT, json=True)

    log.info('Run healthcheck for master2')
    run_healthcheck_and_flush_log(topology_m2, M2, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2, M2, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1796343
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
@pytest.mark.xfail(reason="Will fail because of bz1837315. Set proper version after bug is fixed")
def test_healthcheck_unable_to_query_backend(topology_st):
    """Check if HealthCheck returns DSBLE0002 code

    :id: 716b1ff1-94bd-4780-98b8-96ff8ef21e30
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Create a new root suffix and database
        3. Disable new suffix
        4. Use HealthCheck without --json option
        5. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. HealthCheck should return code DSBLE0002
        5. HealthCheck should return code DSBLE0002
    """

    RET_CODE = 'DSBLE0002'
    NEW_SUFFIX = 'dc=test,dc=com'
    NEW_BACKEND = 'userData'

    standalone = topology_st.standalone

    backends = Backends(standalone)
    backends.create(properties={
        'cn': NEW_BACKEND,
        'nsslapd-suffix': NEW_SUFFIX,
    })

    mts = MappingTrees(standalone)
    mt_new = mts.get(NEW_SUFFIX)
    mt_new.replace('nsslapd-state', 'disabled')

    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
@pytest.mark.xfail(reason="Will fail because of bz1835619 and bz1837315. Set proper version after bugs are fixed")
def test_healthcheck_backend_missing_mapping_tree(topology_st):
    """Check if HealthCheck returns DSBLE0001 and DSBLE0003 code

    :id: 4c83ffcf-01a4-4ec8-a3d2-01022b566225
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Disable the dc=example,dc=com backend suffix entry in the mapping tree
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. Enable the dc=example,dc=com backend suffix entry in the mapping tree
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSBLE0001 and DSBLE0003 codes and related details
        4. Healthcheck reports DSBLE0001 and DSBLE0003 codes and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """

    RET_CODE1 = 'DSBLE0001'
    RET_CODE2 = 'DSBLE0003'

    standalone = topology_st.standalone

    log.info('Delete the dc=example,dc=com backend suffix entry in the mapping tree')
    mts = MappingTrees(standalone)
    mt = mts.get(DEFAULT_SUFFIX)
    mt.delete()

    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE1, json=False, searched_code2=RET_CODE2)
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE1, json=True, searched_code2=RET_CODE2)

    log.info('Create the dc=example,dc=com backend suffix entry')
    mts.create(properties={
        'cn': DEFAULT_SUFFIX,
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRoot',
    })

    run_healthcheck_and_flush_log(topology_st, standalone, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1796343
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_database_not_initialized(topology_no_sample):
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

    RET_CODE = 'DSBLE0003'
    standalone = topology_no_sample.standalone

    run_healthcheck_and_flush_log(topology_no_sample, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_no_sample, standalone, RET_CODE, json=True)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
