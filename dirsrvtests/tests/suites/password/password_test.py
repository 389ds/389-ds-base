# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_password_delete_specific_password(topology_st):
    """ Delete a specific userpassword, and make sure
    it is actually deleted from the entry
    """

    log.info('Running test_password_delete_specific_password...')

    USER_DN = 'uid=test_entry,' + DEFAULT_SUFFIX

    #
    # Add a test user with a password
    #
    try:
        topology_st.standalone.add_s(Entry((USER_DN, {'objectclass': "top extensibleObject".split(),
                                                      'sn': '1',
                                                      'cn': 'user 1',
                                                      'uid': 'user1',
                                                      'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_password_delete_specific_password: Failed to add test user ' +
                  USER_DN + ': error ' + e.message['desc'])
        assert False

    #
    # Delete the exact password
    #
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_DELETE, 'userpassword', PASSWORD)])
    except ldap.LDAPError as e:
        log.fatal('test_password_delete_specific_password: Failed to delete userpassword: error ' +
                  e.message['desc'])
        assert False

    #
    # Check the password is actually deleted
    #
    try:
        entry = topology_st.standalone.search_s(USER_DN, ldap.SCOPE_BASE, 'objectclass=top')
        if entry[0].hasAttr('userpassword'):
            log.fatal('test_password_delete_specific_password: Entry incorrectly still have the userpassword attribute')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_password_delete_specific_password: Failed to search for user(%s), error: %s' %
                  (USER_DN, e.message('desc')))
        assert False

    #
    # Cleanup
    #
    try:
        topology_st.standalone.delete_s(USER_DN)
    except ldap.LDAPError as e:
        log.fatal('test_password_delete_specific_password: Failed to delete user(%s), error: %s' %
                  (USER_DN, e.message('desc')))
        assert False

    log.info('test_password_delete_specific_password: PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
