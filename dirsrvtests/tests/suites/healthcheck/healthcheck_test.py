# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import os
from lib389 import DirSrv
from lib389.backend import Backends, DatabaseConfig
from lib389.mappingTree import MappingTrees
from lib389.replica import Changelog5,  Changelog
from lib389.utils import *
from lib389._constants import *
from lib389.cli_base import FakeArgs, LogCapture
from lib389.topologies import topology_st, topology_no_sample, topology_m2
from lib389.cli_ctl.health import health_check_run
from lib389.cli_conf.backend import db_config_set
from lib389.paths import Paths
from lib389.instance.setup import SetupDs

CMD_OUTPUT = 'No issues found.'
JSON_OUTPUT = '[]'
CHANGELOG = 'cn=changelog,{}'.format(DN_USERROOT_LDBM)

ds_paths = Paths()
log = logging.getLogger(__name__)


def run_healthcheck_and_flush_log(logcap, instance, searched_code=None, json=False, searched_code2=None,
                                  list_checks=False, list_errors=False, check=None, searched_list=None):
    args = FakeArgs()
    args.instance = instance.serverid
    args.verbose = instance.verbose
    args.list_errors = list_errors
    args.list_checks = list_checks
    args.check = check
    args.dry_run = False
    args.json = json

    # If we are using BDB as a backend, we will get error DSBLE0006 on new versions
    if ds_is_newer("3.0.0") and instance.get_db_lib() == 'bdb' and \
       (searched_code is CMD_OUTPUT or searched_code is JSON_OUTPUT):
        searched_code = 'DSBLE0006'

    log.info('Use healthcheck with --json == {} option'.format(json))
    health_check_run(instance, logcap.log, args)

    if searched_list is not None:
        for item in searched_list:
            assert logcap.contains(item)
            log.info('Healthcheck returned searched item: %s' % item)
    else:
        assert logcap.contains(searched_code)
        log.info('Healthcheck returned searched code: %s' % searched_code)

    if searched_code2 is not None:
        if ds_is_newer("3.0.0") and instance.get_db_lib() == 'bdb' and \
        (searched_code2 is CMD_OUTPUT or searched_code2 is JSON_OUTPUT):
            searched_code = 'DSBLE0006'

        assert logcap.contains(searched_code2)
        log.info('Healthcheck returned searched code: %s' % searched_code2)

    log.info('Clear the log')
    logcap.flush()


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
    topology_st.standalone.config.set("nsslapd-accesslog-logbuffering", "on")

    run_healthcheck_and_flush_log(topology_st.logcap, topology_st.standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st.logcap, topology_st.standalone, RET_CODE, json=True)

    # reset the suffix state
    mt.replace("nsslapd-state", "backend")


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

    run_healthcheck_and_flush_log(topology_st.logcap, standalone, CMD_OUTPUT,json=False)
    run_healthcheck_and_flush_log(topology_st.logcap, standalone, JSON_OUTPUT, json=True)


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
                   'logs:notes',
                   'tunables:thp',
                   ]

    standalone = topology_st.standalone

    run_healthcheck_and_flush_log(topology_st.logcap, standalone, json=False, list_checks=True, searched_list=output_list)


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
                   'DSBLE0004 :: Both MDB and BDB database files are present',
                   'DSBLE0005 :: Backend configuration attributes mismatch',
                   'DSBLE0006 :: BDB is still used as a backend',
                   'DSCERTLE0001 :: Certificate about to expire',
                   'DSCERTLE0002 :: Certificate expired',
                   'DSCLE0001 :: Different log timestamp format',
                   'DSCLE0002 :: Weak passwordStorageScheme',
                   'DSCLE0003 :: Unauthorized Binds Allowed',
                   'DSCLE0004 :: Access Log buffering disabled',
                   'DSCLE0005 :: Security Log buffering disabled',
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
                   'DSTHPLE0001 :: Transparent Huge Pages',
                   'DSVIRTLE0001 :: Virtual attribute indexed']

    standalone = topology_st.standalone

    run_healthcheck_and_flush_log(topology_st.logcap, standalone, json=False, list_errors=True, searched_list=output_list)


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
                   # 'config:accesslog_buffering',  Skip test access log buffering is disabled
                   'config:securitylog_buffering',
                   'config:unauth_binds',
                   'backends:userroot:cl_trimming',
                   'backends:userroot:mappingtree',
                   'backends:userroot:search',
                   'backends:userroot:virt_attrs',
                   'encryption:check_tls_version',
                   'fschecks:file_perms',
                   'refint:attr_indexes',
                   'refint:update_delay',
                   'memberof:member_attr_indexes',
                   'monitor-disk-space:disk_space',
                   'replication:agmts_status',
                   'replication:conflicts',
                   'replication:no_ruv',
                   'dseldif:nsstate',
                   'tls:certificate_expiration',
                   'logs:notes']

    standalone = topology_st.standalone

    for item in output_list:
        pattern = 'Checking ' + item
        log.info('Check {}'.format(item))
        run_healthcheck_and_flush_log(topology_st.logcap, standalone, searched_code=pattern, json=False, check=[item],
                                      searched_code2=CMD_OUTPUT)


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

    run_healthcheck_and_flush_log(topology_st.logcap, standalone, CMD_OUTPUT,json=False)
    run_healthcheck_and_flush_log(topology_st.logcap, standalone, JSON_OUTPUT, json=True)


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
    M1.config.set("nsslapd-accesslog-logbuffering", "on")
    M2.config.set("nsslapd-accesslog-logbuffering", "on")

    log.info('Run healthcheck for supplier1')
    run_healthcheck_and_flush_log(topology_m2.logcap, M1, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2.logcap, M1, JSON_OUTPUT, json=True)

    log.info('Run healthcheck for supplier2')
    run_healthcheck_and_flush_log(topology_m2.logcap, M2, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2.logcap, M2, JSON_OUTPUT, json=True)


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
    M1.config.set("nsslapd-accesslog-logbuffering", "on")
    M2.config.set("nsslapd-accesslog-logbuffering", "on")
    run_healthcheck_and_flush_log(topology_m2.logcap, M1, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2.logcap, M1, JSON_OUTPUT, json=True)

    log.info('Run healthcheck for supplier2')
    run_healthcheck_and_flush_log(topology_m2.logcap, M2, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2.logcap, M2, JSON_OUTPUT, json=True)


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

    run_healthcheck_and_flush_log(topology_st.logcap, standalone, RET_CODE1, json=False, searched_code2=RET_CODE2)
    run_healthcheck_and_flush_log(topology_st.logcap, standalone, RET_CODE1, json=True, searched_code2=RET_CODE2)

    log.info('Create the dc=example,dc=com backend suffix entry')
    mts.create(properties={
        'cn': DEFAULT_SUFFIX,
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'USERROOT',
    })

    run_healthcheck_and_flush_log(topology_st.logcap, standalone, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_st.logcap, standalone, JSON_OUTPUT, json=True)


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

    run_healthcheck_and_flush_log(topology_st.logcap, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st.logcap, standalone, RET_CODE, json=True)

    log.info('Enable the suffix again and check if nothing is broken')
    mt_new.replace('nsslapd-state', 'backend')
    run_healthcheck_and_flush_log(topology_st.logcap, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st.logcap, standalone, RET_CODE, json=True)


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

    run_healthcheck_and_flush_log(topology_no_sample.logcap, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_no_sample.logcap, standalone, RET_CODE, json=True)


