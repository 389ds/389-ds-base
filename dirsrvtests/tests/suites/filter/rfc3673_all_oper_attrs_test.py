# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DN_DM, DEFAULT_SUFFIX, DN_CONFIG, PASSWORD

DN_PEOPLE = 'ou=people,%s' % DEFAULT_SUFFIX
DN_ROOT = ''
TEST_USER_NAME = 'all_attrs_test'
TEST_USER_DN = 'uid=%s,%s' % (TEST_USER_NAME, DN_PEOPLE)
TEST_USER_PWD = 'all_attrs_test'

# Suffix for search, Regular user boolean, List of expected attrs
TEST_PARAMS = [(DN_ROOT, False, [
    'aci', 'createTimestamp', 'creatorsName',
    'modifiersName', 'modifyTimestamp', 'namingContexts',
    'nsBackendSuffix', 'nsUniqueId', 'subschemaSubentry',
    'supportedControl', 'supportedExtension',
    'supportedFeatures', 'supportedLDAPVersion',
    'supportedSASLMechanisms', 'vendorName', 'vendorVersion'
]),
               (DN_ROOT, True, [
                   'createTimestamp', 'creatorsName',
                   'modifiersName', 'modifyTimestamp', 'namingContexts',
                   'nsBackendSuffix', 'nsUniqueId', 'subschemaSubentry',
                   'supportedControl', 'supportedExtension',
                   'supportedFeatures', 'supportedLDAPVersion',
                   'supportedSASLMechanisms', 'vendorName', 'vendorVersion'
               ]),
               (DN_PEOPLE, False, [
                   'aci', 'createTimestamp', 'creatorsName', 'entrydn',
                   'entryid', 'modifiersName', 'modifyTimestamp',
                   'nsUniqueId', 'numSubordinates', 'parentid'
               ]),
               (DN_PEOPLE, True, [
                   'createTimestamp', 'creatorsName', 'entrydn',
                   'entryid', 'modifyTimestamp', 'nsUniqueId',
                   'numSubordinates', 'parentid'
               ]),
               (TEST_USER_DN, False, [
                   'createTimestamp', 'creatorsName', 'entrydn',
                   'entryid', 'modifiersName', 'modifyTimestamp',
                   'nsUniqueId', 'parentid'
               ]),
               (TEST_USER_DN, True, [
                   'createTimestamp', 'creatorsName', 'entrydn',
                   'entryid', 'modifyTimestamp', 'nsUniqueId', 'parentid'
               ]),
               (DN_CONFIG, False, ['numSubordinates', 'passwordHistory'])]


@pytest.fixture(scope="module")
def test_user(topology_st):
    """User for binding operation"""

    try:
        topology_st.standalone.add_s(Entry((TEST_USER_DN, {
            'objectclass': 'top person'.split(),
            'objectclass': 'organizationalPerson',
            'objectclass': 'inetorgperson',
            'cn': TEST_USER_NAME,
            'sn': TEST_USER_NAME,
            'userpassword': TEST_USER_PWD,
            'mail': '%s@redhat.com' % TEST_USER_NAME,
            'uid': TEST_USER_NAME
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add user (%s): error (%s)' % (TEST_USER_DN,
                                                           e.message['desc']))
        raise e


@pytest.fixture(scope="module")
def user_aci(topology_st):
    """Deny modifiersName attribute for the test user
    under whole suffix
    """

    ACI_TARGET = '(targetattr= "modifiersName")'
    ACI_ALLOW = '(version 3.0; acl "Deny modifiersName for user"; deny (read)'
    ACI_SUBJECT = ' userdn = "ldap:///%s";)' % TEST_USER_DN
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD,
                                                      'aci',
                                                      ACI_BODY)])


def test_supported_features(topology_st):
    """Verify that OID 1.3.6.1.4.1.4203.1.5.1 is published
       in the supportedFeatures [RFC3674] attribute in the rootDSE.

    :id: 441b3f1f-a24b-4943-aa65-7edce460abbf
    :setup: Standalone instance
    :steps:
         1. Search for 'supportedFeatures' at rootDSE
    :expectedresults:
         1. Value 1.3.6.1.4.1.4203.1.5.1 is presented
    """

    entries = topology_st.standalone.search_s('', ldap.SCOPE_BASE,
                                              '(objectClass=*)',
                                              ['supportedFeatures'])
    supported_value = entries[0].data['supportedfeatures']

    assert supported_value == ['1.3.6.1.4.1.4203.1.5.1']


@pytest.mark.parametrize('add_attr', ['', '*', 'objectClass'])
@pytest.mark.parametrize('search_suffix,regular_user,oper_attr_list',
                         TEST_PARAMS)
def test_search_basic(topology_st, test_user, user_aci, add_attr,
                      search_suffix, regular_user, oper_attr_list):
    """Verify that you can get all expected operational attributes
       by a Search Request [RFC2251] with '+' (ASCII 43) filter.
       Please see: https://tools.ietf.org/html/rfc3673

    :id: 14c66bc2-28e1-4f5f-893e-508e0f720f8c
    :setup: Standalone instance, test user for binding,
            deny one attribute aci for that user
    :steps:
         1. Bind as regular user or Directory Manager
         2. Search with '+' filter and with additionally
            'objectClass' and '*' attributes too
         3. Check attributes listed contain both operational
            and non-operational attributes for '*' attributes
            search
    :expectedresults:
         1. Bind should be successful
         2. All expected values of attributes should be returned
            as per the parametrization done
         3. It should pass
    """

    if regular_user:
        log.info("bound as: %s", TEST_USER_DN)
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)
    else:
        log.info("bound as: %s", DN_DM)
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    search_filter = ['+']
    if add_attr:
        search_filter.append(add_attr)
        expected_attrs = sorted(oper_attr_list + ['objectClass'])
    else:
        expected_attrs = sorted(oper_attr_list)

    log.info("suffix: %s filter: %s" % (search_suffix, search_filter))
    entries = topology_st.standalone.search_s(search_suffix, ldap.SCOPE_BASE,
                                              '(objectclass=*)',
                                              search_filter)
    log.info("results: %s" % entries)
    assert len(entries) > 0
    found_attrs = sorted(entries[0].data.keys())

    if add_attr == '*':
        # Check that found attrs contain both operational
        # and non-operational attributes
        assert all(attr in found_attrs
                   for attr in ['objectClass', expected_attrs[0]])
    else:
        assert set(expected_attrs).issubset(set(found_attrs))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
