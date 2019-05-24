# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
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
import uuid
from lib389.utils import *
from lib389.tasks import *
from lib389.tools import DirSrvTools
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.plugins import RootDNAccessControlPlugin

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

localhost = DirSrvTools.getLocalhost()
hostname = socket.gethostname()

@pytest.fixture(scope="function")
def rootdn_cleanup(topology_st):
    """Do a cleanup of the config area before the test """
    log.info('Cleaning up the config area')
    plugin = RootDNAccessControlPlugin(topology_st.standalone)
    plugin.remove_all_allow_host()
    plugin.remove_all_deny_host()
    plugin.remove_all_allow_ip()
    plugin.remove_all_deny_ip()


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
    global inst
    inst = topology_st.standalone

    #
    # Set an aci so we can modify the plugin after we deny the Root DN
    #
    ACI = ('(target ="ldap:///cn=config")(targetattr = "*")(version 3.0' +
           ';acl "all access";allow (all)(userdn="ldap:///anyone");)')
    assert inst.config.set('aci', ACI)

    #
    # Create a user to modify the config
    #
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    TEST_USER_PROPERTIES['userpassword'] = PASSWORD
    global user
    user = users.create(properties=TEST_USER_PROPERTIES)

    #
    # Enable dynamic plugins
    #
    assert inst.config.set('nsslapd-dynamic-plugins', 'on')

    #
    # Enable the plugin (after enabling dynamic plugins)
    #
    global plugin
    plugin = RootDNAccessControlPlugin(inst)
    plugin.enable()

    log.info('test_rootdn_init: Initialized root DN test suite.')


def test_rootdn_access_specific_time(topology_st, rootdn_setup, rootdn_cleanup):
    """Test binding inside and outside of a specific time

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e8
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Get the current time, and bump it ahead twohours
        2. Bind as Root DN
        3. Set config to allow the entire day
        4. Bind as Root DN
        5. Cleanup
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

    assert plugin.replace_many(('rootdn-open-time', open_time),
                               ('rootdn-close-time', close_time))

    #
    # Bind as Root DN - should fail
    #
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        inst.simple_bind_s(DN_DM, PASSWORD)

    #
    # Set config to allow the entire day
    #
    assert inst.simple_bind_s(user.dn, PASSWORD)

    assert plugin.replace_many(('rootdn-open-time', '0000'),
                               ('rootdn-close-time', '2359'))

    assert inst.simple_bind_s(DN_DM, PASSWORD)

    #
    # Cleanup - undo the changes we made so the next test has a clean slate
    #

    assert plugin.apply_mods([(ldap.MOD_DELETE, 'rootdn-open-time'),
                              (ldap.MOD_DELETE, 'rootdn-close-time')])


def test_rootdn_access_day_of_week(topology_st, rootdn_setup, rootdn_cleanup):
    """Test the days of week feature

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e1
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Set the deny days
        2. Bind as Root DN
        3. Set the allow days
        4. Bind as Root DN
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
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
    plugin.set_days_allowed(deny_days)

    #
    # Bind as Root DN - should fail
    #
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        inst.simple_bind_s(DN_DM, PASSWORD)

    #
    # Set the allow days
    #
    assert inst.simple_bind_s(user.dn, PASSWORD)
    plugin.set_days_allowed(allow_days)

    assert inst.simple_bind_s(DN_DM, PASSWORD)


def test_rootdn_access_denied_ip(topology_st, rootdn_setup, rootdn_cleanup):
    """Test denied IP feature - we can just test denying 127.0.0.1

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e2
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Set rootdn-deny-ip to '127.0.0.1' and '::1'
        2. Bind as Root DN
        3. Change the denied IP so root DN succeeds
        4. Bind as Root DN
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
    """

    log.info('Running test_rootdn_access_denied_ip...')
    plugin.add_deny_ip('127.0.0.1')
    plugin.add_deny_ip('::1')

    #
    # Bind as Root DN - should fail
    #
    conn = ldap.initialize('ldap://{}:{}'.format('127.0.0.1', inst.port))
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        conn.simple_bind_s(DN_DM, PASSWORD)

    #
    # Change the denied IP so root DN succeeds
    #
    assert inst.simple_bind_s(user.dn, PASSWORD)

    plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-deny-ip', '255.255.255.255')])

    conn = ldap.initialize('ldap://{}:{}'.format('127.0.0.1', inst.port))
    assert conn.simple_bind_s(DN_DM, PASSWORD)