def create_dummy_db_files(inst, backend_type):
    # Define the sets of dummy files for each backend type
    mdb_files = ['data.mdb', 'lock.mdb', 'INFO.mdb']
    bdb_files = ['__db.001', 'DBVERSION', '__db.003', 'userRoot', 'log.0000000001', '__db.002']

    # Determine the target file list based on the backend type
    if backend_type == 'mdb':
        target_files = mdb_files
    else:
        target_files = bdb_files

    # Get the database directory paths from the instance
    db_dir = inst.ds_paths.db_dir

    # Create dummy files in the primary database directory
    for filename in target_files:
        filepath = os.path.join(db_dir, filename)
        with open(filepath, 'w') as f:
            f.write('')  # Create an empty file for simplicity


def test_lint_backend_implementation_wrong_files(topology_st):
    """Test the lint for backend implementation wrong files

    :id: 22cd14f2-c5ba-45e0-96fc-0678adc5c5db
    :setup: Custom instance with db_lib set to either mdb or bdb.
    :steps:
        1. Manually create dummy backend files for the test instance.
        2. Run the linting function to check for errors.
    :expectedresults:
        1. The dummy backend files are created successfully.
        2. The linting function identifies the issue and reports correctly.
    """

    RET_CODE = 'DSBLE0004'

    inst = topology_st.standalone

    if inst.get_db_lib() == 'mdb':
        create_dummy_db_files(inst, 'bdb')
    else:
        create_dummy_db_files(inst, 'mdb')

    run_healthcheck_and_flush_log(topology_st.logcap, inst, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st.logcap, inst, RET_CODE, json=True)


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not needed for mdb")
def test_lint_backend_implementation(topology_st):
    """Test the lint for backend implementation mismatch

    :id: eff607de-768a-4cf4-bcde-48d4c7368934
    :setup: Custom instance with db_lib set to either mdb or bdb.
    :steps:
        1. Fetch the 'nsslapd-backend-implement' attribute value.
        2. Manually set BDB as the backend implementation if MDB
        3. Run the linting function to check if BDB is used
    :expectedresults:
        1. The 'nsslapd-backend-implement' attribute is fetched correctly.
        2. The implementation is set to BDB.
        3. The linting function identifies that BDB is still used as a backend and reports the correct severity issue.
    """

    RET_CODE = 'DSBLE0006'
    inst = topology_st.standalone

    run_healthcheck_and_flush_log(topology_st.logcap, inst, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st.logcap, inst, RET_CODE, json=True)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
