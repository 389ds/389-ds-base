# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from random import sample

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.plugins import *
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.idm.organizationalunit import OrganizationalUnits


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
            'cn' : 'testuser%d' % uid,
            'sn' : 'user',
            'uidNumber' : '%d' % uid,
            'gidNumber' : '%d' % uid,
            'homeDirectory' : '/home/testuser%d' % uid
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
    test_user.rename('uid=new_test_user_777', newsuperior=SUFFIX)

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


@pytest.fixture(scope="module")
def add_user_log_level_260(topology_st, enable_plugins):
    log.info('Configure access log level to 260 (4 + 256)')
    access_log_level = '260'
    topology_st.standalone.config.set(LOG_ACCESS_LEVEL, access_log_level)
    add_group_and_perform_user_operations(topology_st)


@pytest.fixture(scope="module")
def add_user_log_level_516(topology_st, enable_plugins):
    log.info('Configure access log level to 516 (4 + 512)')
    access_log_level = '516'
    topology_st.standalone.config.set(LOG_ACCESS_LEVEL, access_log_level)
    add_group_and_perform_user_operations(topology_st)


@pytest.fixture(scope="module")
def add_user_log_level_131076(topology_st, enable_plugins):
    log.info('Configure access log level to 131076 (4 + 131072)')
    access_log_level = '131076'
    topology_st.standalone.config.set(LOG_ACCESS_LEVEL, access_log_level)
    add_group_and_perform_user_operations(topology_st)


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
    assert (default == "on")
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
        result = topology_st.standalone.config.set(PLUGIN_TIMESTAMP, 'JUNK')


@pytest.mark.bz1273549
def test_log_plugin_on(topology_st):
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
def test_log_plugin_off(topology_st):
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
    search_users(topology_st.standalone)

    log.info('Restart the server to flush the logs')
    topology_st.standalone.restart(timeout=10)

    log.info('check access log that microseconds are not present')
    access_log_lines = topology_st.standalone.ds_access_log.readlines()
    assert len(access_log_lines) > 0
    assert not topology_st.standalone.ds_access_log.match(r'^\[.+\d{9}.+\].+')


@pytest.mark.bz1358706
@pytest.mark.ds49029
def test_internal_log_server_level_0(topology_st):
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

    log.info('Delete the previous access logs')
    topo.deleteAccessLogs()

    log.info('Set nsslapd-plugin-logging to on')
    topo.config.set(PLUGIN_LOGGING, 'ON')

    log.info('Configure access log level to 0')
    access_log_level = '0'
    topo.config.set(LOG_ACCESS_LEVEL, access_log_level)

    log.info('Restart the server to flush the logs')
    topo.restart()

    # These comments contain lines we are trying to find without regex
    log.info("Check if access log does not contain internal log of MOD operation")
    # (Internal) op=2(2)(1) SRCH base="cn=config
    assert not topo.ds_access_log.match(r'.*\(Internal\) op=2\(2\)\(1\) SRCH base="cn=config.*')
    # (Internal) op=2(2)(1) RESULT err=0 tag=48 nentries=1
    assert not topo.ds_access_log.match(r'.*\(Internal\) op=2\(2\)\(1\) RESULT err=0 tag=48 nentries=1.*')

    log.info("Check if the other internal operations are not present")
    # conn=Internal(0) op=0
    assert not topo.ds_access_log.match(r'.*conn=Internal\(0\) op=0.*')


@pytest.mark.bz1358706
@pytest.mark.ds49029
def test_internal_log_server_level_4(topology_st):
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

    log.info('Delete the previous access logs for the next test')
    topo.deleteAccessLogs()

    log.info('Set nsslapd-plugin-logging to on')
    topo.config.set(PLUGIN_LOGGING, 'ON')

    log.info('Configure access log level to 4')
    access_log_level = '4'
    topo.config.set(LOG_ACCESS_LEVEL, access_log_level)

    log.info('Restart the server to flush the logs')
    topo.restart()

    # These comments contain lines we are trying to find without regex
    log.info("Check if access log contains internal MOD operation in correct format")
    # (Internal) op=2(2)(1) SRCH base="cn=config
    assert topo.ds_access_log.match(r'.*\(Internal\) op=2\(2\)\(1\) SRCH base="cn=config.*')
    # (Internal) op=2(2)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=2\(2\)\(1\) RESULT err=0 tag=48 nentries=1.*')

    log.info("Check if the other internal operations have the correct format")
    # conn=Internal(0) op=0
    assert topo.ds_access_log.match(r'.*conn=Internal\(0\) op=0.*')

    log.info('Delete the previous access logs for the next test')
    topo.deleteAccessLogs()


