# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import subprocess
from lib389 import Entry
from lib389.tasks import Tasks
from lib389.dseldif import DSEldif
from create_data import RHDSDataLDIF
from lib389.properties import TASK_WAIT
from lib389.utils import ldap, os, time, logging, ds_is_older
from lib389._constants import SUFFIX, DN_SCHEMA, DN_DM, DEFAULT_SUFFIX, PASSWORD, PLUGIN_MEMBER_OF, \
    PLUGIN_MANAGED_ENTRY, PLUGIN_AUTOMEMBER, DN_CONFIG_LDBM, HOST_STANDALONE, PORT_STANDALONE
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier3

MEMOF_PLUGIN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
MAN_ENTRY_PLUGIN = ('cn=' + PLUGIN_MANAGED_ENTRY + ',cn=plugins,cn=config')
AUTO_MEM_PLUGIN = ('cn=' + PLUGIN_AUTOMEMBER + ',cn=plugins,cn=config')
DOMAIN = 'redhat.com'
LDAP_MOD = '/usr/bin/ldapmodify'
FILTER = 'objectClass=*'
USER_FILTER = '(|(uid=user*)(cn=group*))'
MEMBEROF_ATTR = 'memberOf'
DN_ATTR = 'dn:'

logging.basicConfig(level=logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def memberof_setup(topo, request):
    """Configure required plugins and restart the server"""

    log.info('Configuring memberOf, managedEntry and autoMembers plugins and restarting the server')
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    try:
        topo.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    except ldap.LDAPError as e:
        log.error('Failed to enable {} plugin'.format(PLUGIN_MEMBER_OF))
        raise e
    try:
        topo.standalone.plugins.enable(name=PLUGIN_MANAGED_ENTRY)
        topo.standalone.plugins.enable(name=PLUGIN_AUTOMEMBER)
    except ldap.LDAPError as e:
        log.error('Failed to enable {}, {} plugins'.format(PLUGIN_MANAGED_ENTRY, PLUGIN_AUTOMEMBER))
        raise e

    log.info('Change config values for db-locks and dbcachesize to import large ldif files')
    if ds_is_older('1.3.6'):
        topo.standalone.stop(timeout=10)
        dse_ldif = DSEldif(topo.standalone)
        try:
            dse_ldif.replace(DN_CONFIG_LDBM, 'nsslapd-db-locks', '100000')
            dse_ldif.replace(DN_CONFIG_LDBM, 'nsslapd-dbcachesize', '10000000')
        except:
            log.error('Failed to replace cn=config values of db-locks and dbcachesize')
            raise
        topo.standalone.start(timeout=10)
    else:
        try:
            topo.standalone.modify_s(DN_CONFIG_LDBM, [(ldap.MOD_REPLACE, 'nsslapd-db-locks', '100000')])
            topo.standalone.modify_s(DN_CONFIG_LDBM, [(ldap.MOD_REPLACE, 'nsslapd-cache-autosize', '0')])
            topo.standalone.modify_s(DN_CONFIG_LDBM, [(ldap.MOD_REPLACE, 'nsslapd-dbcachesize', '10000000')])
        except ldap.LDAPError as e:
            log.error(
                'Failed to replace values of nsslapd-db-locks and nsslapd-dbcachesize {}'.format(e.message['desc']))
            raise e
        topo.standalone.restart(timeout=10)

    def fin():
        log.info('Disabling plugins {}, {}, {}'.format(PLUGIN_MEMBER_OF, PLUGIN_MANAGED_ENTRY, PLUGIN_AUTOMEMBER))
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        try:
            topo.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
            topo.standalone.plugins.disable(name=PLUGIN_MANAGED_ENTRY)
            topo.standalone.plugins.disable(name=PLUGIN_AUTOMEMBER)
        except ldap.LDAPError as e:
            log.error('Failed to disable plugins, {}'.format(e.message['desc']))
            assert False
        topo.standalone.restart(timeout=10)

    request.addfinalizer(fin)


def _create_base_ldif(topo, import_base=False):
    """Create base ldif file to clean entries from suffix"""

    log.info('Add base entry for online import')
    ldif_dir = topo.standalone.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, '/perf.ldif')
    log.info('LDIF FILE is this: {}'.format(ldif_file))
    base_ldif = """dn: dc=example,dc=com
objectclass: top
objectclass: domain
dc: example

dn: ou=people,dc=example,dc=com
objectclass: top
objectclass: organizationalUnit
ou: people

dn: ou=groups,dc=example,dc=com
objectclass: top
objectclass: organizationalUnit
ou: groups
"""
    with open(ldif_file, "w") as fd:
        fd.write(base_ldif)
    if import_base:
        log.info('Adding base entry to suffix to remove users/groups and leave only the OUs')
        try:
            topo.standalone.tasks.importLDIF(suffix=SUFFIX, input_file=ldif_file, args={TASK_WAIT: True})
        except ValueError as e:
            log.error('Online import failed' + e.message('desc'))
            assert False
    else:
        log.info('Return LDIF file')
        return ldif_file


