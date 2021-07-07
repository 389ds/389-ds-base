# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from decimal import *
import os
import logging
import pytest
import subprocess
from lib389._mapped_object import DSLdapObject
from lib389.topologies import topology_st
from lib389.plugins import AutoMembershipPlugin, ReferentialIntegrityPlugin, AutoMembershipDefinitions
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389._constants import DEFAULT_SUFFIX, LOG_ACCESS_LEVEL, PASSWORD
from lib389.utils import ds_is_older, ds_is_newer
from lib389.config import RSA
import ldap
import glob
import re

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

PLUGIN_TIMESTAMP = 'nsslapd-logging-hr-timestamps-enabled'
PLUGIN_LOGGING = 'nsslapd-plugin-logging'
USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX

def add_users(topology_st, users_num):
    users = UserAccounts(topology_st, DEFAULT_SUFFIX)
    log.info('Adding %d users' % users_num)
    for i in range(0, users_num):
        uid = 1000 + i
        users.create(properties={
            'uid': 'testuser%d' % uid,
            'cn': 'testuser%d' % uid,
            'sn': 'user',
            'uidNumber': '%d' % uid,
            'gidNumber': '%d' % uid,
            'homeDirectory': '/home/testuser%d' % uid
        })


def search_users(topology_st):
    users = UserAccounts(topology_st, DEFAULT_SUFFIX)
    entries = users.list()
    # We just assert we got some data ...
    assert len(entries) > 0


def delete_obj(obj):
    if obj.exists():
        obj.delete()


def add_group_and_perform_user_operations(topology_st):
    topo = topology_st.standalone

    # Add the automember group
    groups = Groups(topo, DEFAULT_SUFFIX)
    group = groups.create(properties={'cn': 'group'})

    ous = OrganizationalUnits(topo, DEFAULT_SUFFIX)
    branch1 = ous.create(properties={'ou': 'branch1'})

    # Add the automember config entry
    am_configs = AutoMembershipDefinitions(topo)
    am_config = am_configs.create(properties={'cn': 'config',
                                              'autoMemberScope': branch1.dn,
                                              'autoMemberFilter': 'objectclass=top',
                                              'autoMemberDefaultGroup': group.dn,
                                              'autoMemberGroupingAttr': 'member:dn'})

    # Add a user that should get added to the group
    users = UserAccounts(topo, DEFAULT_SUFFIX, rdn='ou={}'.format(branch1.rdn))
    test_user = users.create_test_user(uid=777)

    # Check if created user is group member
    assert test_user.dn in group.list_members()

    log.info('Renaming user')
    test_user.rename('uid=new_test_user_777', newsuperior=DEFAULT_SUFFIX)

    log.info('Delete the user')
    delete_obj(test_user)

    log.info('Delete automember entry, org. unit and group for the next test')
    delete_obj(am_config)
    delete_obj(branch1)
    delete_obj(group)


@pytest.fixture(scope="module")
def enable_plugins(topology_st):
    topo = topology_st.standalone

    log.info("Enable automember plugin")
    plugin = AutoMembershipPlugin(topo)
    plugin.enable()

    log.info('Enable Referential Integrity plugin')
    plugin = ReferentialIntegrityPlugin(topo)
    plugin.enable()

    log.info('Set nsslapd-plugin-logging to on')
    topo.config.set(PLUGIN_LOGGING, 'ON')

    log.info('Restart the server')
    topo.restart()


def add_user_log_level(topology_st, loglevel, request):
    topo = topology_st.standalone
    default_log_level = topo.config.get_attr_val_utf8(LOG_ACCESS_LEVEL)
    log.info(f'Configure access log level to {loglevel}')
    topo.config.set(LOG_ACCESS_LEVEL, str(loglevel))
    add_group_and_perform_user_operations(topology_st)

    def fin():
        topo.config.set(LOG_ACCESS_LEVEL, default_log_level)
        log.info('Delete the previous access logs for the next test')
        topo.deleteAccessLogs()
    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def add_user_log_level_260(topology_st, enable_plugins, request):
    access_log_level = 4 + 256
    add_user_log_level(topology_st, access_log_level, request)


@pytest.fixture(scope="function")
def add_user_log_level_516(topology_st, enable_plugins, request):
    access_log_level = 4 + 512
    add_user_log_level(topology_st, access_log_level, request)


@pytest.fixture(scope="function")
def add_user_log_level_131076(topology_st, enable_plugins, request):
    access_log_level = 4 + 131072
    add_user_log_level(topology_st, access_log_level, request)


@pytest.fixture(scope="function")
def clean_access_logs(topology_st, request):
    def _clean_access_logs():
        topo = topology_st.standalone
        log.info("Stopping the instance")
        topo.stop()
        log.info("Deleting the access logs")
        topo.deleteAccessLogs()
        log.info("Starting the instance")
        topo.start()

    request.addfinalizer(_clean_access_logs)

    return clean_access_logs

@pytest.fixture(scope="function")
def remove_users(topology_st, request):
    def _remove_users():
        topo = topology_st.standalone
        users = UserAccounts(topo, DEFAULT_SUFFIX)
        entries = users.list()
        assert len(entries) > 0

        log.info("Removing all added users")
        for entry in entries:
            delete_obj(entry)

    request.addfinalizer(_remove_users)


