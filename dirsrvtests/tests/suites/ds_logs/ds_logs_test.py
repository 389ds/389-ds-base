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

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

PLUGIN_TIMESTAMP = 'nsslapd-logging-hr-timestamps-enabled'
USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX


def add_users(topology_st, users_num):
    """Add users to the default suffix"""

    users_list = []
    log.info('Adding %d users' % users_num)
    for num in sample(range(1000), users_num):
        num_ran = int(round(num))
        USER_NAME = 'test%05d' % num_ran
        USER_DN = 'uid=%s,%s' % (USER_NAME, DEFAULT_SUFFIX)
        users_list.append(USER_DN)
        try:
            topology_st.standalone.add_s(Entry((USER_DN, {
                'objectclass': 'top person'.split(),
                'objectclass': 'organizationalPerson',
                'objectclass': 'inetorgperson',
                'cn': USER_NAME,
                'sn': USER_NAME,
                'userpassword': 'pass%s' % num_ran,
                'mail': '%s@redhat.com' % USER_NAME,
                'uid': USER_NAME
            })))
        except ldap.LDAPError as e:
            log.error('Failed to add user (%s): error (%s)' % (USER_DN,
                                                               e.message['desc']))
            raise e


def search_users(topology_st):
    try:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(cn=*)', ['cn'])
        for entry in entries:
            if 'user1' in entry.data['cn']:
                log.info('Search found "user1"')

    except ldap.LDAPError as e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        raise e


def test_check_default(topology_st):
    """Bug 1273549 - Check the default value of nsslapd-logging-hr-timestamps-enabled,
    it should be ON
    """

    log.info('Check the default value of nsslapd-logging-hr-timestamps-enabled, it should be ON')

    # Get the default value of nsslapd-logging-hr-timestamps-enabled attribute
    default = topology_st.standalone.config.get_attr_val(PLUGIN_TIMESTAMP)

    # Now check it should be ON by default
    assert (default == "on")
    log.debug(default)


def test_plugin_set_invalid(topology_st):
    """Bug 1273549 - Try to set some invalid values for the newly added attribute"""

    log.info('test_plugin_set_invalid - Expect to fail with junk value')
    with pytest.raises(ldap.OPERATIONS_ERROR):
        result = topology_st.standalone.config.set(PLUGIN_TIMESTAMP, 'JUNK')


def test_log_plugin_on(topology_st):
    """Bug 1273549 - Check access logs for milisecond, when attribute is ON"""

    log.info('Bug 1273549 - Check access logs for milisecond, when attribute is ON')
    log.info('perform any ldap operation, which will trigger the logs')
    add_users(topology_st, 100)
    search_users(topology_st)

    log.info('Restart the server to flush the logs')
    topology_st.standalone.restart(timeout=10)

    log.info('parse the access logs')
    access_log_lines = topology_st.standalone.ds_access_log.readlines()
    assert len(access_log_lines) > 0
    assert topology_st.standalone.ds_access_log.match('^\[.+\d{9}.+\].+')


def test_log_plugin_off(topology_st):
    """Bug 1273549 - Check access logs for missing milisecond, when attribute is OFF"""

    log.info('Bug 1273549 - Check access logs for missing milisecond, when attribute is OFF')

    log.info('test_log_plugin_off - set the configuraton attribute to OFF')
    topology_st.standalone.config.set(PLUGIN_TIMESTAMP, 'OFF')

    log.info('Restart the server to flush the logs')
    topology_st.standalone.restart(timeout=10)

    log.info('test_log_plugin_off - delete the privious access logs')
    topology_st.standalone.deleteAccessLogs()

    # Now generate some fresh logs
    search_users(topology_st)

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