def _run_fixup_memberof(topo):
    """Run fixup memberOf task and measure the time taken"""

    log.info('Running fixup memberOf task and measuring the time taken')
    start = time.time()
    try:
        topo.standalone.tasks.fixupMemberOf(suffix=SUFFIX, args={TASK_WAIT: True})
    except ValueError as e:
        log.error('Running fixup MemberOf task failed' + e.message('desc'))
        assert False
    end = time.time()
    cmd_time = int(end - start)
    return cmd_time


def _nested_import_add_ldif(topo, nof_users, nof_groups, grps_user, ngrps_user, nof_depth, is_import=False):
    """Create LDIF files for given nof users, groups and nested group levels"""

    log.info('Checking if the operation is Import or Ldapadd')
    if is_import:
        log.info('Import: Create base entry before adding users and groups')
        exp_entries = nof_users + nof_groups
        data_ldif = _create_base_ldif(topo, False)
        log.info('Create data LDIF file by appending users, groups and nested groups')
        with open(data_ldif, 'a') as file1:
            data = RHDSDataLDIF(stream=file1, users=nof_users, groups=nof_groups, grps_puser=grps_user,
                                nest_level=nof_depth, ngrps_puser=ngrps_user, basedn=SUFFIX)
            data.do_magic()
        start = time.time()
        log.info('Run importLDIF task to add entries to Server')
        try:
            topo.standalone.tasks.importLDIF(suffix=SUFFIX, input_file=data_ldif, args={TASK_WAIT: True})
        except ValueError as e:
            log.error('Online import failed' + e.message('desc'))
            assert False
        end = time.time()
        time_import = int(end - start)

        log.info('Check if number of entries created matches the expected entries')
        users_groups = topo.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, USER_FILTER, [DN_ATTR])
        act_entries = str(users_groups).count(DN_ATTR)
        log.info('Expected entries: {}, Actual entries: {}'.format(exp_entries, act_entries))
        assert act_entries == exp_entries
        return time_import
    else:
        log.info('Ldapadd: Create data LDIF file with users, groups and nested groups')
        ldif_dir = topo.standalone.get_ldif_dir()
        data_ldif = os.path.join(ldif_dir, '/perf_add.ldif')
        with open(data_ldif, 'w') as file1:
            data = RHDSDataLDIF(stream=file1, users=nof_users, groups=nof_groups, grps_puser=grps_user,
                                nest_level=nof_depth, ngrps_puser=ngrps_user, basedn=SUFFIX)
            data.do_magic()
        start = time.time()
        log.info('Run LDAPMODIFY to add entries to Server')
        try:
            subprocess.check_output(
                [LDAP_MOD, '-cx', '-D', DN_DM, '-w', PASSWORD, '-h', HOST_STANDALONE, '-p', str(PORT_STANDALONE), '-af',
                 data_ldif])
        except subprocess.CalledProcessError as e:
            log.error('LDAPMODIFY failed to add entries, error:{:s}'.format(str(e)))
            raise e
        end = time.time()
        cmd_time = int(end - start)
        log.info('Time taken to complete LDAPADD: {} secs'.format(cmd_time))
        return cmd_time


