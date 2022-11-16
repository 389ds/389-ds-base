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

from lib389.backend import Backends
from lib389.cos import CosTemplates, CosPointerDefinitions
from lib389.dbgen import dbgen_users
from lib389.idm.account import Accounts
from lib389.index import Index
from lib389.plugins import ReferentialIntegrityPlugin, MemberOfPlugin
from lib389.utils import *
from lib389._constants import *
from lib389.cli_base import FakeArgs
from lib389.topologies import topology_st
from lib389.cli_ctl.health import health_check_run
from lib389.paths import Paths

pytestmark = pytest.mark.tier1

CMD_OUTPUT = 'No issues found.'
JSON_OUTPUT = '[]'
log = logging.getLogger(__name__)


def run_healthcheck_and_flush_log(topology, instance, searched_code, json, searched_code2=None):
    args = FakeArgs()
    args.instance = instance.serverid
    args.verbose = instance.verbose
    args.list_errors = False
    args.list_checks = False
    args.check = ['config', 'refint', 'backends', 'monitor-disk-space', 'logs', 'memberof']
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


@pytest.fixture(scope="function")
def setup_ldif(topology_st, request):
    log.info("Generating LDIF...")
    ldif_dir = topology_st.standalone.get_ldif_dir()
    global import_ldif
    import_ldif = ldif_dir + '/basic_import.ldif'
    dbgen_users(topology_st.standalone, 5000, import_ldif, DEFAULT_SUFFIX)

    def fin():
        log.info('Delete file')
        os.remove(import_ldif)

    request.addfinalizer(fin)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_logging_format_should_be_revised(topology_st):
    """Check if HealthCheck returns DSCLE0001 code

    :id: 277d7980-123b-481b-acba-d90921b9f5ac
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Set nsslapd-logging-hr-timestamps-enabled to 'off'
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. Set nsslapd-logging-hr-timestamps-enabled to 'on'
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

    RET_CODE = 'DSCLE0001'

    standalone = topology_st.standalone

    log.info('Set nsslapd-logging-hr-timestamps-enabled to off')
    standalone.config.set('nsslapd-logging-hr-timestamps-enabled', 'off')

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info('Set nsslapd-logging-hr-timestamps-enabled to off')
    standalone.config.set('nsslapd-logging-hr-timestamps-enabled', 'on')

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_RI_plugin_is_misconfigured(topology_st):
    """Check if HealthCheck returns DSRILE0001 code

    :id: de2e90a2-89fe-472c-acdb-e13cbca5178d
    :setup: Standalone instance
    :steps:
        1. Create DS instance
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

    RET_CODE = 'DSRILE0001'

    standalone = topology_st.standalone

    plugin = ReferentialIntegrityPlugin(standalone)
    plugin.disable()
    plugin.enable()

    log.info('Set the referint-update-delay attribute to a value upper than 0')
    plugin.replace('referint-update-delay', '5')

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info('Set the referint-update-delay attribute back to 0')
    plugin.replace('referint-update-delay', '0')

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_RI_plugin_missing_indexes(topology_st):
    """Check if HealthCheck returns DSRILE0002 code

    :id: 05c55e37-bb3e-48d1-bbe8-29c980f94f10
    :setup: Standalone instance
    :steps:
        1. Create DS instance
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

    RET_CODE = 'DSRILE0002'
    MEMBER_DN = 'cn=member,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config'

    standalone = topology_st.standalone

    log.info('Enable RI plugin')
    plugin = ReferentialIntegrityPlugin(standalone)
    plugin.disable()
    plugin.enable()

    log.info('Change the index type of the member attribute index to approx')
    index = Index(topology_st.standalone, MEMBER_DN)
    index.replace('nsIndexType', 'approx')

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info('Set the index type of the member attribute index back to eq')
    index.replace('nsIndexType', 'eq')

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


def test_healthcheck_MO_plugin_missing_indexes(topology_st):
    """Check if HealthCheck returns DSMOLE0002 code

    :id: 236b0ec2-13da-48fb-b65a-db7406d56d5d
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Configure the instance with MO Plugin with two memberOfGroupAttrs
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. Add index for new group attr
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSMOLE0002 code and related details
        4. Healthcheck reports DSMOLE0002 code and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """

    RET_CODE = 'DSMOLE0001'
    MO_GROUP_ATTR = 'creatorsname'

    standalone = topology_st.standalone

    log.info('Enable MO plugin')
    plugin = MemberOfPlugin(standalone)
    plugin.disable()
    plugin.enable()
    plugin.add('memberofgroupattr', MO_GROUP_ATTR)
    time.sleep(.5)

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info('Add the missing "eq" index')
    be = Backends(standalone).get('userRoot')
    be.add_index(MO_GROUP_ATTR, "eq", None)
    time.sleep(.5)

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)
    # Restart the intsnce after changing the plugin to avoid breaking the other tests
    standalone.restart()


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_virtual_attr_incorrectly_indexed(topology_st):
    """Check if HealthCheck returns DSVIRTLE0001 code

    :id: 1055173b-21aa-4aaa-9e91-4dc6c5e0c01f
    :setup: Standalone instance
    :steps:
        1. Create DS instance
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

    RET_CODE = 'DSVIRTLE0001'

    standalone = topology_st.standalone
    postal_index_properties = {
        'cn': 'postalcode',
        'nsSystemIndex': 'False',
        'nsIndexType': ['eq', 'sub', 'pres'],
    }

    log.info('Add cosPointer, cosTemplate and test entry to default suffix, where virtual attribute is postal code')
    cos_pointer_properties = {
        'cn': 'cosPointer',
        'description': 'cosPointer example',
        'cosTemplateDn': 'cn=cosTemplateExample,ou=People,dc=example,dc=com',
        'cosAttribute': 'postalcode',
    }
    cos_pointer_definitions = CosPointerDefinitions(standalone, DEFAULT_SUFFIX, 'ou=People')
    cos_pointer_definitions.create(properties=cos_pointer_properties)

    log.info('Create CoS template')
    cos_template_properties = {
        'cn': 'cosTemplateExample',
        'postalcode': '117'
    }
    cos_templates = CosTemplates(standalone, DEFAULT_SUFFIX, 'ou=People')
    cos_templates.create(properties=cos_template_properties)

    log.info('Create an index for postalcode')
    backends = Backends(topology_st.standalone)
    ur_indexes = backends.get('userRoot').get_indexes()
    ur_indexes.create(properties=postal_index_properties)

    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
