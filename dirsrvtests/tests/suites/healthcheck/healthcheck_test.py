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
from lib389.replica import Changelog5,  Changelog
from lib389.utils import *
from lib389._constants import *
from lib389.cli_base import FakeArgs
from lib389.topologies import topology_st, topology_no_sample, topology_m2
from lib389.cli_ctl.health import health_check_run
from lib389.paths import Paths

CMD_OUTPUT = 'No issues found.'
JSON_OUTPUT = '[]'
CHANGELOG = 'cn=changelog,{}'.format(DN_USERROOT_LDBM)

ds_paths = Paths()
log = logging.getLogger(__name__)


def run_healthcheck_and_flush_log(topology, instance, searched_code=None, json=False, searched_code2=None,
                                  list_checks=False, list_errors=False, check=None, searched_list=None):
    args = FakeArgs()
    args.instance = instance.serverid
    args.verbose = instance.verbose
    args.list_errors = list_errors
    args.list_checks = list_checks
    args.check = check
    args.dry_run = False
    args.json = json

    log.info('Use healthcheck with --json == {} option'.format(json))
    health_check_run(instance, topology.logcap.log, args)

    if searched_list is not None:
        for item in searched_list:
            assert topology.logcap.contains(item)
            log.info('Healthcheck returned searched item: %s' % item)
    else:
        assert topology.logcap.contains(searched_code)
        log.info('Healthcheck returned searched code: %s' % searched_code)

    if searched_code2 is not None:
        assert topology.logcap.contains(searched_code2)
        log.info('Healthcheck returned searched code: %s' % searched_code2)

    log.info('Clear the log')
    topology.logcap.flush()


def set_changelog_trimming(instance):
    log.info('Set nsslapd-changelogmaxage to 30d')

    if ds_supports_new_changelog():
        cl = Changelog(instance, DEFAULT_SUFFIX)
    else:
        cl = Changelog5(instance)
    cl.replace('nsslapd-changelogmaxage', '30')


def test_healthcheck_disabled_suffix(topology_st):
    """Test that we report when a suffix is disabled

    :id: 49ebce72-7e7b-4eff-8bd9-8384d12251b4
    :setup: Standalone Instance
    :steps:
        1. Disable suffix
        2. Use HealthCheck without --json option
        3. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. HealthCheck should return code DSBLE0002
        3. HealthCheck should return code DSBLE0002
    """

    RET_CODE = 'DSBLE0002'

    mts = MappingTrees(topology_st.standalone)
    mt = mts.get(DEFAULT_SUFFIX)
    mt.replace("nsslapd-state", "disabled")

    run_healthcheck_and_flush_log(topology_st, topology_st.standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st, topology_st.standalone, RET_CODE, json=True)

    # reset the suffix state
    mt.replace("nsslapd-state", "backend")


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skipif(ds_is_older("1.4.1"), reason="Not implemented")
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


@pytest.mark.ds50746
@pytest.mark.bz1816851
@pytest.mark.xfail(ds_is_older("1.4.2"), reason="Not implemented")
def test_healthcheck_list_checks(topology_st):
    """Check functionality of HealthCheck Tool with --list-checks option

    :id: 44b1d8d3-b94a-4c2d-9233-ebe876802803
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Set list_checks to True
        3. Run HealthCheck
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    output_list = ['config:hr_timestamp',
                   'config:passwordscheme',
                   'backends:userroot:cl_trimming',
                   'backends:userroot:mappingtree',
                   'backends:userroot:search',
                   'backends:userroot:virt_attrs',
                   'encryption:check_tls_version',
                   'fschecks:file_perms',
                   'refint:attr_indexes',
                   'refint:update_delay',
                   'monitor-disk-space:disk_space',
                   'replication:agmts_status',
                   'replication:conflicts',
                   'dseldif:nsstate',
                   'tls:certificate_expiration',
                   'logs:notes']

    standalone = topology_st.standalone

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, list_checks=True, searched_list=output_list)


@pytest.mark.ds50746
@pytest.mark.bz1816851
@pytest.mark.xfail(ds_is_older("1.4.2"), reason="Not implemented")
def test_healthcheck_list_errors(topology_st):
    """Check functionality of HealthCheck Tool with --list-errors option

    :id: 295c07c0-a939-4d5e-b3a6-b4c9d0da3897
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Set list_errors to True
        3. Run HealthCheck
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    output_list = ['DSBLE0001 :: Possibly incorrect mapping tree',
                   'DSBLE0002 :: Unable to query backend',
                   'DSBLE0003 :: Uninitialized backend database',
                   'DSCERTLE0001 :: Certificate about to expire',
                   'DSCERTLE0002 :: Certificate expired',
                   'DSCLE0001 :: Different log timestamp format',
                   'DSCLE0002 :: Weak passwordStorageScheme',
                   'DSCLLE0001 :: Changelog trimming not configured',
                   'DSDSLE0001 :: Low disk space',
                   'DSELE0001 :: Weak TLS protocol version',
                   'DSLOGNOTES0001 :: Unindexed Search',
                   'DSLOGNOTES0002 :: Unknown Attribute In Filter',
                   'DSPERMLE0001 :: Incorrect file permissions',
                   'DSPERMLE0002 :: Incorrect security database file permissions',
                   'DSREPLLE0001 :: Replication agreement not set to be synchronized',
                   'DSREPLLE0002 :: Replication conflict entries found',
                   'DSREPLLE0003 :: Unsynchronized replication agreement',
                   'DSREPLLE0004 :: Unable to get replication agreement status',
                   'DSREPLLE0005 :: Replication consumer not reachable',
                   'DSRILE0001 :: Referential integrity plugin may be slower',
                   'DSRILE0002 :: Referential integrity plugin configured with unindexed attribute',
                   'DSSKEWLE0001 :: Medium time skew',
                   'DSSKEWLE0002 :: Major time skew',
                   'DSSKEWLE0003 :: Extensive time skew',
                   'DSVIRTLE0001 :: Virtual attribute indexed']

    standalone = topology_st.standalone

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, list_errors=True, searched_list=output_list)


