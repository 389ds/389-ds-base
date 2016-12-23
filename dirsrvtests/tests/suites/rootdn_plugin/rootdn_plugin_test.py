# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import socket

import pytest
from lib389.tasks import *
from lib389.tools import DirSrvTools
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

PLUGIN_DN = 'cn=' + PLUGIN_ROOTDN_ACCESS + ',cn=plugins,cn=config'
USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX


def test_rootdn_init(topology_st):
    '''
    Initialize our setup to test the ROot DN Access Control Plugin

        Test the following access control type:

        - Allowed IP address *
        - Denied IP address *
        - Specific time window
        - Days allowed access
        - Allowed host *
        - Denied host *

        * means mulitple valued
    '''

    log.info('Initializing root DN test suite...')

    #
    # Set an aci so we can modify the plugin after we deny the Root DN
    #
    ACI = ('(target ="ldap:///cn=config")(targetattr = "*")(version 3.0' +
           ';acl "all access";allow (all)(userdn="ldap:///anyone");)')
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_ADD, 'aci', ACI)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_init: Failed to add aci to config: error ' +
                  e.message['desc'])
        assert False

    #
    # Create a user to modify the config
    #
    try:
        topology_st.standalone.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                                       'uid': 'user1',
                                                       'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_init: Failed to add test user ' + USER1_DN + ': error ' +
                  e.message['desc'])
        assert False

    #
    # Enable dynamic plugins
    #
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_init: Failed to set dynamic plugins: error ' + e.message['desc'])
        assert False

    #
    # Enable the plugin (aftewr enabling dynamic plugins)
    #
    topology_st.standalone.plugins.enable(PLUGIN_ROOTDN_ACCESS)

    log.info('test_rootdn_init: Initialized root DN test suite.')


def test_rootdn_access_specific_time(topology_st):
    '''
    Test binding inside and outside of a specific time
    '''

    log.info('Running test_rootdn_access_specific_time...')

    # Get the current time, and bump it ahead twohours
    current_hour = time.strftime("%H")
    if int(current_hour) > 12:
        open_time = '0200'
        close_time = '0400'
    else:
        open_time = '1600'
        close_time = '1800'

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD, 'rootdn-open-time', open_time),
                                                    (ldap.MOD_ADD, 'rootdn-close-time', close_time)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: Failed to set (blocking) open/close times: error ' +
                  e.message['desc'])
        assert False

    #
    # Bind as Root DN - should fail
    #
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        succeeded = True
    except ldap.LDAPError as e:
        succeeded = False

    if succeeded:
        log.fatal('test_rootdn_access_specific_time: Root DN was incorrectly able to bind')
        assert False

    #
    # Set config to allow the entire day
    #
    try:
        topology_st.standalone.simple_bind_s(USER1_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: test_rootdn: failed to bind as user1')
        assert False

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', '0000'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', '2359')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: Failed to set (open) open/close times: error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    #
    # Cleanup - undo the changes we made so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-open-time', None),
                                                    (ldap.MOD_DELETE, 'rootdn-close-time', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: Failed to delete open and close time: error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    log.info('test_rootdn_access_specific_time: PASSED')


def test_rootdn_access_day_of_week(topology_st):
    '''
    Test the days of week feature
    '''

    log.info('Running test_rootdn_access_day_of_week...')

    days = ('Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat')
    day = int(time.strftime("%w", time.gmtime()))

    if day == 6:
        # Handle the roll over from Saturday into Sunday
        deny_days = days[1] + ', ' + days[2]
        allow_days = days[6] + ',' + days[0]
    elif day > 3:
        deny_days = days[0] + ', ' + days[1]
        allow_days = days[day] + ',' + days[day - 1]
    else:
        deny_days = days[4] + ',' + days[5]
        allow_days = days[day] + ',' + days[day + 1]

    log.info('Today:        ' + days[day])
    log.info('Allowed days: ' + allow_days)
    log.info('Deny days:    ' + deny_days)

    #
    # Set the deny days
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-days-allowed',
                                                     deny_days)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: Failed to set the deny days: error ' +
                  e.message['desc'])
        assert False

    #
    # Bind as Root DN - should fail
    #
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        succeeded = True
    except ldap.LDAPError as e:
        succeeded = False

    if succeeded:
        log.fatal('test_rootdn_access_day_of_week: Root DN was incorrectly able to bind')
        assert False

    #
    # Set the allow days
    #
    try:
        topology_st.standalone.simple_bind_s(USER1_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: : failed to bind as user1')
        assert False

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-days-allowed',
                                                     allow_days)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: Failed to set the deny days: error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    #
    # Cleanup - undo the changes we made so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-days-allowed', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: Failed to set rootDN plugin config: error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    log.info('test_rootdn_access_day_of_week: PASSED')