@pytest.mark.bz1358706
@pytest.mark.ds49029
def test_internal_log_level_260(topology_st, add_user_log_level_260):
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

    # These comments contain lines we are trying to find without regex
    log.info("Check the access logs for ADD operation of the user")
    # op=10 ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*op=10 ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com".*')
    # (Internal) op=10(1)(1) MOD dn="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(1\) MOD dn="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) SRCH base="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(2\) SRCH base="cn=group,'
                                    r'ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(2\) RESULT err=0 tag=48 nentries=1*')
    # (Internal) op=10(1)(1) RESULT err=0 tag=48
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(1\) RESULT err=0 tag=48.*')
    # op=10 RESULT err=0 tag=105
    assert topo.ds_access_log.match(r'.*op=10 RESULT err=0 tag=105.*')

    log.info("Check the access logs for MOD operation of the user")
    # op=12 MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
    #      'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*op=12 MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
                                    'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com".*')
    # (Internal) op=12(1)(1) SRCH base="uid=test_user_777, ou=branch1,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=12\(1\)\(1\) SRCH base="uid=test_user_777,'
                                    'ou=branch1,dc=example,dc=com".*')
    # (Internal) op=12(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=12\(1\)\(1\) RESULT err=0 tag=48 nentries=1.*')
    # op=12 RESULT err=0 tag=109
    assert topo.ds_access_log.match(r'.*op=12 RESULT err=0 tag=109.*')

    log.info("Check the access logs for DEL operation of the user")
    # op=15 DEL dn="uid=new_test_user_777,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*op=15 DEL dn="uid=new_test_user_777,dc=example,dc=com".*')
    # (Internal) op=15(1)(1) SRCH base="uid=new_test_user_777, dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=15\(1\)\(1\) SRCH base="uid=new_test_user_777,'
                                    'dc=example,dc=com".*')
    # (Internal) op=15(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=15\(1\)\(1\) RESULT err=0 tag=48 nentries=1.*')
    # op=15 RESULT err=0 tag=107
    assert topo.ds_access_log.match(r'.*op=15 RESULT err=0 tag=107.*')

    log.info("Check if the other internal operations have the correct format")
    # conn=Internal(0) op=0
    assert topo.ds_access_log.match(r'.*conn=Internal\(0\) op=0.*')

    log.info('Delete the previous access logs for the next test')
    topo.deleteAccessLogs()


@pytest.mark.bz1358706
@pytest.mark.ds49029
def test_internal_log_level_131076(topology_st, add_user_log_level_131076):
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

    # These comments contain lines we are trying to find without regex
    log.info("Check the access logs for ADD operation of the user")
    # op=10 ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=10 ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com".*')
    # (Internal) op=10(1)(1) MOD dn="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(1\) MOD dn="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) SRCH base="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(2\) SRCH base="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) RESULT err=0 tag=48 nentries=1*')
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(2\) RESULT err=0 tag=48 nentries=1*')
    # (Internal) op=10(1)(1) RESULT err=0 tag=48
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(1\) RESULT err=0 tag=48.*')
    # op=10 RESULT err=0 tag=105
    assert not topo.ds_access_log.match(r'.*op=10 RESULT err=0 tag=105.*')

    log.info("Check the access logs for MOD operation of the user")
    # op=12 MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
    #      'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=12 MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
                                        'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com".*')
    # (Internal) op=12(1)(1) SRCH base="uid=test_user_777, ou=branch1,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=12\(1\)\(1\) SRCH base="uid=test_user_777,'
                                    'ou=branch1,dc=example,dc=com".*')
    # (Internal) op=12(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=12\(1\)\(1\) RESULT err=0 tag=48 nentries=1.*')
    # op=12 RESULT err=0 tag=109
    assert not topo.ds_access_log.match(r'.*op=12 RESULT err=0 tag=109.*')

    log.info("Check the access logs for DEL operation of the user")
    # op=15 DEL dn="uid=new_test_user_777,dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=15 DEL dn="uid=new_test_user_777,dc=example,dc=com".*')
    # (Internal) op=15(1)(1) SRCH base="uid=new_test_user_777, dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=15\(1\)\(1\) SRCH base="uid=new_test_user_777,'
                                    'dc=example,dc=com".*')
    # (Internal) op=15(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=15\(1\)\(1\) RESULT err=0 tag=48 nentries=1.*')
    # op=15 RESULT err=0 tag=107
    assert not topo.ds_access_log.match(r'.*op=15 RESULT err=0 tag=107.*')

    log.info("Check if the other internal operations have the correct format")
    # conn=Internal(0) op=0
    assert topo.ds_access_log.match(r'.*conn=Internal\(0\) op=0.*')

    log.info('Delete the previous access logs for the next test')
    topo.deleteAccessLogs()