@pytest.mark.ds50746
@pytest.mark.bz1816851
@pytest.mark.xfail(ds_is_older("1.4.2"), reason="Not implemented")
def test_healthcheck_check_option(topology_st):
    """Check functionality of HealthCheck Tool with --check option

    :id: ee382d6f-8bec-4236-ace4-4700d19dc9fd
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Set check to value from list
        3. Run HealthCheck
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    output_list = ['config:hr_timestamp',
                   'config:passwordscheme',
                   'backends:userroot:cl_trimming',
                   'backends:userroot:mappingtree',
                   'backends:userroot:search',
                   'backends:userroot:virt_attrs',
                   'encryption:check_tls_version',
                   'fschecks:file_perms',
                   'refint:attr_indexes',
                   'refint:update_delay',
                   'monitor-disk-space:disk_space',
                   'replication:agmts_status',
                   'replication:conflicts',
                   'dseldif:nsstate',
                   'tls:certificate_expiration',
                   'logs:notes']

    standalone = topology_st.standalone

    for item in output_list:
        pattern = 'Checking ' + item
        log.info('Check {}'.format(item))
        run_healthcheck_and_flush_log(topology_st, standalone, searched_code=pattern, json=False, check=[item],
                                      searched_code2=CMD_OUTPUT)
        run_healthcheck_and_flush_log(topology_st, standalone, searched_code=JSON_OUTPUT, json=True, check=[item])


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skipif(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_standalone_tls(topology_st):
    """Check functionality of HealthCheck Tool on TLS enabled standalone instance with no errors

    :id: 832374e6-6d2c-42af-80c8-d3685dbfa234
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
@pytest.mark.skipif(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_replication(topology_m2):
    """Check functionality of HealthCheck Tool on replication instance with no errors

    :id: d7751cc3-271c-4c33-b296-8a4c8941233e
    :setup: 2 MM topology
    :steps:
        1. Create a two suppliers replication topology
        2. Set nsslapd-changelogmaxage to 30d
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    M1 = topology_m2.ms['supplier1']
    M2 = topology_m2.ms['supplier2']

    # If we don't set changelog trimming, we will get error DSCLLE0001
    set_changelog_trimming(M1)
    set_changelog_trimming(M2)

    log.info('Run healthcheck for supplier1')
    run_healthcheck_and_flush_log(topology_m2, M1, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2, M1, JSON_OUTPUT, json=True)

    log.info('Run healthcheck for supplier2')
    run_healthcheck_and_flush_log(topology_m2, M2, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2, M2, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skipif(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_replication_tls(topology_m2):
    """Check functionality of HealthCheck Tool on replication instance with no errors

    :id: 9ee6d491-d6d7-4c2c-ac78-70d08f054166
    :setup: 2 MM topology
    :steps:
        1. Create a two suppliers replication topology
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

    M1 = topology_m2.ms['supplier1']
    M2 = topology_m2.ms['supplier2']

    M1.enable_tls()
    M2.enable_tls()

    log.info('Run healthcheck for supplier1')
    run_healthcheck_and_flush_log(topology_m2, M1, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2, M1, JSON_OUTPUT, json=True)

    log.info('Run healthcheck for supplier2')
    run_healthcheck_and_flush_log(topology_m2, M2, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2, M2, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.skipif(ds_is_older("1.4.1"), reason="Not implemented")
@pytest.mark.xfail(ds_is_older("1.4.3"),reason="Might fail because of bz1835619")
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
@pytest.mark.skipif(ds_is_older("1.4.1"), reason="Not implemented")
@pytest.mark.xfail(reason="Will fail because of bz1837315. Set proper version after bug is fixed")
def test_healthcheck_unable_to_query_backend(topology_st):
    """Check if HealthCheck returns DSBLE0002 code

    :id: 01de2fe5-079d-4166-b4c9-1f1e00bb091c
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

    log.info('Create new suffix')
    backends = Backends(standalone)
    backends.create(properties={
        'cn': NEW_BACKEND,
        'nsslapd-suffix': NEW_SUFFIX,
    })

    log.info('Disable the newly created suffix')
    mts = MappingTrees(standalone)
    mt_new = mts.get(NEW_SUFFIX)
    mt_new.replace('nsslapd-state', 'disabled')

    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)

    log.info('Enable the suffix again and check if nothing is broken')
    mt_new.replace('nsslapd-state', 'backend')
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1796343
@pytest.mark.skipif(ds_is_older("1.4.1"), reason="Not implemented")
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