def set_audit_log_config_values(topology_st, request, enabled, logsize):
    topo = topology_st.standalone

    topo.config.set('nsslapd-auditlog-logging-enabled', enabled)
    topo.config.set('nsslapd-auditlog-maxlogsize', logsize)

    def fin():
        topo.start()
        log.info('Setting audit log config back to default values')
        topo.config.set('nsslapd-auditlog-logging-enabled', 'off')
        topo.config.set('nsslapd-auditlog-maxlogsize', '100')

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def set_audit_log_config_values_to_rotate(topology_st, request):
    set_audit_log_config_values(topology_st, request, 'on', '1')

@pytest.fixture(scope="function")
def disable_access_log_buffering(topology_st, request):
    log.info('Disable access log buffering')
    topology_st.standalone.config.set('nsslapd-accesslog-logbuffering', 'off')
    def fin():
        log.info('Enable access log buffering')
        topology_st.standalone.config.set('nsslapd-accesslog-logbuffering', 'on')

    request.addfinalizer(fin)

    return disable_access_log_buffering

@pytest.mark.bz1273549
def test_check_default(topology_st):
    """Check the default value of nsslapd-logging-hr-timestamps-enabled,
     it should be ON

    :id: 2d15002e-9ed3-4796-b0bb-bf04e4e59bd3

    :setup: Standalone instance

    :steps:
         1. Fetch the value of nsslapd-logging-hr-timestamps-enabled attribute
         2. Test that the attribute value should be "ON" by default

    :expectedresults:
         1. Value should be fetched successfully
         2. Value should be "ON" by default
    """

    # Get the default value of nsslapd-logging-hr-timestamps-enabled attribute
    default = topology_st.standalone.config.get_attr_val_utf8(PLUGIN_TIMESTAMP)

    # Now check it should be ON by default
    assert default == "on"
    log.debug(default)


@pytest.mark.bz1273549
def test_plugin_set_invalid(topology_st):
    """Try to set some invalid values for nsslapd-logging-hr-timestamps-enabled
    attribute

    :id: c60a68d2-703a-42bf-a5c2-4040736d511a

    :setup: Standalone instance

    :steps:
         1. Set some "JUNK" value of nsslapd-logging-hr-timestamps-enabled attribute

    :expectedresults:
         1. There should be an operation error
    """

    log.info('test_plugin_set_invalid - Expect to fail with junk value')
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topology_st.standalone.config.set(PLUGIN_TIMESTAMP, 'JUNK')


@pytest.mark.bz1273549
def test_log_plugin_on(topology_st, remove_users):
    """Check access logs for millisecond, when
    nsslapd-logging-hr-timestamps-enabled=ON

    :id: 65ae4e2a-295f-4222-8d69-12124bc7a872

    :setup: Standalone instance

    :steps:
         1. To generate big logs, add 100 test users
         2. Search users to generate more access logs
         3. Restart server
         4. Parse the logs to check the milliseconds got recorded in logs

    :expectedresults:
         1. Add operation should be successful
         2. Search operation should be successful
         3. Server should be restarted successfully
         4. There should be milliseconds added in the access logs
    """

    log.info('Bug 1273549 - Check access logs for millisecond, when attribute is ON')
    log.info('perform any ldap operation, which will trigger the logs')
    add_users(topology_st.standalone, 10)
    search_users(topology_st.standalone)

    log.info('Restart the server to flush the logs')
    topology_st.standalone.restart(timeout=10)

    log.info('parse the access logs')
    access_log_lines = topology_st.standalone.ds_access_log.readlines()
    assert len(access_log_lines) > 0
    assert topology_st.standalone.ds_access_log.match(r'^\[.+\d{9}.+\].+')


@pytest.mark.bz1273549
def test_log_plugin_off(topology_st, remove_users):
    """Milliseconds should be absent from access logs when
    nsslapd-logging-hr-timestamps-enabled=OFF

    :id: b3400e46-d940-4574-b399-e3f4b49bc4b5

    :setup: Standalone instance

    :steps:
         1. Set nsslapd-logging-hr-timestamps-enabled=OFF
         2. Restart the server
         3. Delete old access logs
         4. Do search operations to generate fresh access logs
         5. Restart the server
         6. Check access logs

    :expectedresults:
         1. Attribute nsslapd-logging-hr-timestamps-enabled should be set to "OFF"
         2. Server should restart
         3. Access logs should be deleted
         4. Search operation should PASS
         5. Server should restart
         6. There should not be any milliseconds added in the access logs
    """

    log.info('Bug 1273549 - Check access logs for missing millisecond, when attribute is OFF')

    log.info('test_log_plugin_off - set the configuration attribute to OFF')
    topology_st.standalone.config.set(PLUGIN_TIMESTAMP, 'OFF')

    log.info('Restart the server to flush the logs')
    topology_st.standalone.restart(timeout=10)

    log.info('test_log_plugin_off - delete the previous access logs')
    topology_st.standalone.deleteAccessLogs()

    # Now generate some fresh logs
    add_users(topology_st.standalone, 10)
    search_users(topology_st.standalone)

    log.info('Restart the server to flush the logs')
    topology_st.standalone.restart(timeout=10)

    log.info('check access log that microseconds are not present')
    access_log_lines = topology_st.standalone.ds_access_log.readlines()
    assert len(access_log_lines) > 0
    assert not topology_st.standalone.ds_access_log.match(r'^\[.+\d{9}.+\].+')


