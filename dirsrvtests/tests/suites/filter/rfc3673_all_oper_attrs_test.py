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
from lib389.idm.user import UserAccounts
from lib389.idm.domain import Domain

from lib389._constants import DN_DM, DEFAULT_SUFFIX, DN_CONFIG, PASSWORD

pytestmark = pytest.mark.tier1

DN_PEOPLE = 'ou=people,%s' % DEFAULT_SUFFIX
DN_ROOT = ''
TEST_USER_NAME = 'all_attrs_test'
TEST_USER_DN = 'uid=%s,%s' % (TEST_USER_NAME, DN_PEOPLE)
TEST_USER_PWD = 'all_attrs_test'

# Suffix for search, Regular user boolean, List of expected attrs
TEST_PARAMS = [(DN_ROOT, False, [
                'aci', 'createTimestamp', 'creatorsName',
                'modifiersName', 'modifyTimestamp', 'namingContexts',
                'nsBackendSuffix', 'subschemaSubentry',
                'supportedControl', 'supportedExtension',
                'supportedFeatures', 'supportedLDAPVersion',
                'supportedSASLMechanisms', 'vendorName', 'vendorVersion'
               ]),
               (DN_ROOT, True, [
                'createTimestamp', 'creatorsName',
                'modifiersName', 'modifyTimestamp', 'namingContexts',
                'nsBackendSuffix', 'subschemaSubentry',
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
                'nsUniqueId', 'parentid', 'entryUUID'
               ]),
               (TEST_USER_DN, True, [
                'createTimestamp', 'creatorsName', 'entrydn',
                'entryid', 'modifyTimestamp', 'nsUniqueId', 'parentid', 'entryUUID'
               ]),
               (DN_CONFIG, False, [
                'numSubordinates', 'passwordHistory', 'modifyTimestamp', 'modifiersName'
               ])
            ]


@pytest.fixture(scope="module")
def create_user(topology_st):
    """User for binding operation"""

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    users.create(properties={
        'cn': TEST_USER_NAME,
        'sn': TEST_USER_NAME,
        'userpassword': TEST_USER_PWD,
        'mail': '%s@redhat.com' % TEST_USER_NAME,
        'uid': TEST_USER_NAME,
        'uidNumber': '1000',
        'gidNumber': '1000',
        'homeDirectory': '/home/test'
    })

    # Add anonymous access aci
    ACI_TARGET = "(targetattr != \"userpassword || aci\")(target = \"ldap:///%s\")" % (DEFAULT_SUFFIX)
    ACI_ALLOW = "(version 3.0; acl \"Anonymous Read access\"; allow (read,search,compare)"
    ACI_SUBJECT = "(userdn=\"ldap:///anyone\");)"
    ANON_ACI = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    suffix = Domain(topology_st.standalone, DEFAULT_SUFFIX)
    try:
        suffix.add('aci', ANON_ACI)
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass


@pytest.fixture(scope="module")
def user_aci(topology_st):
    """Don't allow modifiersName attribute for the test user
    under whole suffix
    """

    ACI_TARGET = '(targetattr= "modifiersName")'
    ACI_RULE = ('(version 3.0; acl "Deny modifiersName for user"; deny (read)'
                ' userdn = "ldap:///%s";)' % TEST_USER_DN)
    ACI_BODY = ensure_bytes(ACI_TARGET + ACI_RULE)
    topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', ACI_BODY)])


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

    assert supported_value == [b'1.3.6.1.4.1.4203.1.5.1']


@pytest.mark.parametrize('add_attr', ['', '*', 'objectClass'])
@pytest.mark.parametrize('search_suffix,regular_user,oper_attr_list',
                         TEST_PARAMS)
def test_search_basic(topology_st, create_user, user_aci, add_attr,
                      search_suffix, regular_user, oper_attr_list):
    """Verify that you can get all expected operational attributes
       by a Search Request [RFC2251] with '+' (ASCII 43) filter.
       Please see: https://tools.ietf.org/html/rfc3673

    :id: 14c66bc2-28e1-4f5f-893e-508e0f720f8c
    :parametrized: yes
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
        topology_st.standalone.simple_bind_s(TEST_USER_DN, ensure_bytes(TEST_USER_PWD))
    else:
        log.info("bound as: %s", DN_DM)
        topology_st.standalone.simple_bind_s(DN_DM, ensure_bytes(PASSWORD))

    search_filter = ['+']
    expected_attrs = oper_attr_list
    if add_attr:
        search_filter.append(add_attr)
        expected_attrs += ['objectClass']

    entries = topology_st.standalone.search_s(search_suffix, ldap.SCOPE_BASE,
                                              '(objectclass=*)',
                                              search_filter)
    found_attrs = set(entries[0].data.keys())
    if search_suffix == DN_ROOT and "nsUniqueId" in found_attrs:
        found_attrs.remove("nsUniqueId")

    if add_attr == '*':
        assert set(expected_attrs) - set(found_attrs) == set()
    else:
        assert set(expected_attrs) == set(found_attrs)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
