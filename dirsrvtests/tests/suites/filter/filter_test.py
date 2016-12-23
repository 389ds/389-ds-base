# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_filter_escaped(topology_st):
    '''
    Test we can search for an '*' in a attribute value.
    '''

    log.info('Running test_filter_escaped...')

    USER1_DN = 'uid=test_entry,' + DEFAULT_SUFFIX
    USER2_DN = 'uid=test_entry2,' + DEFAULT_SUFFIX

    try:
        topology_st.standalone.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '1',
                                                       'cn': 'test * me',
                                                       'uid': 'test_entry',
                                                       'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_filter_escaped: Failed to add test user ' + USER1_DN + ': error ' +
                  e.message['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '2',
                                                       'cn': 'test me',
                                                       'uid': 'test_entry2',
                                                       'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_filter_escaped: Failed to add test user ' + USER2_DN + ': error ' + e.message['desc'])
        assert False

    try:
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'cn=*\**')
        if not entry or len(entry) > 1:
            log.fatal('test_filter_escaped: Entry was not found using "cn=*\**"')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_filter_escaped: Failed to search for user(%s), error: %s' %
                  (USER1_DN, e.message('desc')))
        assert False

    log.info('test_filter_escaped: PASSED')


def test_filter_search_original_attrs(topology_st):
    '''
    Search and request attributes with extra characters.  The returned entry
    should not have these extra characters:  "objectclass EXTRA"
    '''

    log.info('Running test_filter_search_original_attrs...')

    try:
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE,
                                                'objectclass=top', ['objectclass-EXTRA'])
        if entry[0].hasAttr('objectclass-EXTRA'):
            log.fatal('test_filter_search_original_attrs: Entry does not have the original attribute')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_filter_search_original_attrs: Failed to search suffix(%s), error: %s' %
                  (DEFAULT_SUFFIX, e.message('desc')))
        assert False

    log.info('test_filter_search_original_attrs: PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