@pytest.mark.xfail(ds_is_older('1.4.0'), reason="May fail on 1.3.x because of bug 1358706")
@pytest.mark.bz1358706
@pytest.mark.ds49029
def test_internal_log_server_level_0(topology_st, clean_access_logs, disable_access_log_buffering):
    """Tests server-initiated internal operations

    :id: 798d06fe-92e8-4648-af66-21349c20638e
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-plugin-logging to on
        2. Configure access log level to only 0
        3. Check the access logs.
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Access log should not contain internal operations log formats
    """

    topo = topology_st.standalone
    default_log_level = topo.config.get_attr_val_utf8(LOG_ACCESS_LEVEL)


    log.info('Set nsslapd-plugin-logging to on')
    topo.config.set(PLUGIN_LOGGING, 'ON')

    log.info('Configure access log level to 0')
    access_log_level = '0'
    topo.config.set(LOG_ACCESS_LEVEL, access_log_level)

    log.info('Restart the server to flush the logs')
    topo.restart()

    # These comments contain lines we are trying to find without regex (the op numbers are just examples)
    log.info("Check if access log does not contain internal log of MOD operation")
    # (Internal) op=2(2)(1) SRCH base="cn=config
    assert not topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) SRCH base="cn=config.*')
    # (Internal) op=2(2)(1) RESULT err=0 tag=48 nentries=1
    assert not topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=1.*')

    log.info("Check if the other internal operations are not present")
    # conn=Internal(0) op=0
    assert not topo.ds_access_log.match(r'.*conn=Internal\([0-9]+\) op=[0-9]+\([0-9]+\)\([0-9]+\).*')

    topo.config.set(LOG_ACCESS_LEVEL, default_log_level)


@pytest.mark.xfail(ds_is_older('1.4.0'), reason="May fail on 1.3.x because of bug 1358706")
@pytest.mark.bz1358706
@pytest.mark.ds49029
def test_internal_log_server_level_4(topology_st, clean_access_logs, disable_access_log_buffering):
    """Tests server-initiated internal operations

    :id: a3500e47-d941-4575-b399-e3f4b49bc4b6
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-plugin-logging to on
        2. Configure access log level to only 4
        3. Check the access logs, it should contain info about MOD operation of cn=config and other
           internal operations should have the conn field set to Internal
           and all values inside parenthesis set to 0.
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Access log should contain correct internal log formats with cn=config modification:
           "(Internal) op=2(1)(1)"
           "conn=Internal(0)"
    """

    topo = topology_st.standalone
    default_log_level = topo.config.get_attr_val_utf8(LOG_ACCESS_LEVEL)

    log.info('Set nsslapd-plugin-logging to on')
    topo.config.set(PLUGIN_LOGGING, 'ON')

    log.info('Configure access log level to 4')
    access_log_level = '4'
    topo.config.set(LOG_ACCESS_LEVEL, access_log_level)

    log.info('Restart the server to flush the logs')
    topo.restart()

    try:
        # These comments contain lines we are trying to find without regex (the op numbers are just examples)
        log.info("Check if access log contains internal MOD operation in correct format")
        # (Internal) op=2(2)(1) SRCH base="cn=config
        assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) SRCH base="cn=config.*')
        # (Internal) op=2(2)(1) RESULT err=0 tag=48 nentries=
        assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=.*')

        log.info("Check if the other internal operations have the correct format")
        # conn=Internal(0) op=0
        assert topo.ds_access_log.match(r'.*conn=Internal\([0-9]+\) op=[0-9]+\([0-9]+\)\([0-9]+\).*')
    finally:
        topo.config.set(LOG_ACCESS_LEVEL, default_log_level)


@pytest.mark.xfail(ds_is_older('1.4.0'), reason="May fail on 1.3.x because of bug 1358706")
@pytest.mark.bz1358706
@pytest.mark.ds49029
def test_internal_log_level_260(topology_st, add_user_log_level_260, disable_access_log_buffering):
    """Tests client initiated operations when automember plugin is enabled

    :id: e68a303e-c037-42b2-a5a0-fbea27c338a9
    :setup: Standalone instance with internal operation
            logging on and nsslapd-plugin-logging to on
    :steps:
        1. Configure access log level to 260 (4 + 256)
        2. Set nsslapd-plugin-logging to on
        3. Enable Referential Integrity and automember plugins
        4. Restart the server
        5. Add a test group
        6. Add a test user and add it as member of the test group
        7. Rename the test user
        8. Delete the test user
        9. Check the access logs for nested internal operation logs
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. Operation should be successful
        5. Operation should be successful
        6. Operation should be successful
        7. Operation should be successful
        8. Operation should be successful
        9. Access log should contain internal info about operations of the user
    """

    topo = topology_st.standalone

    log.info('Restart the server to flush the logs')
    topo.restart()

    # These comments contain lines we are trying to find without regex (the op numbers are just examples)
    log.info("Check the access logs for ADD operation of the user")
    # op=10 ADD dn="uid=test_user_777,ou=topology_st, branch1,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*op=[0-9]+ ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com".*')
    # (Internal) op=10(1)(1) MOD dn="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) '
                                    r'MOD dn="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) SRCH base="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) SRCH base="cn=group,'
                                    r'ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=1*')
    # (Internal) op=10(1)(1) RESULT err=0 tag=48
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48.*')
    # op=10 RESULT err=0 tag=105
    assert topo.ds_access_log.match(r'.*op=[0-9]+ RESULT err=0 tag=105.*')

    log.info("Check the access logs for MOD operation of the user")
    # op=12 MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
    #      'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*op=[0-9]+ MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
                                    'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com".*')
    if ds_is_older(('1.4.3.9', '1.4.4.3')):
        # (Internal) op=12(1)(1) SRCH base="uid=test_user_777, ou=branch1,dc=example,dc=com"
        assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) SRCH base="uid=test_user_777,'
                                        'ou=branch1,dc=example,dc=com".*')
    # (Internal) op=12(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=1.*')
    # op=12 RESULT err=0 tag=109
    assert topo.ds_access_log.match(r'.*op=[0-9]+ RESULT err=0 tag=109.*')

    log.info("Check the access logs for DEL operation of the user")
    # op=15 DEL dn="uid=new_test_user_777,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*op=[0-9]+ DEL dn="uid=new_test_user_777,dc=example,dc=com".*')
    if ds_is_older(('1.4.3.9', '1.4.4.3')):
        # (Internal) op=15(1)(1) SRCH base="uid=new_test_user_777, dc=example,dc=com"
        assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) SRCH base="uid=new_test_user_777,'
                                        'dc=example,dc=com".*')
    # (Internal) op=15(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=1.*')
    # op=15 RESULT err=0 tag=107
    assert topo.ds_access_log.match(r'.*op=[0-9]+ RESULT err=0 tag=107.*')

    log.info("Check if the other internal operations have the correct format")
    # conn=Internal(0) op=0
    assert topo.ds_access_log.match(r'.*conn=Internal\([0-9]+\) op=[0-9]+\([0-9]+\)\([0-9]+\).*')