def test_rootdn_access_denied_ip(topology_st):
    '''
    Test denied IP feature - we can just test denying 127.0.01
    '''

    log.info('Running test_rootdn_access_denied_ip...')

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                     'rootdn-deny-ip',
                                                     '127.0.0.1'),
                                                    (ldap.MOD_ADD,
                                                     'rootdn-deny-ip',
                                                     '::1')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: Failed to set rootDN plugin config: error ' +
                  e.message['desc'])
        assert False

    #
    # Bind as Root DN - should fail
    #
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        succeeded = True
    except ldap.LDAPError as e:
        succeeded = False

    if succeeded:
        log.fatal('test_rootdn_access_denied_ip: Root DN was incorrectly able to bind')
        assert False

    #
    # Change the denied IP so root DN succeeds
    #
    try:
        topology_st.standalone.simple_bind_s(USER1_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: : failed to bind as user1')
        assert False

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-deny-ip', '255.255.255.255')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: Failed to set rootDN plugin config: error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    #
    # Cleanup - undo the changes we made so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-deny-ip', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: Failed to set rootDN plugin config: error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    log.info('test_rootdn_access_denied_ip: PASSED')


def test_rootdn_access_denied_host(topology_st):
    '''
    Test denied Host feature - we can just test denying localhost
    '''

    log.info('Running test_rootdn_access_denied_host...')
    hostname = socket.gethostname()
    localhost = DirSrvTools.getLocalhost()
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD,
                                                     'rootdn-deny-host',
                                                     hostname)])
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD,
                                                     'rootdn-deny-host',
                                                     localhost)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: Failed to set deny host: error ' +
                  e.message['desc'])
        assert False

    #
    # Bind as Root DN - should fail
    #
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        succeeded = True
    except ldap.LDAPError as e:
        succeeded = False

    if succeeded:
        log.fatal('test_rootdn_access_denied_host: Root DN was incorrectly able to bind')
        assert False

    #
    # Change the denied host so root DN succeeds
    #
    try:
        topology_st.standalone.simple_bind_s(USER1_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: : failed to bind as user1')
        assert False

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-deny-host', 'i.dont.exist.com')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: Failed to set rootDN plugin config: error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    #
    # Cleanup - undo the changes we made so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-deny-host', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: Failed to set rootDN plugin config: error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    log.info('test_rootdn_access_denied_host: PASSED')


def test_rootdn_access_allowed_ip(topology_st):
    '''
    Test allowed ip feature
    '''

    log.info('Running test_rootdn_access_allowed_ip...')

    #
    # Set allowed host to an unknown host - blocks the Root DN
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-ip', '255.255.255.255')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: Failed to set allowed host: error ' +
                  e.message['desc'])
        assert False

    #
    # Bind as Root DN - should fail
    #
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        succeeded = True
    except ldap.LDAPError as e:
        succeeded = False

    if succeeded:
        log.fatal('test_rootdn_access_allowed_ip: Root DN was incorrectly able to bind')
        assert False

    #
    # Allow localhost
    #
    try:
        topology_st.standalone.simple_bind_s(USER1_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: : failed to bind as user1')
        assert False

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-ip', '127.0.0.1'),
                                                    (ldap.MOD_ADD, 'rootdn-allow-ip', '::1')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: Failed to set allowed host: error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    #
    # Cleanup - undo everything we did so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-allow-ip', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: Failed to delete(rootdn-allow-ip): error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    log.info('test_rootdn_access_allowed_ip: PASSED')


def test_rootdn_access_allowed_host(topology_st):
    '''
    Test allowed ip feature
    '''

    log.info('Running test_rootdn_access_allowed_host...')

    #
    # Set allowed host to an unknown host - blocks the Root DN
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-host', 'i.dont.exist.com')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: Failed to set allowed host: error ' +
                  e.message['desc'])
        assert False

    #
    # Bind as Root DN - should fail
    #
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        succeeded = True
    except ldap.LDAPError as e:
        succeeded = False

    if succeeded:
        log.fatal('test_rootdn_access_allowed_host: Root DN was incorrectly able to bind')
        assert False

    #
    # Allow localhost
    #
    try:
        topology_st.standalone.simple_bind_s(USER1_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: : failed to bind as user1')
        assert False

    hostname = socket.gethostname()
    localhost = DirSrvTools.getLocalhost()
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD,
                                                     'rootdn-allow-host',
                                                     localhost)])
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD,
                                                     'rootdn-allow-host',
                                                     hostname)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: Failed to set allowed host: error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    #
    # Cleanup - undo everything we did so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-allow-host', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: Failed to delete(rootdn-allow-host): error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: Root DN bind failed unexpectedly failed: error ' +
                  e.message['desc'])
        assert False

    log.info('test_rootdn_access_allowed_host: PASSED')


