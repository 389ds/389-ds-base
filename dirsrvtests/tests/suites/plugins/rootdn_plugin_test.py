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
import ldap
import pytest
from lib389.utils import *
from lib389.tasks import *
from lib389.tools import DirSrvTools
from lib389.topologies import topology_st

from lib389._constants import PLUGIN_ROOTDN_ACCESS, DN_CONFIG, DEFAULT_SUFFIX, DN_DM, PASSWORD, LOCALHOST_IP

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

PLUGIN_DN = 'cn=' + PLUGIN_ROOTDN_ACCESS + ',cn=plugins,cn=config'
USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX


@pytest.fixture(scope="module")
def rootdn_setup(topology_st):
    """Initialize our setup to test the Root DN Access Control Plugin

    Test the following access control type:

    - Allowed IP address *
    - Denied IP address *
    - Specific time window
    - Days allowed access
    - Allowed host *
    - Denied host *

    * means mulitple valued
    """

    log.info('Initializing root DN test suite...')

    #
    # Set an aci so we can modify the plugin after we deny the Root DN
    #
    ACI = ('(target ="ldap:///cn=config")(targetattr = "*")(version 3.0' +
           ';acl "all access";allow (all)(userdn="ldap:///anyone");)')
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_ADD, 'aci', ensure_bytes(ACI))])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_init: Failed to add aci to config: error {}'
                  .format(e))
        assert False

    #
    # Create a user to modify the config
    #
    try:
        topology_st.standalone.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                                       'uid': 'user1',
                                                       'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_init: Failed to add test user ' + USER1_DN + ': error {}'
                  .format(e))
        assert False

    #
    # Enable dynamic plugins
    #
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', b'on')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_init: Failed to set dynamic plugins: error {}'.format(e))
        assert False

    #
    # Enable the plugin (aftewr enabling dynamic plugins)
    #
    topology_st.standalone.plugins.enable(PLUGIN_ROOTDN_ACCESS)

    log.info('test_rootdn_init: Initialized root DN test suite.')


def test_rootdn_access_specific_time(topology_st, rootdn_setup):
    """Test binding inside and outside of a specific time

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e8
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Get the current time, and bump it ahead twohours
        2. Bind as Root DN
        3. Set config to allow the entire day
        4. Bind as Root DN
        5. Cleanup - undo the changes we made so the next test has a clean slate
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
        5. Success
    """

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
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD, 'rootdn-open-time', ensure_bytes(open_time)),
                                                    (ldap.MOD_ADD, 'rootdn-close-time', ensure_bytes(close_time))])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: Failed to set (blocking) open/close times: error {}'
                  .format(e))
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
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', b'0000'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', b'2359')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: Failed to set (open) open/close times: error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    #
    # Cleanup - undo the changes we made so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-open-time', ensure_bytes(None)),
                                                    (ldap.MOD_DELETE, 'rootdn-close-time', ensure_bytes(None))])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: Failed to delete open and close time: error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_specific_time: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    log.info('test_rootdn_access_specific_time: PASSED')


def test_rootdn_access_day_of_week(topology_st, rootdn_setup):
    """Test the days of week feature

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e1
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Set the deny days
        2. Bind as Root DN
        3. Set the allow days
        4. Bind as Root DN
        5. Cleanup - undo the changes we made so the next test has a clean slate
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
        5. Success
    """

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
                                                     ensure_bytes(deny_days))])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: Failed to set the deny days: error {}'
                  .format(e))
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
                                                     ensure_bytes(allow_days))])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: Failed to set the deny days: error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    #
    # Cleanup - undo the changes we made so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-days-allowed', ensure_bytes(None))])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: Failed to set rootDN plugin config: error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_day_of_week: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    log.info('test_rootdn_access_day_of_week: PASSED')