@pytest.mark.xfail(ds_is_older('1.4.0'), reason="May fail on 1.3.x because of bug 1358706")
@pytest.mark.bz1358706
@pytest.mark.ds49029
def test_internal_log_level_131076(topology_st, add_user_log_level_131076, disable_access_log_buffering):
    """Tests client-initiated operations while referential integrity plugin is enabled

    :id: 44836ac9-dabd-4a8c-abd5-ecd7c2509739
    :setup: Standalone instance
            Configure access log level to - 131072 + 4
            Set nsslapd-plugin-logging to on
    :steps:
        1. Configure access log level to 131076
        2. Set nsslapd-plugin-logging to on
        3. Enable Referential Integrity and automember plugins
        4. Restart the server
        5. Add a test group
        6. Add a test user and add it as member of the test group
        7. Rename the test user
        8. Delete the test user
        9. Check the access logs for nested internal operation logs
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. Operation should be successful
        5. Operation should be successful
        6. Operation should be successful
        7. Operation should be successful
        8. Operation should be successful
        9. Access log should contain internal info about operations of the user
    """

    topo = topology_st.standalone

    log.info('Restart the server to flush the logs')
    topo.restart()

    # These comments contain lines we are trying to find without regex (the op numbers are just examples)
    log.info("Check the access logs for ADD operation of the user")
    # op=10 ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com".*')
    # (Internal) op=10(1)(1) MOD dn="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) '
                                    r'MOD dn="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) SRCH base="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) '
                                    r'SRCH base="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) RESULT err=0 tag=48 nentries=1*')
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=1*')
    # (Internal) op=10(1)(1) RESULT err=0 tag=48
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48.*')
    # op=10 RESULT err=0 tag=105
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ RESULT err=0 tag=105.*')

    log.info("Check the access logs for MOD operation of the user")
    # op=12 MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
    #      'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
                                        'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com".*')
    if ds_is_older(('1.4.3.9', '1.4.4.3')):
        # (Internal) op=12(1)(1) SRCH base="uid=test_user_777, ou=branch1,dc=example,dc=com"
        assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) SRCH base="uid=test_user_777,'
                                        'ou=branch1,dc=example,dc=com".*')
    # (Internal) op=12(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=1.*')
    # op=12 RESULT err=0 tag=109
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ RESULT err=0 tag=109.*')

    log.info("Check the access logs for DEL operation of the user")
    # op=15 DEL dn="uid=new_test_user_777,dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ DEL dn="uid=new_test_user_777,dc=example,dc=com".*')
    if ds_is_older(('1.4.3.9', '1.4.4.3')):
        # (Internal) op=15(1)(1) SRCH base="uid=new_test_user_777, dc=example,dc=com"
        assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) SRCH base="uid=new_test_user_777,'
                                        'dc=example,dc=com".*')
    # (Internal) op=15(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=1.*')
    # op=15 RESULT err=0 tag=107
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ RESULT err=0 tag=107.*')

    log.info("Check if the other internal operations have the correct format")
    # conn=Internal(0) op=0
    assert topo.ds_access_log.match(r'.*conn=Internal\([0-9]+\) op=[0-9]+\([0-9]+\)\([0-9]+\).*')


