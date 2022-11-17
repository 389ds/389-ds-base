# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket48370(topology_st):
    """
    Deleting attirbute values and readding a value does not properly update
    the pres index.  The values are not actually deleted from the index
    """

    DN = 'uid=user0099,' + DEFAULT_SUFFIX

    #
    # Add an entry
    #
    topology_st.standalone.add_s(Entry((DN, {
        'objectclass': ['top', 'person',
                        'organizationalPerson',
                        'inetorgperson',
                        'posixAccount'],
        'givenname': 'test',
        'sn': 'user',
        'loginshell': '/bin/bash',
        'uidNumber': '10099',
        'gidNumber': '10099',
        'gecos': 'Test User',
        'mail': ['user0099@dev.null',
                 'alias@dev.null',
                 'user0099@redhat.com'],
        'cn': 'Test User',
        'homeDirectory': '/home/user0099',
        'uid': 'admin2',
        'userpassword': 'password'})))

    #
    # Perform modify (delete & add mail attributes)
    #
    try:
        topology_st.standalone.modify_s(DN, [(ldap.MOD_DELETE,
                                              'mail',
                                              b'user0099@dev.null'),
                                             (ldap.MOD_DELETE,
                                              'mail',
                                              b'alias@dev.null'),
                                             (ldap.MOD_ADD,
                                              'mail', b'user0099@dev.null')])
    except ldap.LDAPError as e:
        log.fatal('Failedto modify user: ' + str(e))
        assert False

    #
    # Search using deleted attribute value- no entries should be returned
    #
    try:
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                ldap.SCOPE_SUBTREE,
                                                'mail=alias@dev.null')
        if entry:
            log.fatal('Entry incorrectly returned')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Failed to search for user: ' + str(e))
        assert False

    #
    # Search using existing attribute value - the entry should be returned
    #
    try:
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                ldap.SCOPE_SUBTREE,
                                                'mail=user0099@dev.null')
        if entry is None:
            log.fatal('Entry not found, but it should have been')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Failed to search for user: ' + str(e))
        assert False

    #
    # Delete the last values
    #
    try:
        topology_st.standalone.modify_s(DN, [(ldap.MOD_DELETE,
                                              'mail',
                                              b'user0099@dev.null'),
                                             (ldap.MOD_DELETE,
                                              'mail',
                                              b'user0099@redhat.com')
                                             ])
    except ldap.LDAPError as e:
        log.fatal('Failed to modify user: ' + str(e))
        assert False

    #
    # Search using deleted attribute value - no entries should be returned
    #
    try:
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                ldap.SCOPE_SUBTREE,
                                                'mail=user0099@redhat.com')
        if entry:
            log.fatal('Entry incorrectly returned')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Failed to search for user: ' + str(e))
        assert False

    #
    # Make sure presence index is correctly updated - no entries should be
    # returned
    #
    try:
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                ldap.SCOPE_SUBTREE,
                                                'mail=*')
        if entry:
            log.fatal('Entry incorrectly returned')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Failed to search for user: ' + str(e))
        assert False

    #
    # Now add the attributes back, and lets run a different set of tests with
    # a different number of attributes
    #
    try:
        topology_st.standalone.modify_s(DN, [(ldap.MOD_ADD,
                                              'mail',
                                              [b'user0099@dev.null',
                                               b'alias@dev.null'])])
    except ldap.LDAPError as e:
        log.fatal('Failedto modify user: ' + str(e))
        assert False

    #
    # Remove and readd some attibutes
    #
    try:
        topology_st.standalone.modify_s(DN, [(ldap.MOD_DELETE,
                                              'mail',
                                              b'alias@dev.null'),
                                             (ldap.MOD_DELETE,
                                              'mail',
                                              b'user0099@dev.null'),
                                             (ldap.MOD_ADD,
                                              'mail', b'user0099@dev.null')])
    except ldap.LDAPError as e:
        log.fatal('Failedto modify user: ' + str(e))
        assert False

    #
    # Search using deleted attribute value - no entries should be returned
    #
    try:
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                ldap.SCOPE_SUBTREE,
                                                'mail=alias@dev.null')
        if entry:
            log.fatal('Entry incorrectly returned')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Failed to search for user: ' + str(e))
        assert False

    #
    # Search using existing attribute value - the entry should be returned
    #
    try:
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                ldap.SCOPE_SUBTREE,
                                                'mail=user0099@dev.null')
        if entry is None:
            log.fatal('Entry not found, but it should have been')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Failed to search for user: ' + str(e))
        assert False

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
