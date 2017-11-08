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
from lib389.topologies import topology_st

from lib389.idm.user import UserAccounts

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

PLUGIN_TIMESTAMP = 'nsslapd-logging-hr-timestamps-enabled'
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
    assert topology_st.standalone.ds_access_log.match('^\[.+\d{9}.+\].+')

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
    assert not topology_st.standalone.ds_access_log.match('^\[.+\d{9}.+\].+')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