@pytest.mark.xfail(ds_is_older('1.4.0'), reason="May fail on 1.3.x because of bug 1358706")
@pytest.mark.bz1358706
@pytest.mark.ds49029
def test_internal_log_level_516(topology_st, add_user_log_level_516, disable_access_log_buffering):
    """Tests client initiated operations when referential integrity plugin is enabled

    :id: bee1d681-763d-4fa5-aca2-569cf93f8b71
    :setup: Standalone instance
            Configure access log level to - 512+4
            Set nsslapd-plugin-logging to on
    :steps:
        1. Configure access log level to 516
        2. Set nsslapd-plugin-logging to on
        3. Enable Referential Integrity and automember plugins
        4. Restart the server
        5. Add a test group
        6. Add a test user and add it as member of the test group
        7. Rename the test user
        8. Delete the test user
        9. Check the access logs for nested internal operation logs
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. Operation should be successful
        5. Operation should be successful
        6. Operation should be successful
        7. Operation should be successful
        8. Operation should be successful
        9. Access log should contain internal info about operations of the user
    """

    topo = topology_st.standalone

    log.info('Restart the server to flush the logs')
    topo.restart()

    # These comments contain lines we are trying to find without regex (the op numbers are just examples)
    log.info("Check the access logs for ADD operation of the user")
    # op=10 ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com".*')
    # (Internal) op=10(1)(1) MOD dn="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) '
                                    r'MOD dn="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) SRCH base="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) '
                                    r'SRCH base="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) ENTRY dn="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) '
                                    r'ENTRY dn="cn=group,ou=groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) RESULT err=0 tag=48 nentries=1*')
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=1*')
    # (Internal) op=10(1)(1) RESULT err=0 tag=48
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48.*')

    log.info("Check the access logs for MOD operation of the user")
    # op=12 MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
    #      'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
                                        'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com".*')
    if ds_is_older(('1.4.3.9', '1.4.4.3')):
        # Internal) op=12(1)(1) SRCH base="uid=test_user_777, ou=branch1,dc=example,dc=com"
        assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) SRCH base="uid=test_user_777,'
                                        'ou=branch1,dc=example,dc=com".*')
        # (Internal) op=12(1)(1) ENTRY dn="uid=test_user_777, ou=branch1,dc=example,dc=com"
        assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) ENTRY dn="uid=test_user_777,'
                                        'ou=branch1,dc=example,dc=com".*')
    # (Internal) op=12(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=1.*')
    # op=12 RESULT err=0 tag=48
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ RESULT err=0 tag=48.*')

    log.info("Check the access logs for DEL operation of the user")
    # op=15 DEL dn="uid=new_test_user_777,dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ DEL dn="uid=new_test_user_777,dc=example,dc=com".*')
    if ds_is_older(('1.4.3.9', '1.4.4.3')):
        # (Internal) op=15(1)(1) SRCH base="uid=new_test_user_777, dc=example,dc=com"
        assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) SRCH base="uid=new_test_user_777,'
                                        'dc=example,dc=com".*')
        # (Internal) op=15(1)(1) ENTRY dn="uid=new_test_user_777, dc=example,dc=com"
        assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) ENTRY dn="uid=new_test_user_777,'
                                        'dc=example,dc=com".*')
    # (Internal) op=15(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=[0-9]+\([0-9]+\)\([0-9]+\) RESULT err=0 tag=48 nentries=1.*')
    # op=15 RESULT err=0 tag=107
    assert not topo.ds_access_log.match(r'.*op=[0-9]+ RESULT err=0 tag=107.*')

    log.info("Check if the other internal operations have the correct format")
    # conn=Internal(0) op=0
    assert topo.ds_access_log.match(r'.*conn=Internal\([0-9]+\) op=[0-9]+\([0-9]+\)\([0-9]+\).*')


@pytest.mark.skipif(ds_is_older('1.4.2.0'), reason="Not implemented")
@pytest.mark.bz1358706
@pytest.mark.ds49232
def test_access_log_truncated_search_message(topology_st, clean_access_logs):
    """Tests that the access log message is properly truncated when the message is too long

    :id: 0a9af37d-3311-4a2f-ac0a-9a1c631aaf27
    :setup: Standalone instance
    :steps:
        1. Make a search with a 2048+ characters basedn, filter and attribute list
        2. Check the access log has the message and it's truncated
    :expectedresults:
        1. Operation should be successful
        2. Access log should contain truncated basedn, filter and attribute list
    """

    topo = topology_st.standalone

    large_str_base = "".join("cn=test," for _ in range(512))
    large_str_filter = "".join("(cn=test)" for _ in range(512))
    users = UserAccounts(topo, f'{large_str_base}dc=ending')
    users._list_attrlist = [f'cn{i}' for i in range(512)]
    log.info("Make a search")
    users.filter(f'(|(objectclass=tester){large_str_filter}(cn=ending))')

    log.info('Restart the server to flush the logs')
    topo.restart()

    assert topo.ds_access_log.match(r'.*cn=test,cn=test,.*')
    assert topo.ds_access_log.match(r'.*objectClass=tester.*')
    assert topo.ds_access_log.match(r'.*cn10.*')
    assert not topo.ds_access_log.match(r'.*dc=ending.*')
    assert not topo.ds_access_log.match(r'.*cn=ending.*')
    assert not topo.ds_access_log.match(r'.*cn500.*')


@pytest.mark.skipif(ds_is_newer("1.4.3"), reason="rsearch was removed")
@pytest.mark.xfail(ds_is_older('1.4.2.0'), reason="May fail because of bug 1732053")
@pytest.mark.bz1732053
@pytest.mark.ds50510
def test_etime_at_border_of_second(topology_st, clean_access_logs):
    topo = topology_st.standalone

    prog = os.path.join(topo.ds_paths.bin_dir, 'rsearch')

    cmd = [prog]

    # base search
    cmd.extend(['-s', DN_CONFIG])

    # scope of the search
    cmd.extend(['-S', '0'])

    # host / port
    cmd.extend(['-h', HOST_STANDALONE])
    cmd.extend(['-p', str(PORT_STANDALONE)])

    # bound as DM to make it faster
    cmd.extend(['-D', DN_DM])
    cmd.extend(['-w', PASSWORD])

    # filter
    cmd.extend(['-f', "(cn=config)"])

    # 2 samples SRCH
    cmd.extend(['-C', "2"])

    output = subprocess.check_output(cmd)
    topo.stop()

    # No etime with 0.199xxx (everything should be few ms)
    invalid_etime = topo.ds_access_log.match(r'.*etime=0\.19.*')
    if invalid_etime:
        for i in range(len(invalid_etime)):
            log.error('It remains invalid or weird etime: %s' % invalid_etime[i])
    assert not invalid_etime