@pytest.mark.bz1358706
@pytest.mark.ds49029
def test_internal_log_level_516(topology_st, add_user_log_level_516):
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

    # These comments contain lines we are trying to find without regex
    log.info("Check the access logs for ADD operation of the user")
    # op=10 ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=10 ADD dn="uid=test_user_777,ou=branch1,dc=example,dc=com".*')
    # (Internal) op=10(1)(1) MOD dn="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(1\) MOD dn="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) SRCH base="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(2\) SRCH base="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) ENTRY dn="cn=group,ou=Groups,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(2\) ENTRY dn="cn=group,ou=Groups,dc=example,dc=com".*')
    # (Internal) op=10(1)(2) RESULT err=0 tag=48 nentries=1*')
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(2\) RESULT err=0 tag=48 nentries=1*')
    # (Internal) op=10(1)(1) RESULT err=0 tag=48
    assert topo.ds_access_log.match(r'.*\(Internal\) op=10\(1\)\(1\) RESULT err=0 tag=48.*')
    # op=10 RESULT err=0 tag=105
    assert not topo.ds_access_log.match(r'.*op=10 RESULT err=0 tag=105.*')

    log.info("Check the access logs for MOD operation of the user")
    # op=12 MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
    #      'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=12 MODRDN dn="uid=test_user_777,ou=branch1,dc=example,dc=com" '
                                        'newrdn="uid=new_test_user_777" newsuperior="dc=example,dc=com".*')
    # Internal) op=12(1)(1) SRCH base="uid=test_user_777, ou=branch1,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=12\(1\)\(1\) SRCH base="uid=test_user_777,'
                                    'ou=branch1,dc=example,dc=com".*')
    # (Internal) op=12(1)(1) ENTRY dn="uid=test_user_777, ou=branch1,dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=12\(1\)\(1\) ENTRY dn="uid=test_user_777,'
                                    'ou=branch1,dc=example,dc=com".*')
    # (Internal) op=12(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=12\(1\)\(1\) RESULT err=0 tag=48 nentries=1.*')
    # op=12 RESULT err=0 tag=109
    assert not topo.ds_access_log.match(r'.*op=12 RESULT err=0 tag=109.*')

    log.info("Check the access logs for DEL operation of the user")
    # op=15 DEL dn="uid=new_test_user_777,dc=example,dc=com"
    assert not topo.ds_access_log.match(r'.*op=15 DEL dn="uid=new_test_user_777,dc=example,dc=com".*')
    # (Internal) op=15(1)(1) SRCH base="uid=new_test_user_777, dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=15\(1\)\(1\) SRCH base="uid=new_test_user_777,'
                                    'dc=example,dc=com".*')
    # (Internal) op=15(1)(1) ENTRY dn="uid=new_test_user_777, dc=example,dc=com"
    assert topo.ds_access_log.match(r'.*\(Internal\) op=15\(1\)\(1\) ENTRY dn="uid=new_test_user_777,'
                                    'dc=example,dc=com".*')
    # (Internal) op=15(1)(1) RESULT err=0 tag=48 nentries=1
    assert topo.ds_access_log.match(r'.*\(Internal\) op=15\(1\)\(1\) RESULT err=0 tag=48 nentries=1.*')
    # op=15 RESULT err=0 tag=107
    assert not topo.ds_access_log.match(r'.*op=15 RESULT err=0 tag=107.*')

    log.info("Check if the other internal operations have the correct format")
    # conn=Internal(0) op=0
    assert topo.ds_access_log.match(r'.*conn=Internal\(0\) op=0.*')

    log.info('Delete the previous access logs for the next test')
    topo.deleteAccessLogs()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
