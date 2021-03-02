import logging
import pytest 
import time, ldap, re, os
from lib389.schema import Schema
from lib389.utils import ensure_bytes
from lib389.topologies import topology_st as topo
from lib389._mapped_object import DSLdapObject
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.idm.services import ServiceAccounts
from lib389.idm.user import UserAccounts
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PW_DM

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def add_ou_entry(topo, name, myparent):
    
    ou_dn = 'ou={},{}'.format(name, myparent)                     

    ou = OrganizationalUnit(topo.standalone,dn=ou_dn)
    ou.create(properties={'ou': name})


def add_user_entry(topo, user, name, pw, myparent):
    dn = 'ou=%s,%s' % (name, myparent)
    properties = {
            'uid': name,
            'cn' : name,
            'sn' : name,
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory':'/home/{}'.format(name),
            'telephonenumber': '+1 222 333-4444',
            'userpassword' : pw,
        }

    user.create(properties=properties)
    return user


def test_aci_with_exclude_filter(topo):
    """
       Test an ACI(Access control instruction) which contains an extensible filter.
       Test that during the schema reload task there is a small window where the new schema is not loaded
       into the asi hashtables - this results in searches not returning entries.
    :id: test_aci_with_exclude_filter
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
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. PASS - users found do not have the --> deniedattr = 'telephonenumber' marker

    """
    log.info('Bind as root DN')
  
    log.info('Create an OU for them')    
    ous = OrganizationalUnit(topo.standalone, DEFAULT_SUFFIX)
#   # Create the OU for them
  
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
    try:
         topo.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', ensure_bytes(aci_text))])
    except ldap.LDAPError as e:
         log.error('Failed to add aci: (%s) error %s' % (aci_text, e.args[0]['desc']))
         assert False

    log.info('Add entries ...')
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
        username = '{}{}'.format(username, idx)
        log.info('Adding User: %s under %s...' % (username, parent))
        
        user = add_user_entry(topo, user, username, passwd, parent)


    # binddn = 'cn=%s,%s' % (username, parent)
    log.info('Bind as user %s' % username)
    binddn_user = user.get(username)
    
    try:
        conn = binddn_user.bind(passwd)
        
    except :
        log.error(" {} failed to authenticate: ".format(binddn_user))
        assert False

    cn_filter = '(cn=%s)' % username
    try:
        # entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, cn_filter, [deniedattr, 'dn'])
        entries = user.get((DEFAULT_SUFFIX, cn_filter, [deniedattr, 'dn']))
        log.info("Entries {}".format(entries))
        
        assert 2 == len(entries)
        for idx in range(0, 1):
            if entries[idx].hasAttr(deniedattr):
                log.fatal('aci with extensible filter failed -- %s')
                assert False
    except:
        log.error('Search (%s, %s) failed: ' % (DEFAULT_SUFFIX, cn_filter))
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