@pytest.mark.skipif(ds_is_older('1.3.10.1', '1.4.1'), reason="Fail because of bug 1749236")
@pytest.mark.bz1749236
def test_etime_order_of_magnitude(topology_st, clean_access_logs, remove_users, disable_access_log_buffering):
    """Test that the etime reported in the access log has a correct order of magnitude

    :id: e815cfa0-8136-4932-b50f-c3dfac34b0e6
    :setup: Standalone instance
    :steps:
         1. Unset log buffering for the access log
         2. Delete potential existing access logs
         3. Add users
         4. Search users
         5. Restart the server to flush the logs
         6. Parse the access log looking for the SRCH operation log
         7. From the SRCH string get the start time and op number of the operation
         8. From the op num find the associated RESULT string in the access log
         9. From the RESULT string get the end time and the etime for the operation
         10. Calculate the ratio between the calculated elapsed time (end time - start time) and the logged etime
    :expectedresults:
         1. access log buffering is off
         2. Previously existing access logs are deleted
         3. Users are successfully added
         4. Search operation is successful
         5. Server is restarted and logs are flushed
         6. SRCH operation log string is catched
         7. start time and op number are collected
         8. RESULT string is catched from the access log
         9. end time and etime are collected
         10. ratio between calculated elapsed time and logged etime is less or equal to 1
    """

    DSLdapObject(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('add_users')
    add_users(topology_st.standalone, 30)

    log.info ('search users')
    search_users(topology_st.standalone)

    log.info('parse the access logs to get the SRCH string')
    # Here we are looking at the whole string logged for the search request with base ou=People,dc=example,dc=com
    search_str = str(topology_st.standalone.ds_access_log.match(r'.*SRCH base="ou=People,dc=example,dc=com.*'))[1:-1]
    assert len(search_str) > 0

    # the search_str returned looks like :
    # [23/Apr/2020:06:06:14.360857624 -0400] conn=1 op=93 SRCH base="ou=People,dc=example,dc=com" scope=2 filter="(&(objectClass=account)(objectClass=posixaccount)(objectClass=inetOrgPerson)(objectClass=organizationalPerson))" attrs="distinguishedName"

    log.info('get the operation start time from the SRCH string')
    # Here we are getting the sec.nanosec part of the date, '14.360857624' in the example above
    start_time = (search_str.split()[0]).split(':')[3]

    log.info('get the OP number from the SRCH string')
    # Here we are getting the op number, 'op=93' in the above example
    op_num = search_str.split()[3]

    log.info('get the RESULT string matching the SRCH OP number')
    # Here we are looking at the RESULT string for the above search op, 'op=93' in this example
    result_str = str(topology_st.standalone.ds_access_log.match(r'.*{} RESULT*'.format(op_num)))[1:-1]
    assert len(result_str) > 0

    # The result_str returned looks like :
    # For ds older than 1.4.3.8: [23/Apr/2020:06:06:14.366429900 -0400] conn=1 op=93 RESULT err=0 tag=101 nentries=30 etime=0.005723017
    # For ds newer than 1.4.3.8: [21/Oct/2020:09:27:50.095209871 -0400] conn=1 op=96 RESULT err=0 tag=101 nentries=30 wtime=0.000412584 optime=0.005428971 etime=0.005836077

    log.info('get the operation end time from the RESULT string')
    # Here we are getting the sec.nanosec part of the date, '14.366429900' in the above example
    end_time = (result_str.split()[0]).split(':')[3]

    log.info('get the logged etime for the operation from the RESULT string')
    # Here we are getting the etime value, '0.005723017' in the example above
    if ds_is_older('1.4.3.8'):
        etime = result_str.split()[8].split('=')[1][:-3]
    else:
        etime = result_str.split()[10].split('=')[1][:-3]

    log.info('Calculate the ratio between logged etime for the operation and elapsed time from its start time to its end time - should be around 1')
    etime_ratio = (Decimal(end_time) - Decimal(start_time)) // Decimal(etime)
    assert etime_ratio <= 1


@pytest.mark.skipif(ds_is_older('1.4.3.8'), reason="Fail because of bug 1850275")
@pytest.mark.bz1850275
@pytest.mark.bz1924848
def test_optime_and_wtime_keywords(topology_st, clean_access_logs, remove_users, disable_access_log_buffering):
    """Test that the new optime and wtime keywords are present in the access log and have correct values

    :id: dfb4a49d-1cfc-400e-ba43-c107f58d62cf
    :customerscenario: True
    :setup: Standalone instance
    :steps:
         1. Unset log buffering for the access log
         2. Delete potential existing access logs
         3. Add users
         4. Search users
         5. Parse the access log looking for the SRCH operation log
         6. From the SRCH string get the op number of the operation
         7. From the op num find the associated RESULT string in the access log
         8. Search for the wtime optime keywords in the RESULT string
         9. From the RESULT string get the wtime, optime and etime values for the operation
         10. Check that optime + wtime is approximatively etime
    :expectedresults:
         1. access log buffering is off
         2. Previously existing access logs are deleted
         3. Users are successfully added
         4. Search operation is successful
         5. SRCH operation log string is catched
         6. op number is collected
         7. RESULT string is catched from the access log
         8. wtime and optime keywords are collected
         9. wtime, optime and etime values are collected
         10. (optime + wtime) =~ etime
    """

    log.info('add_users')
    add_users(topology_st.standalone, 30)

    log.info ('search users')
    search_users(topology_st.standalone)

    log.info('parse the access logs to get the SRCH string')
    # Here we are looking at the whole string logged for the search request with base ou=People,dc=example,dc=com
    search_str = str(topology_st.standalone.ds_access_log.match(r'.*SRCH base="ou=People,dc=example,dc=com.*'))[1:-1]
    assert len(search_str) > 0

    # the search_str returned looks like :
    # [22/Oct/2020:09:47:11.951316798 -0400] conn=1 op=96 SRCH base="ou=People,dc=example,dc=com" scope=2 filter="(&(objectClass=account)(objectClass=posixaccount)(objectClass=inetOrgPerson)(objectClass=organizationalPerson))" attrs="distinguishedName"

    log.info('get the OP number from the SRCH string')
    # Here we are getting the op number, 'op=96' in the above example
    op_num = search_str.split()[3]

    log.info('get the RESULT string matching the SRCH op number')
    # Here we are looking at the RESULT string for the above search op, 'op=96' in this example
    result_str = str(topology_st.standalone.ds_access_log.match(r'.*{} RESULT*'.format(op_num)))[1:-1]
    assert len(result_str) > 0

    # The result_str returned looks like :
    # [22/Oct/2020:09:47:11.963276018 -0400] conn=1 op=96 RESULT err=0 tag=101 nentries=30 wtime=0.000180294 optime=0.011966632 etime=0.012141311
    log.info('Search for the wtime keyword in the RESULT string')
    assert re.search('wtime', result_str)

    log.info('get the wtime value from the RESULT string')
    wtime_value = result_str.split()[8].split('=')[1][:-3]

    log.info('Search for the optime keyword in the RESULT string')
    assert re.search('optime', result_str)

    log.info('get the optime value from the RESULT string')
    optime_value = result_str.split()[9].split('=')[1][:-3]

    log.info('get the etime value from the RESULT string')
    etime_value = result_str.split()[10].split('=')[1][:-3]

    log.info('Check that (wtime + optime) is approximately equal to etime i.e. their ratio is 1')
    etime_ratio = (Decimal(wtime_value) + Decimal(optime_value)) // Decimal(etime_value)
    assert etime_ratio == 1

    log.info('Perform a compare operation')
    topology_st.standalone.compare_s('uid=testuser1000,ou=people,dc=example,dc=com','uid', 'testuser1000')
    ops = topology_st.standalone.ds_access_log.match('.*CMP dn="uid=testuser1000,ou=people,dc=example,dc=com"')

    log.info('get the wtime and optime values from the RESULT string')
    ops_value = topology_st.standalone.ds_access_log.parse_line(ops[0])
    value = topology_st.standalone.ds_access_log.match(f'.*op={ops_value["op"]} RESULT')
    time_value = topology_st.standalone.ds_access_log.parse_line(value[0])
    wtime = time_value['rem'].split()[3].split('=')[1]
    optime = time_value['rem'].split()[4].split('=')[1]

    log.info('Check that compare operation is not generating negative values for wtime and optime')
    if (Decimal(wtime) > 0) and (Decimal(optime) > 0):
        assert True
    else:
        log.info('wtime and optime values are negatives')
        assert False


@pytest.mark.xfail(ds_is_older('1.3.10.1'), reason="May fail because of bug 1662461")
@pytest.mark.bz1662461
@pytest.mark.ds50428
@pytest.mark.ds49969
def test_log_base_dn_when_invalid_attr_request(topology_st, disable_access_log_buffering):
    """Test that DS correctly logs the base dn when a search with invalid attribute request is performed

    :id: 859de962-c261-4ffb-8705-97bceab1ba2c
    :setup: Standalone instance
    :steps:
         1. Disable the accesslog-logbuffering config parameter
         2. Delete the previous access log
         3. Perform a base search on the DEFAULT_SUFFIX, using ten empty attribute requests
         4. Check the access log file for 'invalid attribute request'
         5. Check the access log file for 'SRCH base="\(null\)"'
         6. Check the access log file for 'SRCH base="DEFAULT_SUFFIX"'
    :expectedresults:
         1. Operations are visible in the access log in real time
         2. Fresh new access log is created
         3. The search operation raises a Protocol error
         4. The access log should have an 'invalid attribute request' message
         5. The access log should not have "\(null\)" as value for the Search base dn
         6. The access log should have the value of DEFAULT_SUFFIX as Search base dn
    """

    entry = DSLdapObject(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('delete the previous access logs to get a fresh new one')
    topology_st.standalone.deleteAccessLogs()

    log.info("Search the default suffix, with invalid '\"\" \"\"' attribute request")
    log.info("A Protocol error exception should be raised, see https://github.com/389ds/389-ds-base/issues/3028")
    # A ldap.PROTOCOL_ERROR exception is expected after 10 empty values
    with pytest.raises(ldap.PROTOCOL_ERROR):
        assert entry.get_attrs_vals_utf8(['', '', '', '', '', '', '', '', '', '', ''])

    # Search for appropriate messages in the access log
    log.info('Check the access logs for correct messages')
    # We should find the 'invalid attribute request' information
    assert topology_st.standalone.ds_access_log.match(r'.*invalid attribute request.*')
    # We should not find a "(null)" base dn mention
    assert not topology_st.standalone.ds_access_log.match(r'.*SRCH base="\(null\)".*')
    # We should find the base dn for the search
    assert topology_st.standalone.ds_access_log.match(r'.*SRCH base="{}".*'.format(DEFAULT_SUFFIX))


@pytest.mark.xfail(ds_is_older('1.3.8', '1.4.2'), reason="May fail because of bug 1676948")
@pytest.mark.bz1676948
@pytest.mark.ds50536
def test_audit_log_rotate_and_check_string(topology_st, clean_access_logs, set_audit_log_config_values_to_rotate):
    """Version string should be logged only once at the top of audit log
    after it is rotated.

    :id: 14dffb22-2f9c-11e9-8a03-54e1ad30572c

    :customerscenario: True

    :setup: Standalone instance

    :steps:
         1. Set nsslapd-auditlog-logging-enabled: on
         2. Set nsslapd-auditlog-maxlogsize: 1
         3. Do modifications to the entry, until audit log file is rotated
         4. Check audit logs

    :expectedresults:
         1. Attribute nsslapd-auditlog-logging-enabled should be set to on
         2. Attribute nsslapd-auditlog-maxlogsize should be set to 1
         3. Audit file should grow till 1MB and then should be rotated
         4. Audit file log should contain version string only once at the top
    """

    standalone = topology_st.standalone
    search_ds = '389-Directory'

    users = UserAccounts(standalone, DEFAULT_SUFFIX)
    user = users.create(properties={
            'uid': 'test_audit_log',
            'cn': 'test',
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '1000',
            'homeDirectory': '/home/test',
        })

    log.info('Doing modifications to rotate audit log')
    audit_log = standalone.ds_paths.audit_log
    while len(glob.glob(audit_log + '*')) == 2:
        user.replace('description', 'test'*100)

    log.info('Doing one more modification just in case')
    user.replace('description', 'test2'*100)

    standalone.stop()

    count = 0
    with open(audit_log) as f:
        log.info('Check that DS string is present on first line')
        assert search_ds in f.readline()
        f.seek(0)

        log.info('Check that DS string is present only once')
        for line in f.readlines():
            if search_ds in line:
                count += 1
        assert count == 1


def test_enable_external_libs_debug_log(topology_st):
    """Check that OpenLDAP logs are successfully enabled and disabled

    :id: b04646e3-9a5e-45ae-ad81-2882c1daf23e
    :setup: Standalone instance
    :steps: 1. Create a user to bind on
            2. Set nsslapd-external-libs-debug-enabled to "on"
            3. Clean the error log
            4. Bind as the user to generate OpenLDAP output
            5. Restart the servers to flush the logs
            6. Check the error log for OpenLDAP debug log
            7. Set nsslapd-external-libs-debug-enabled to "on"
            8. Clean the error log
            9. Bind as the user to generate OpenLDAP output
            10. Restart the servers to flush the logs
            11. Check the error log for OpenLDAP debug log
    :expectedresults: 1. Success
                      2. Success
                      3. Success
                      4. Success
                      5. Success
                      6. Logs are present
                      7. Success
                      8. Success
                      9. Success
                      10. Success
                      11. No logs are present
    """

    standalone = topology_st.standalone

    log.info('Create a user to bind on')
    users = UserAccounts(standalone, DEFAULT_SUFFIX)
    user = users.ensure_state(properties={
            'uid': 'test_audit_log',
            'cn': 'test',
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '1000',
            'homeDirectory': '/home/test',
            'userPassword': PASSWORD
        })

    log.info('Set nsslapd-external-libs-debug-enabled to "on"')
    standalone.config.set('nsslapd-external-libs-debug-enabled', 'on')

    log.info('Clean the error log')
    standalone.deleteErrorLogs()

    log.info('Bind as the user to generate OpenLDAP output')
    user.bind(PASSWORD)

    log.info('Restart the servers to flush the logs')
    standalone.restart()

    log.info('Check the error log for OpenLDAP debug log')
    assert standalone.ds_error_log.match('.*libldap/libber.*')

    log.info('Set nsslapd-external-libs-debug-enabled to "off"')
    standalone.config.set('nsslapd-external-libs-debug-enabled', 'off')

    log.info('Clean the error log')
    standalone.deleteErrorLogs()

    log.info('Bind as the user to generate OpenLDAP output')
    user.bind(PASSWORD)

    log.info('Restart the servers to flush the logs')
    standalone.restart()

    log.info('Check the error log for OpenLDAP debug log')
    assert not standalone.ds_error_log.match('.*libldap/libber.*')


@pytest.mark.skipif(ds_is_older('1.4.3'), reason="Might fail because of bug 1895460")
@pytest.mark.bz1895460
@pytest.mark.ds4593
def test_cert_personality_log_help(topology_st):
    """Test changing the nsSSLPersonalitySSL attribute will raise help message in log

    :id: d6f17f64-d784-438e-89b6-8595bdf6defb
    :customerscenario: True
    :setup: Standalone
    :steps:
        1. Create instance
        2. Change nsSSLPersonalitySSL to wrong certificate nickname
        3. Check there is a help message in error log
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    WRONG_NICK = 'otherNick'
    standalone = topology_st.standalone
    standalone.enable_tls()

    log.info('Change nsSSLPersonalitySSL to wrong certificate nickname')
    config_RSA = RSA(standalone)
    config_RSA.set('nsSSLPersonalitySSL', WRONG_NICK)

    with pytest.raises(subprocess.CalledProcessError):
        standalone.restart()

    assert standalone.ds_error_log.match(r".*Please, make sure that nsSSLPersonalitySSL value "
                                         r"is correctly set to the certificate from NSS database "
                                         r"\(currently, nsSSLPersonalitySSL attribute "
                                         r"is set to '{}'\)\..*".format(WRONG_NICK))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