def test_rootdn_access_denied_ip(topology_st, rootdn_setup):
    """Test denied IP feature - we can just test denying 127.0.0.1

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e2
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Set rootdn-deny-ip to '127.0.0.1' and '::1'
        2. Bind as Root DN
        3. Change the denied IP so root DN succeeds
        4. Bind as Root DN
        5. Cleanup - undo the changes we made so the next test has a clean slate
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
        5. Success
    """

    log.info('Running test_rootdn_access_denied_ip...')
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                     'rootdn-deny-ip',
                                                     b'127.0.0.1'),
                                                    (ldap.MOD_ADD,
                                                     'rootdn-deny-ip',
                                                     b'::1')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: Failed to set rootDN plugin config: error {}'
                  .format(e))
        assert False

    #
    # Bind as Root DN - should fail
    #
    try:
        conn = ldap.initialize('ldap://{}:{}'.format(LOCALHOST_IP, topology_st.standalone.port))
        topology_st.standalone.restart()
        conn.simple_bind_s(DN_DM, PASSWORD)
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
        log.fatal('test_rootdn_access_denied_ip: failed to bind as user1')
        assert False

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-deny-ip', b'255.255.255.255')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: Failed to set rootDN plugin config: error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    #
    # Cleanup - undo the changes we made so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-deny-ip', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: Failed to set rootDN plugin config: error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_ip: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    log.info('test_rootdn_access_denied_ip: PASSED')


def test_rootdn_access_denied_host(topology_st, rootdn_setup):
    """Test denied Host feature - we can just test denying localhost

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e3
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Set rootdn-deny-host to hostname (localhost if not accessable)
        2. Bind as Root DN
        3. Change the denied host so root DN succeeds
        4. Bind as Root DN
        5. Cleanup - undo the changes we made so the next test has a clean slate
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
        5. Success
    """

    log.info('Running test_rootdn_access_denied_host...')
    hostname = socket.gethostname()
    localhost = DirSrvTools.getLocalhost()
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD,
                                                     'rootdn-deny-host',
                                                     ensure_bytes(hostname))])
        if localhost != hostname:
            topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD,
                                                         'rootdn-deny-host',
                                                         ensure_bytes(localhost))])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: Failed to set deny host: error {}'
                  .format(e))
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
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-deny-host', b'i.dont.exist.com')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: Failed to set rootDN plugin config: error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    #
    # Cleanup - undo the changes we made so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-deny-host', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: Failed to set rootDN plugin config: error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_denied_host: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    log.info('test_rootdn_access_denied_host: PASSED')


def test_rootdn_access_allowed_ip(topology_st, rootdn_setup):
    """Test allowed ip feature

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e4
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Set allowed ip to 255.255.255.255 - blocks the Root DN
        2. Bind as Root DN
        3. Allow localhost
        4. Bind as Root DN
        5. Cleanup - undo the changes we made so the next test has a clean slate
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
        5. Success
    """

    log.info('Running test_rootdn_access_allowed_ip...')

    #
    # Set allowed ip to 255.255.255.255 - blocks the Root DN
    #
    try:
        conn = ldap.initialize('ldap://{}:{}'.format(LOCALHOST_IP, topology_st.standalone.port))
        topology_st.standalone.restart()
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-ip', b'255.255.255.255')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: Failed to set allowed host: error {}'
                  .format(e))
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
        #ipv4 = socket.gethostbyname(socket.gethostname())
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-ip', b'127.0.0.1'),
                                                    (ldap.MOD_ADD, 'rootdn-allow-ip', b'::1')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: Failed to set allowed host: error {}'
                  .format(e))
        assert False

    try:
        #topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        conn.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    #
    # Cleanup - undo everything we did so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-allow-ip', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: Failed to delete(rootdn-allow-ip): error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_ip: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    log.info('test_rootdn_access_allowed_ip: PASSED')


def test_rootdn_access_allowed_host(topology_st, rootdn_setup):
    """Test allowed host feature

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e5
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Set allowed host to an unknown host - blocks the Root DN
        2. Bind as Root DN
        3. Allow localhost
        4. Bind as Root DN
        5. Cleanup - undo the changes we made so the next test has a clean slate
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
        5. Success
    """

    log.info('Running test_rootdn_access_allowed_host...')

    #
    # Set allowed host to an unknown host - blocks the Root DN
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-host', b'i.dont.exist.com')])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: Failed to set allowed host: error {}'
                  .format(e))
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
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE,
                                                     'rootdn-allow-host',
                                                     None)])
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD,
                                                     'rootdn-allow-host',
                                                     ensure_bytes(localhost))])
        if hostname != localhost:
            topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD,
                                                         'rootdn-allow-host',
                                                         ensure_bytes(hostname))])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: Failed to set allowed host: error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    #
    # Cleanup - undo everything we did so the next test has a clean slate
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_DELETE, 'rootdn-allow-host', None)])
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: Failed to delete(rootdn-allow-host): error {}'
                  .format(e))
        assert False

    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('test_rootdn_access_allowed_host: Root DN bind failed unexpectedly failed: error {}'
                  .format(e))
        assert False

    log.info('test_rootdn_access_allowed_host: PASSED')


