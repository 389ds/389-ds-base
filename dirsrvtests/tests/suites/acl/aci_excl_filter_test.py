# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
import logging
import os
import pytest
from lib389.topologies import topology_st as topo
from lib389._mapped_object import DSLdapObject
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.idm.user import UserAccounts
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.domain import Domain
from lib389.idm.account import Accounts

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

@pytest.fixture(scope="function")
def add_anon_aci_access(topo, request):
    # Add anonymous access aci
    ACI_TARGET = "(targetattr != \"userpassword\")(target = \"ldap:///%s\")" % (DEFAULT_SUFFIX)
    ACI_ALLOW = "(version 3.0; acl \"Anonymous Read access\"; allow (read,search,compare)"
    ACI_SUBJECT = "(userdn=\"ldap:///anyone\");)"
    ANON_ACI = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)

    try:
        suffix.add('aci', ANON_ACI)
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass
    def fin():
        suffix.delete()
    request.addfinalizer(fin)


def add_ou_entry(topo, name, myparent):

    ou_dn = 'ou={},{}'.format(name, myparent)
    ou = OrganizationalUnit(topo.standalone, dn=ou_dn)
    assert ou.create(properties={'ou': name})
    log.info('Organisation {} created for ou :{} .'.format(name, ou_dn))


def add_user_entry(topo, user, name, pw, myparent):

    dn = 'ou=%s,%s' % (name, myparent)
    properties = {
        'uid': name,
        'cn': 'admin',
        'sn': name,
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/{}'.format(name),
        'telephonenumber': '+1 222 333-4444',
        'userpassword': pw,
        }

    assert user.create(properties=properties)
    log.info('User created for dn :{} .'.format(dn))
    return user


def test_aci_with_exclude_filter(topo, add_anon_aci_access):
    """Test an ACI(Access control instruction) which contains an extensible filter.
    :id: 238da674-81d9-11eb-a965-98fa9ba19b65
    :setup: Standalone instance
    :steps:
        1. Bind to a new Standalone instance
        2. Generate text for the Access Control Instruction(ACI) and add to the standalone instance
           -Create a test user 'admin' with a marker -> deniedattr = 'telephonenumber'
        3. Create 2 top Organizational units (ou) under the same root suffix
        4. Create 2 test users for each Organizational unit (ou) above with the same username 'admin'
        5. Bind to the Standalone instance as the user 'admin' from the ou created in step 4 above
           - Search for user(s) ' admin in the subtree that satisfy this criteria:
               DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, cn_filter, [deniedattr, 'dn']
        6.  The search should return 2 entries with the username 'admin'
        7.  Verify that the users found do not have the --> deniedattr = 'telephonenumber' marker
    :expectedresults:
        1. Bind should be successful
        2. Operation to create 2 Orgs (ou) should be successful
        3. Operation to create 2 (admin*) users should be successful
        4. Operation should be successful.
        5. Operation should be successful 
        6. Should successfully return 2 users that match "admin*"
        7. PASS - users found do not have the --> deniedattr = 'telephonenumber' marker

    """

    log.info('Create an OU for them')
    ous = OrganizationalUnit(topo.standalone, DEFAULT_SUFFIX)
    log.info('Create an top org users')
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    log.info('Add aci which contains extensible filter.')
    ouname = 'outest'
    username = 'admin'
    passwd = 'Password'
    deniedattr = 'telephonenumber'
    log.info('Add aci which contains extensible filter.')

    aci_text = ('(targetattr = "{}")'.format(deniedattr) +
                '(target = "ldap:///{}")'.format(DEFAULT_SUFFIX) +
                '(version 3.0;acl "admin-tel-matching-rule-outest";deny (all)' +
                '(userdn = "ldap:///{}??sub?(&(cn={})(ou:dn:={}))");)'.format(DEFAULT_SUFFIX, username, ouname))

    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    suffix.add('aci', aci_text)
    log.info('Adding OU entries ...')
    for idx in range(0, 2):
        ou0 = 'OU%d' % idx
        log.info('Adding "ou" : %s under "dn" : %s...' % (ou0, DEFAULT_SUFFIX))
        add_ou_entry(topo, ou0, DEFAULT_SUFFIX)
        parent = 'ou=%s,%s' % (ou0, DEFAULT_SUFFIX)
        log.info('Adding %s under %s...' % (ouname, parent))
        add_ou_entry(topo, ouname, parent)
        user = UserAccounts(topo.standalone, parent, rdn=None)

    for idx in range(0, 2):
        parent = 'ou=%s,ou=OU%d,%s' % (ouname, idx, DEFAULT_SUFFIX)
        user = UserAccounts(topo.standalone, parent, rdn=None)
        username = '{}{}'.format(username, idx)
        log.info('Adding User: %s under %s...' % (username, parent))
        user = add_user_entry(topo, user, username, passwd, parent)

    log.info('Bind as user %s' % username)
    binddn_user = user.get(username)

    conn = binddn_user.bind(passwd)
    if not conn:
        log.error(" {} failed to authenticate: ".format(binddn_user))
        assert False

    cn_filter = '(cn=%s)' % username
    entries = Accounts(conn, DEFAULT_SUFFIX).filter('(cn=admin*)')
    log.info('Verify 2 Entries returned for cn {}'.format(cn_filter))
    assert len(entries) == 2
    for entry in entries:
        assert not entry.get_attr_val_utf8('telephonenumber')
        log.info("Verified the entries do not contain 'telephonenumber' ")
    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