def test_rootdn_config_validate(topology_st):
    '''
    Test configuration validation

    test single valued attributes: rootdn-open-time,
                                   rootdn-close-time,
                                   rootdn-days-allowed

    '''

    log.info('Running test_rootdn_config_validate...')

    #
    # Test rootdn-open-time
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', '0000')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to just add "rootdn-open-time" ')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD, 'rootdn-open-time', '0000'),
                                                    (ldap.MOD_ADD, 'rootdn-open-time', '0001')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add multiple "rootdn-open-time"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', '-1'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', '0000')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-open-time: -1"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', '2400'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', '0000')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-open-time: 2400"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', 'aaaaa'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', '0000')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-open-time: aaaaa"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test rootdn-close-time
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-close-time', '0000')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add just "rootdn-close-time"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD, 'rootdn-close-time', '0000'),
                                                    (ldap.MOD_ADD, 'rootdn-close-time', '0001')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add multiple "rootdn-open-time"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', '0000'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', '-1')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-close-time: -1"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', '0000'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', '2400')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-close-time: 2400"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', '0000'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', 'aaaaa')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-close-time: aaaaa"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test days allowed
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD, 'rootdn-days-allowed', 'Mon'),
                                                    (ldap.MOD_ADD, 'rootdn-days-allowed', 'Tue')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add two "rootdn-days-allowed"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-days-allowed', 'Mon1')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-days-allowed: Mon1"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-days-allowed', 'Tue, Mon1')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-days-allowed: Tue, Mon1"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-days-allowed', 'm111m')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-days-allowed: 111"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-days-allowed', 'Gur')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-days-allowed: Gur"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test allow ips
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-ip', '12.12.Z.12')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-allow-ip: 12.12.Z.12"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test deny ips
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-deny-ip', '12.12.Z.12')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-deny-ip: 12.12.Z.12"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test allow hosts
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-host', 'host._.com')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-allow-host: host._.com"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test deny hosts
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-deny-host', 'host.####.com')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-deny-host: host.####.com"')
        assert False
    except ldap.LDAPError:
        pass

    log.info('test_rootdn_config_validate: PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