def test_rootdn_config_validate(topology_st, rootdn_setup):
    """Test plugin configuration validation

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e6
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Replace 'rootdn-open-time' with '0000'
        2. Add 'rootdn-open-time': '0000' and 'rootdn-open-time': '0001'
        3. Replace 'rootdn-open-time' with '-1' and 'rootdn-close-time' with '0000'
        4. Replace 'rootdn-open-time' with '2400' and 'rootdn-close-time' with '0000'
        5. Replace 'rootdn-open-time' with 'aaaaa' and 'rootdn-close-time' with '0000'
        6. Replace 'rootdn-close-time' with '0000'
        7. Add 'rootdn-close-time': '0000' and 'rootdn-close-time': '0001'
        8. Replace 'rootdn-open-time' with '0000' and 'rootdn-close-time' with '-1'
        9. Replace 'rootdn-open-time' with '0000' and 'rootdn-close-time' with '2400'
        10. Replace 'rootdn-open-time' with '0000' and 'rootdn-close-time' with 'aaaaa'
        11. Add 'rootdn-days-allowed': 'Mon' and 'rootdn-days-allowed': 'Tue'
        12. Replace 'rootdn-days-allowed' with 'Mon1'
        13. Replace 'rootdn-days-allowed' with 'Tue, Mon1'
        14. Replace 'rootdn-days-allowed' with 'm111m'
        15. Replace 'rootdn-days-allowed' with 'Gur'
        16. Replace 'rootdn-allow-ip' with '12.12.Z.12'
        17. Replace 'rootdn-deny-ip' with '12.12.Z.12'
        18. Replace 'rootdn-allow-host' with 'host._.com'
        19. Replace 'rootdn-deny-host' with 'host.####.com'
    :expectedresults:
        1. Should fail
        2. Should fail
        3. Should fail
        4. Should fail
        5. Should fail
        6. Should fail
        7. Should fail
        8. Should fail
        9. Should fail
        10. Should fail
        11. Should fail
        12. Should fail
        13. Should fail
        14. Should fail
        15. Should fail
        16. Should fail
        17. Should fail
        18. Should fail
        19. Should fail
    """

    log.info('Running test_rootdn_config_validate...')

    #
    # Test rootdn-open-time
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', b'0000')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to just add "rootdn-open-time" ')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD, 'rootdn-open-time', b'0000'),
                                                    (ldap.MOD_ADD, 'rootdn-open-time', b'0001')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add multiple "rootdn-open-time"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', b'-1'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', b'0000')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-open-time: -1"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', b'2400'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', b'0000')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-open-time: 2400"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', b'aaaaa'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', b'0000')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-open-time: aaaaa"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test rootdn-close-time
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-close-time', b'0000')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add just "rootdn-close-time"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD, 'rootdn-close-time', b'0000'),
                                                    (ldap.MOD_ADD, 'rootdn-close-time', b'0001')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add multiple "rootdn-open-time"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', b'0000'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', b'-1')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-close-time: -1"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', b'0000'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', b'2400')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-close-time: 2400"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-open-time', b'0000'),
                                                    (ldap.MOD_REPLACE, 'rootdn-close-time', b'aaaaa')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-close-time: aaaaa"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test days allowed
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_ADD, 'rootdn-days-allowed', b'Mon'),
                                                    (ldap.MOD_ADD, 'rootdn-days-allowed', b'Tue')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add two "rootdn-days-allowed"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-days-allowed', b'Mon1')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-days-allowed: Mon1"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-days-allowed', b'Tue, Mon1')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-days-allowed: Tue, Mon1"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-days-allowed', b'm111m')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-days-allowed: 111"')
        assert False
    except ldap.LDAPError:
        pass

    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-days-allowed', b'Gur')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-days-allowed: Gur"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test allow ips
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-ip', b'12.12.Z.12')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-allow-ip: 12.12.Z.12"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test deny ips
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-deny-ip', b'12.12.Z.12')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-deny-ip: 12.12.Z.12"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test allow hosts
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-allow-host', b'host._.com')])
        log.fatal('test_rootdn_config_validate: Incorrectly allowed to add invalid "rootdn-allow-host: host._.com"')
        assert False
    except ldap.LDAPError:
        pass

    #
    # Test deny hosts
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE, 'rootdn-deny-host', b'host.####.com')])
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