def _sync_memberof_attrs(topo, exp_memberof):
    """Check if expected entries are created or attributes are synced"""

    log.info('_sync_memberof_attrs: Check if expected memberOf attributes are synced/created')
    loop = 0
    start = time.time()
    entries = topo.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, FILTER, [MEMBEROF_ATTR])
    act_memberof = str(entries).count(MEMBEROF_ATTR)
    end = time.time()
    cmd_time = int(end - start)
    log.info('Loop-{}, expected memberOf attrs: {}, synced: {}, time for search-{} secs'.format(loop, exp_memberof,
                                                                                                act_memberof, cmd_time))
    while act_memberof != exp_memberof:
        loop = loop + 1
        time.sleep(30)
        start = time.time()
        entries = topo.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, FILTER, [MEMBEROF_ATTR])
        act_memberof = str(entries).count(MEMBEROF_ATTR)
        end = time.time()
        cmd_time = cmd_time + int(end - start)
        log.info('Loop-{}, expected memberOf attrs: {}, synced: {}, time for search-{} secs'.format(loop, exp_memberof,
                                                                                                    act_memberof,
                                                                                                    cmd_time))
        # Worst case scenario, exit the test after 10hrs of wait
        if loop > 1200:
            log.error('Either syncing memberOf attrs takes too long or some issue with the test itself')
            assert False
    sync_time = 1 + loop * 30
    log.info('Expected memberOf attrs: {}, Actual memberOf attrs: {}'.format(exp_memberof, act_memberof))
    assert act_memberof == exp_memberof
    return sync_time


@pytest.mark.parametrize("nof_users, nof_groups, grps_user, ngrps_user, nof_depth",
                         [(20000, 200, 20, 10, 5), (50000, 500, 50, 10, 10), (100000, 1000, 100, 20, 20)])
