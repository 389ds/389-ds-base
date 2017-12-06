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
from lib389._constants import PASSWORD, DEFAULT_SUFFIX

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_filter_escaped(topology_st):
    """Test we can search for an '*' in a attribute value.

    :id: 5c9aa40c-c641-4603-bce3-b19f4c1f2031
    :setup: Standalone instance
    :steps:
         1. Add a test user with an '*' in its attribute value
            i.e. 'cn=test * me'
         2. Add another similar test user without '*' in its attribute value
         3. Search test user using search filter "cn=*\**"
    :expectedresults:
         1. This should pass
         2. This should pass
         3. Test user with 'cn=test * me' only, should be listed
    """
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
    """Search and request attributes with extra characters. The returned entry
      should not have these extra characters: objectclass EXTRA"

    :id: d30d8a1c-84ac-47ba-95f9-41e3453fbf3a
    :setup: Standalone instance
    :steps:
         1. Execute a search operation for attributes with extra characters
         2. Check the search result have these extra characters or not
    :expectedresults:
         1. Search should pass
         2. Search result should not have these extra characters attribute
    """

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

@pytest.mark.bz1511462
def test_filter_scope_one(topology_st):
    """Test ldapsearch with scope one gives only single entry

    :id: cf5a6078-bbe6-4d43-ac71-553c45923f91
    :setup: Standalone instance
    :steps:
         1. Search cn=Directory Administrators,dc=example,dc=com using ldapsearch with
            scope one using base as dc=example,dc=com
         2. Check that search should return only one entry
    :expectedresults:
         1. This should pass
         2. This should pass
    """

    parent_dn="dn: dc=example,dc=com"
    child_dn="dn: cn=Directory Administrators,dc=example,dc=com"

    log.info('Search user using ldapsearch with scope one')
    results = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_ONELEVEL,'cn=Directory Administrators',['cn'] )
    log.info(results)

    log.info('Search should only have one entry')
    assert len(results) == 1

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