def test_rootdn_access_denied_host(topology_st, rootdn_setup, rootdn_cleanup):
    """Test denied Host feature - we can just test denying localhost

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e3
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Set rootdn-deny-host to hostname (localhost if not accessable)
        2. Bind as Root DN
        3. Change the denied host so root DN succeeds
        4. Bind as Root DN
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
    """

    log.info('Running test_rootdn_access_denied_host...')
    hostname = socket.gethostname()
    plugin.add_deny_host(hostname)
    if localhost != hostname:
        plugin.add_deny_host(localhost)

    #
    # Bind as Root DN - should fail
    #
    conn = ldap.initialize('ldap://{}:{}'.format(localhost, inst.port))
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        conn.simple_bind_s(DN_DM, PASSWORD)

    #
    # Change the denied host so root DN succeeds
    #
    assert inst.simple_bind_s(user.dn, PASSWORD)
    plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-deny-host', 'i.dont.exist.{}'.format(uuid.uuid4()))])

    conn = ldap.initialize('ldap://{}:{}'.format(hostname, inst.port))
    assert conn.simple_bind_s(DN_DM, PASSWORD)


def test_rootdn_access_allowed_ip(topology_st, rootdn_setup, rootdn_cleanup):
    """Test allowed ip feature

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e4
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Set allowed ip to 255.255.255.255 - blocks the Root DN
        2. Bind as Root DN
        3. Allow localhost
        4. Bind as Root DN
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
    """

    log.info('Running test_rootdn_access_allowed_ip...')

    #
    # Set allowed ip to 255.255.255.255 - blocks the Root DN
    #
    plugin.add_allow_ip('255.255.255.255')

    #
    # Bind as Root DN - should fail
    #
    conn = ldap.initialize('ldap://{}:{}'.format(localhost, inst.port))
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        conn.simple_bind_s(DN_DM, PASSWORD)

    #
    # Allow localhost
    #
    assert inst.simple_bind_s(user.dn, PASSWORD)
    plugin.add_allow_ip('127.0.0.1')
    plugin.add_allow_ip('::1')

    conn = ldap.initialize('ldap://{}:{}'.format(localhost, inst.port))
    assert conn.simple_bind_s(DN_DM, PASSWORD)


def test_rootdn_access_allowed_host(topology_st, rootdn_setup, rootdn_cleanup):
    """Test allowed host feature

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e5
    :setup: Standalone instance, rootdn plugin set up
    :steps:
        1. Set allowed host to an unknown host - blocks the Root DN
        2. Bind as Root DN
        3. Allow localhost
        4. Bind as Root DN
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
        4. Success
    """

    log.info('Running test_rootdn_access_allowed_host...')

    #
    # Set allowed host to an unknown host - blocks the Root DN
    #
    plugin.add_allow_host('i.dont.exist.{}'.format(uuid.uuid4()))

    #
    # Bind as Root DN - should fail
    #
    conn = ldap.initialize('ldap://{}:{}'.format(localhost, inst.port))
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        conn.simple_bind_s(DN_DM, PASSWORD)

    #
    # Allow localhost
    #
    assert inst.simple_bind_s(user.dn, PASSWORD)
    plugin.remove_all_allow_host()
    plugin.add_allow_host(localhost)
    if hostname != localhost:
        plugin.add_allow_host(hostname)

    conn = ldap.initialize('ldap://{}:{}'.format(localhost, inst.port))
    assert conn.simple_bind_s(DN_DM, PASSWORD)