@pytest.mark.xfail(ds_is_older("1.4.2.4"), reason="May fail because of bug 1796050")
def test_healthcheck_low_disk_space(topology_st):
    """Check if HealthCheck returns DSDSLE0001 code

    :id: 144b335d-077e-430c-9c0e-cd6b0f2f73c1
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Get the free disk space for /
        3. Use fallocate to create a file large enough for the use % be up 90%
        4. Use HealthCheck without --json option
        5. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Healthcheck reports DSDSLE0001 code and related details
        5. Healthcheck reports DSDSLE0001 code and related details
    """

    RET_CODE = 'DSDSLE0001'

    standalone = topology_st.standalone
    file = '{}/foo'.format(standalone.ds_paths.log_dir)

    log.info('Count the disk space to allocate')
    total_size = int(re.findall(r'\d+', str(os.statvfs(standalone.ds_paths.log_dir)))[2]) * 4096
    avail_size = round(int(re.findall(r'\d+', str(os.statvfs(standalone.ds_paths.log_dir)))[3]) * 4096)
    used_size = total_size - avail_size
    count_total_percent = total_size * 0.92
    final_value = count_total_percent - used_size

    log.info('Create a file large enough for the use % be up 90%')
    subprocess.call(['fallocate', '-l', str(round(final_value)), file])

    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)

    log.info('Remove created file')
    os.remove(file)


@pytest.mark.flaky(max_runs=2, min_passes=1)
@pytest.mark.ds50791
@pytest.mark.bz1843567
@pytest.mark.xfail(ds_is_older("1.4.3.8"), reason="Not implemented")
def test_healthcheck_notes_unindexed_search(topology_st, setup_ldif):
    """Check if HealthCheck returns DSLOGNOTES0001 code

    :id: b25f7027-d43f-4ec2-ac49-9c9bb285df1d
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Set nsslapd-accesslog-logbuffering to off
        3. Import users from created ldif file
        4. Use HealthCheck without --json option
        5. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Healthcheck reports DSLOGNOTES0001
        5. Healthcheck reports DSLOGNOTES0001
    """

    RET_CODE = 'DSLOGNOTES0001'

    standalone = topology_st.standalone

    log.info('Delete the previous access logs')
    topology_st.standalone.deleteAccessLogs()

    log.info('Set nsslapd-accesslog-logbuffering to off')
    standalone.config.set("nsslapd-accesslog-logbuffering", "off")

    log.info('Stopping the server and running offline import...')
    standalone.stop()
    assert standalone.ldif2db(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], encrypt=None, excludeSuffixes=None,
                              import_file=import_ldif)
    standalone.start()

    log.info('Use filters to reproduce "notes=A" in access log')
    accounts = Accounts(standalone, DEFAULT_SUFFIX)
    accounts.filter('(uid=test*)')

    log.info('Check that access log contains "notes=A"')
    assert standalone.ds_access_log.match(r'.*notes=A.*')

    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)


@pytest.mark.ds50791
@pytest.mark.bz1843567
@pytest.mark.xfail(ds_is_older("1.4.3.8"), reason="Not implemented")
def test_healthcheck_notes_unknown_attribute(topology_st, setup_ldif):
    """Check if HealthCheck returns DSLOGNOTES0002 code

    :id: 71ccd1d7-3c71-416b-9d2a-27f9f6633101
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Set nsslapd-accesslog-logbuffering to off
        3. Import users from created ldif file
        4. Use HealthCheck without --json option
        5. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Healthcheck reports DSLOGNOTES0002
        5. Healthcheck reports DSLOGNOTES0002
    """

    RET_CODE = 'DSLOGNOTES0002'

    standalone = topology_st.standalone

    log.info('Delete the previous access logs')
    topology_st.standalone.deleteAccessLogs()

    log.info('Set nsslapd-accesslog-logbuffering to off')
    standalone.config.set("nsslapd-accesslog-logbuffering", "off")

    log.info('Stopping the server and running offline import...')
    standalone.stop()
    assert standalone.ldif2db(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], encrypt=None, excludeSuffixes=None,
                              import_file=import_ldif)
    standalone.start()

    log.info('Use filters to reproduce "notes=F" in access log')
    accounts = Accounts(standalone, DEFAULT_SUFFIX)
    accounts.filter('(unknown=test)')

    log.info('Check that access log contains "notes=F"')
    assert standalone.ds_access_log.match(r'.*notes=F.*')

    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, RET_CODE, json=True)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