def test_nestgrps_import(topo, memberof_setup, nof_users, nof_groups, grps_user, ngrps_user, nof_depth):
    """Import large users and nested groups with N depth and measure the time taken

    :id: 169a09f2-2c2d-4e42-8b90-a0bd1034f278
    :feature: MemberOf Plugin
    :setup: Standalone instance, memberOf plugin enabled
    :steps: 1. Create LDIF file for given nof_users and nof_groups
            2. Import entries to server
            3. Check if entries are created
            4. Run fixupMemberOf task to create memberOf attributes
            5. Check if memberOf attributes are synced for all users and groups
            6. Compare the actual no of memberOf attributes to the expected
            7. Measure the time taken to sync memberOf attributes
    :expectedresults: MemberOf attributes should be synced
    """

    exp_memberof = (nof_users * grps_user) + (
        (nof_groups // grps_user) * (ngrps_user // nof_depth) * (nof_depth * (nof_depth + 1)) // 2)
    log.info('Create nested ldif file with users-{}, groups-{}, nested-{}'.format(nof_users, nof_groups, nof_depth))
    log.info('Import LDIF file and measure the time taken')
    import_time = _nested_import_add_ldif(topo, nof_users, nof_groups, grps_user, ngrps_user, nof_depth, True)

    log.info('Run fixup memberOf task and measure the time taken to complete the task')
    fixup_time = _run_fixup_memberof(topo)

    log.info('Check the total number of memberOf entries created for users and groups')
    sync_memberof = _sync_memberof_attrs(topo, exp_memberof)

    total_time = import_time + fixup_time + sync_memberof
    log.info('Time for import-{}secs, fixup task-{}secs, total time for memberOf sync: {}secs'.format(import_time,
                                                                                                      fixup_time,
                                                                                                      total_time))


@pytest.mark.parametrize("nof_users, nof_groups, grps_user, ngrps_user, nof_depth",
                         [(20000, 100, 20, 10, 5), (50000, 200, 50, 10, 10), (100000, 100, 20, 10, 10)])
def test_nestgrps_add(topo, memberof_setup, nof_users, nof_groups, grps_user, ngrps_user, nof_depth):
    """Import large users and nested groups with n depth and measure the time taken

    :id: 6eda75c6-5ae0-4b17-b610-d217d7ec7542
    :feature: MemberOf Plugin
    :setup: Standalone instance, memberOf plugin enabled
    :steps: 1. Create LDIF file for given nof_users and nof_groups
            2. Add entries using LDAPADD
            3. Check if entries are created
            4. Check if memberOf attributes are synced for all users and groups
            5. Compare the actual no of memberOf attributes to the expected
            6. Measure the time taken to sync memberOf attributes
    :expectedresults: MemberOf attributes should be created and synced
    """

    exp_memberof = (nof_users * grps_user) + (
        (nof_groups // grps_user) * (ngrps_user // nof_depth) * (nof_depth * (nof_depth + 1)) // 2)
    log.info('Creating base_ldif file and importing it to wipe out all users and groups')
    _create_base_ldif(topo, True)
    log.info('Create nested ldif file with users-{}, groups-{}, nested-{}'.format(nof_users, nof_groups, nof_depth))
    log.info('Run LDAPADD to add entries to Server')
    add_time = _nested_import_add_ldif(topo, nof_users, nof_groups, grps_user, ngrps_user, nof_depth, False)

    log.info('Check the total number of memberOf entries created for users and groups')
    sync_memberof = _sync_memberof_attrs(topo, exp_memberof)
    total_time = add_time + sync_memberof
    log.info('Time for ldapadd-{}secs, total time for memberOf sync: {}secs'.format(add_time, total_time))


@pytest.mark.parametrize("nof_users, nof_groups, grps_user, ngrps_user, nof_depth",
                         [(20000, 200, 20, 10, 5), (50000, 500, 50, 10, 10), (100000, 1000, 100, 20, 20)])
def test_mod_nestgrp(topo, memberof_setup, nof_users, nof_groups, grps_user, ngrps_user, nof_depth):
    """Import bulk entries, modify nested groups at N depth and measure the time taken

    :id: 4bf8e753-6ded-4177-8225-aaf6aef4d131
    :feature: MemberOf Plugin
    :setup: Standalone instance, memberOf plugin enabled
    :steps: 1. Import bulk entries with nested group and create memberOf attributes
            2. Modify nested groups by adding new members at each nested level
            3. Check new memberOf attributes created for users and groups
            4. Compare the actual memberOf attributes with the expected
            5. Measure the time taken to sync memberOf attributes
    :expectedresults: MemberOf attributes should be modified and synced
    """

    exp_memberof = (nof_users * grps_user) + (
        (nof_groups // grps_user) * (ngrps_user // nof_depth) * (nof_depth * (nof_depth + 1)) // 2)
    log.info('Create nested ldif file, import it and measure the time taken')
    import_time = _nested_import_add_ldif(topo, nof_users, nof_groups, grps_user, ngrps_user, nof_depth, True)
    log.info('Run fixup memberOf task and measure the time to complete the task')
    fixup_time = _run_fixup_memberof(topo)
    sync_memberof = _sync_memberof_attrs(topo, exp_memberof)
    total_time = import_time + fixup_time + sync_memberof
    log.info('Time for import-{}secs, fixup task-{}secs, total time for memberOf sync: {}secs'.format(import_time,
                                                                                                      fixup_time,
                                                                                                      total_time))

    log.info('Add {} users to existing nested groups at all depth level'.format(nof_groups))
    log.info('Add one user to each groups at different nest levels')
    start = time.time()
    for usr in range(nof_groups):
        usrrdn = 'newcliusr{}'.format(usr)
        userdn = 'uid={},ou=people,{}'.format(usrrdn, SUFFIX)
        groupdn = 'cn=group{},ou=groups,{}'.format(usr, SUFFIX)
        try:
            topo.standalone.add_s(Entry((userdn, {
                'objectclass': 'top person inetUser inetOrgperson'.split(),
                'cn': usrrdn,
                'sn': usrrdn,
                'userpassword': 'Secret123'})))
        except ldap.LDAPError as e:
            log.error('Failed to add {} user: error {}'.format(userdn, e.message['desc']))
            raise
        try:
            topo.standalone.modify_s(groupdn, [(ldap.MOD_ADD, 'member', userdn)])
        except ldap.LDAPError as e:
            log.error('Error-{}: Failed to add user to group'.format(e.message['desc']))
            assert False
    end = time.time()
    cmd_time = int(end - start)

    exp_memberof = (nof_users * grps_user) + nof_groups + (
        (nof_groups // grps_user) * (ngrps_user // nof_depth) * (nof_depth * (nof_depth + 1)))
    log.info('Check the total number of memberOf entries created for users and groups')
    sync_memberof = _sync_memberof_attrs(topo, exp_memberof)
    total_time = cmd_time + sync_memberof
    log.info('Time taken add new members to existing nested groups + memberOf sync: {} secs'.format(total_time))


@pytest.mark.parametrize("nof_users, nof_groups, grps_user, ngrps_user, nof_depth",
                         [(20000, 200, 20, 10, 5), (50000, 500, 50, 10, 10), (100000, 1000, 100, 20, 20)])
def test_del_nestgrp(topo, memberof_setup, nof_users, nof_groups, grps_user, ngrps_user, nof_depth):
    """Import bulk entries, delete nested groups at N depth and measure the time taken

    :id: d3d82ac5-d968-4cd6-a268-d380fc9fd51b
    :feature: MemberOf Plugin
    :setup: Standalone instance, memberOf plugin enabled
    :steps: 1. Import bulk users and groups with nested level N.
            2. Run fixup memberOf task to create memberOf attributes
            3. Delete nested groups at nested level N
            4. Check memberOf attributes deleted for users and groups
            5. Compare the actual memberOf attributes with the expected
            6. Measure the time taken to sync memberOf attributes
    :expectedresults: MemberOf attributes should be deleted and synced
    """

    exp_memberof = (nof_users * grps_user) + (
        (nof_groups // grps_user) * (ngrps_user // nof_depth) * (nof_depth * (nof_depth + 1)) // 2)
    log.info('Create nested ldif file, import it and measure the time taken')
    import_time = _nested_import_add_ldif(topo, nof_users, nof_groups, grps_user, ngrps_user, nof_depth, True)
    log.info('Run fixup memberOf task and measure the time to complete the task')
    fixup_time = _run_fixup_memberof(topo)
    sync_memberof = _sync_memberof_attrs(topo, exp_memberof)
    total_time = import_time + fixup_time + sync_memberof
    log.info('Time taken to complete add users + memberOf sync: {} secs'.format(total_time))

    log.info('Delete {} groups from nested groups at depth level-{}'.format(nof_depth, nof_depth))
    start = time.time()
    for nos in range(nof_depth, nof_groups, grps_user):
        groupdn = 'cn=group{},ou=groups,{}'.format(nos, SUFFIX)
        try:
            topo.standalone.delete_s(groupdn)
        except ldap.LDAPError as e:
            log.error('Error-{}: Failed to delete group'.format(e.message['desc']))
            assert False
    end = time.time()
    cmd_time = int(end - start)

    exp_memberof = exp_memberof - (nof_users + (nof_depth * (nof_groups // grps_user)))
    log.info('Check memberOf attributes after deleting groups at depth-{}'.format(nof_depth))
    sync_memberof = _sync_memberof_attrs(topo, exp_memberof)
    total_time = cmd_time + sync_memberof
    log.info('Time taken to delete and sync memberOf attributes: {}secs'.format(total_time))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