def test_rootdn_config_validate(topology_st, rootdn_setup, rootdn_cleanup):
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
        17. Replace 'rootdn-allow-ip' with '123.234.345.456'
        18. Replace 'rootdn-allow-ip' with ':::'
        19. Replace 'rootdn-deny-ip' with '12.12.Z.12'
        20. Replace 'rootdn-deny-ip' with '123.234.345.456'
        21. Replace 'rootdn-deny-ip' with ':::'
        22. Replace 'rootdn-allow-host' with 'host._.com'
        23. Replace 'rootdn-deny-host' with 'host.####.com'
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
        20. Should fail
        21. Should fail
        22. Should fail
        23. Should fail
    """

    #
    # Test rootdn-open-time
    #
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        log.info('Add just "rootdn-open-time"')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-open-time', '0000')])

        log.info('Add multiple "rootdn-open-time"')
        plugin.apply_mods([(ldap.MOD_ADD, 'rootdn-open-time', '0000'),
                           (ldap.MOD_ADD, 'rootdn-open-time', '0001')])

        log.info('Add invalid "rootdn-open-time" -1 ')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-open-time', '-1'),
                           (ldap.MOD_REPLACE, 'rootdn-close-time', '0000')])

        log.info('Add invalid "rootdn-open-time" 2400')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-open-time', '2400'),
                           (ldap.MOD_REPLACE, 'rootdn-close-time', '0000')])

        log.info('Add invalid "rootdn-open-time" aaaaa')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-open-time','aaaaa'),
                           (ldap.MOD_REPLACE, 'rootdn-close-time', '0000')])


    #
    # Test rootdn-close-time
    #
        log.info('Add just "rootdn-close-time"')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-close-time', '0000')])

        log.info('Add multiple "rootdn-close-time"')
        plugin.apply_mods([(ldap.MOD_ADD, 'rootdn-close-time', '0000'),
                           (ldap.MOD_ADD, 'rootdn-close-time', '0001')])

        log.info('Add invalid "rootdn-close-time" -1 ')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-open-time', '0000'),
                           (ldap.MOD_REPLACE, 'rootdn-close-time', '-1')])

        log.info('Add invalid "rootdn-close-time" 2400')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-open-time', '0000'),
                           (ldap.MOD_REPLACE, 'rootdn-close-time', '2400')])

        log.info('Add invalid "rootdn-open-time" aaaaa')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-open-time','0000'),
                           (ldap.MOD_REPLACE, 'rootdn-close-time','aaaaa')])


    #
    # Test days allowed
    #
        log.info('Add multiple "rootdn-days-allowed"')
        plugin.apply_mods([(ldap.MOD_ADD, 'rootdn-days-allowed', 'Mon'),
                           (ldap.MOD_ADD, 'rootdn-days-allowed', 'Tue')])

        log.info('Add invalid "rootdn-days-allowed"')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-days-allowed', 'Mon1')])
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-days-allowed', 'Tue, Mon1')])
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-days-allowed', 'm111m')])
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-days-allowed', 'Gur')])

    #
    # Test allow ips
    #
        log.info('Add invalid "rootdn-allow-ip"')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-allow-ip', '12.12.Z.12')])
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-allow-ip', '123.234.345.456')])
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-allow-ip', ':::')])

    #
    # Test deny ips
    #
        log.info('Add invalid "rootdn-deny-ip"')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-deny-ip', '12.12.Z.12')])
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-deny-ip', '123.234.345.456')])
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-deny-ip', ':::')])

    #
    # Test allow hosts
    #
        log.info('Add invalid "rootdn-allow-host"')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-allow-host', 'host._.com')])

    #
    # Test deny hosts
    #
        log.info('Add invalid "rootdn-deny-host"')
        plugin.apply_mods([(ldap.MOD_REPLACE, 'rootdn-deny-host', 'host.####.com')])


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
