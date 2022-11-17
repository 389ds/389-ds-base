# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import logging
from ldap.controls import GetEffectiveRightsControl
from lib389.idm.domain import Domain
from lib389.idm.group import Groups
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.topologies import topology_st as topo
from lib389._constants import *
from lib389.utils import *

pytestmark = pytest.mark.tier1

TEST_ENTRY_NAME = 'testuser'
TEST_GROUP_NAME = 'group1'
TEST_GROUP2_NAME = 'group1'

DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

@pytest.fixture(scope="module")
def create_user(topo):
    """
    Create a user.
    Create a request_ctrl.
    """
    log.info('Adding user {}'.format(TEST_ENTRY_NAME))
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    test_user = users.create(properties=TEST_USER_PROPERTIES)

    request_ctrl = GetEffectiveRightsControl(criticality=True,
                                             authzId=ensure_bytes("dn:{}".format(test_user.dn)))
    return (test_user,request_ctrl)

def test_group_aci_entry_exists(topo,create_user):
    """This test case adds the groupdn aci and check ger contains 'vadn'

    :id: 1d73f715-e4b3-4ed6-a93b-9d529898ca78
    :setup: Standalone instance
    :steps:
        1. Create a group.
        2. Add the user as member into the group.
        3. Apply the ACI which will give the group full rights.
        4. Check entryLevelRights value for entries.
        5. Check 'vadn' is in the entryLevelRights.
    :expectedresults:
        1. It should pass
        2. It should pass
        3. It should pass
        4. It should pass
        5. It should pass
    """
    (test_user,request_ctrl) = create_user
    log.info('Adding group {}'.format(TEST_GROUP_NAME))
    groups = Groups(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    group_properties = {
        'cn': TEST_GROUP_NAME,
        'description': 'testgroup'}
    test_group = groups.create(properties=group_properties)
    test_group.add_member(test_user.dn)
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    ACI_TARGET = '(targetattr="*")'
    ACI_TARGET_FILTER = '(targetfilter ="(objectClass=person)")'
    ACI_ALLOW = '(version 3.0; acl "give group1 full rights"; allow (all) '
    ACI_SUBJECT = 'groupdn = "ldap:///{}";)'.format(test_group.dn)
    ACI_BODY = ACI_TARGET + ACI_TARGET_FILTER + ACI_ALLOW + ACI_SUBJECT
    log.info("Add an ACI granting add access to a user matching the groupdn")
    suffix.add('aci', ACI_BODY)
    entries = topo.standalone.search_ext('{}'.format(test_user.dn),
                                         ldap.SCOPE_SUBTREE,
                                         "objectclass=person",
                                         serverctrls=[request_ctrl])

    rtype, rdata, rmsgid, response_ctrl = topo.standalone.result3(entries)
    for dn, attrs in rdata:
        topo.standalone.log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]
        topo.standalone.log.info("########  entryLevelRights: %r" % value)
        assert b'vadn' in value

def test_group_aci_template_entry(topo,create_user):
    """This test case adds the groupdn aci and check ger contains 'vadn'

    :id: 714c8649-36b6-4e28-a4c5-4b16ede4355f
    :setup: Standalone instance
    :steps:
        1. Apply the ACI which will give the user full rights.
        2. Check entryLevelRights value for a non-existing template entry.
        3. Check 'vadn' is in the entryLevelRights of the  non-existing template entry.
    :expectedresults:
        1. It should pass
        2. It should pass
        3. It should pass
    """
    (test_user, request_ctrl) = create_user
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    ACI_TARGET = '(targetattr="*")'
    ACI_TARGET_FILTER = '(targetfilter ="(objectClass=person)")'
    ACI_ALLOW = '(version 3.0; acl "allow all to target"; allow (all) '
    ACI_SUBJECT = 'userdn = "ldap:///{}";)'.format(test_user.dn)
    ACI_BODY = ACI_TARGET + ACI_TARGET_FILTER + ACI_ALLOW + ACI_SUBJECT
    log.info("Add an ACI granting add access to a user matching the userdn")
    suffix.add('aci', ACI_BODY)
    entries = topo.standalone.search_ext(DEFAULT_SUFFIX,
                                         ldap.SCOPE_SUBTREE,
                                         "cn=sub_entry11", ["sn@person:cn", "member@groupofnames:cn"],
                                         serverctrls=[request_ctrl])

    rtype, rdata, rmsgid, response_ctrl = topo.standalone.result3(entries)
    for dn, attrs in rdata:
        if dn == 'cn=template_person_objectclass,dc=example,dc=com':
            topo.standalone.log.info("dn: %s" % dn)
            value = attrs['entryLevelRights'][0]
            topo.standalone.log.info("########  entryLevelRights: %r" % value)
            assert b'vadn' in value
        elif dn == 'cn=template_groupofnames_objectclass,dc=example,dc=com':
            topo.standalone.log.info("dn: %s" % dn)
            value = attrs['entryLevelRights'][0]
            topo.standalone.log.info("########  entryLevelRights: %r" % value)
            assert b'vadn' not in value
        else:
            assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main('-s {}'.format(CURRENT_FILE))
