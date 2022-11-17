# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import pytest
import ldap
from lib389._constants import DEFAULT_SUFFIX, PASSWORD
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.group import Groups
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=None)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

ACCTS_DN = "ou=accounts,dc=example,dc=com"
USERS_DN = "ou=users,ou=accounts,dc=example,dc=com"
GROUPS_DN = "ou=groups,ou=accounts,dc=example,dc=com"
ADMIN_GROUP_DN = "cn=admins,ou=groups,ou=accounts,dc=example,dc=com"
ADMIN_DN = "uid=admin,ou=users,ou=accounts,dc=example,dc=com"

ACCTS_ACI = ('(targetattr="userPassword")(version 3.0; acl "allow password ' +
             'search"; allow(search) userdn = "ldap:///all";)')
USERS_ACI = ('(targetattr = "cn || createtimestamp || description || displayname || entryusn || gecos ' +
             '|| gidnumber || givenname || homedirectory || initials || ' +
             'loginshell || manager || modifytimestamp || objectclass || sn || title || uid || uidnumber")' +
             '(targetfilter = "(objectclass=posixaccount)")' +
             '(version 3.0;acl "Read Attributes";allow (compare,read,search) userdn = "ldap:///anyone";)')
GROUPS_ACIS = [
    (
        '(targetattr = "businesscategory || cn || createtimestamp || description |' +
        '| entryusn || gidnumber || mepmanagedby || modifytimestamp || o || objectclass || ou || own' +
        'er || seealso")(targetfilter = "(objectclass=posixgroup)")(version 3.0;acl' +
        '"permission:System: Read Groups";allow (compare,re' +
        'ad,search) userdn = "ldap:///anyone";)'
    ),
    (
        '(targetattr = "member || memberof || memberuid")(targetfilter = '+
        '"(objectclass=posixgroup)")(version 3.0;acl' +
        '"permission:System: Read Group Membership";allow (compare,read' +
        ',search) userdn = "ldap:///all";)'
    )
]


def test_deref_and_access_control(topo):
    """Test that the deref plugin honors access control rules correctly

    The setup mimics a generic IPA DIT with its ACI's.  The userpassword
    attribute should not be returned

    :id: bedb6af2-b765-479d-808c-df0348e0ec95
    :setup: Standalone Instance
    :steps:
        1. Create container entries with aci's
        2. Perform deref search and make sure userpassword is not returned
    :expectedresults:
        1. Success
        2. Success
    """

    topo.standalone.config.set('nsslapd-schemacheck', 'off')
    if DEBUGGING:
        topo.standalone.config.enable_log('audit')
        topo.standalone.config.set('nsslapd-errorlog-level', '128')

    # Accounts
    ou1 = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ou1.create(properties={
        'ou': 'accounts',
        'aci': ACCTS_ACI
    })

    # Users
    ou2 = OrganizationalUnits(topo.standalone, ACCTS_DN)
    ou2.create(properties={
        'ou': 'users',
        'aci': USERS_ACI
    })

    # Groups
    ou3 = OrganizationalUnits(topo.standalone, ACCTS_DN)
    ou3.create(properties={
        'ou': 'groups',
        'aci': GROUPS_ACIS
    })

    # Create User
    users = UserAccounts(topo.standalone, USERS_DN, rdn=None)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update(
        {
            'uid': 'user',
            'objectclass': ['posixAccount', 'extensibleObject'],
            'userpassword': PASSWORD
        }
    )
    user = users.create(properties=user_props)

    # Create Admin user
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update(
        {
            'uid': 'admin',
            'objectclass': ['posixAccount', 'extensibleObject', 'inetuser'],
            'userpassword': PASSWORD,
            'memberOf': ADMIN_GROUP_DN
        }
    )
    users.create(properties=user_props)

    # Create Admin group
    groups = Groups(topo.standalone, GROUPS_DN, rdn=None)
    group_props = {
        'cn': 'admins',
        'gidNumber': '123',
        'objectclass': ['posixGroup', 'extensibleObject'],
        'member': ADMIN_DN
    }
    groups.create(properties=group_props)

    # Bind as user, then perform deref search on admin user
    user.rebind(PASSWORD)
    result, control_response = topo.standalone.dereference(
        'member:cn,userpassword',
        base=ADMIN_GROUP_DN,
        scope=ldap.SCOPE_BASE)

    log.info('Check, that the dereference search result does not have userpassword')
    assert result[0][2][0].entry[0]['attrVals'][0]['type'] != 'userpassword'


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
