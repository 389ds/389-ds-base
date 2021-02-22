import pytest
import ldap, os
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st as topo
from lib389._mapped_object import DSLdapObject
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PW_DM

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def add_ou_entry(server, name, myparent):
    dn = 'ou=%s,%s' % (name, myparent)
    server.add_s(Entry((dn, {'objectclass': ['top', 'organizationalunit'],
                             'ou': name})))


def add_user_entry(server, name, pw, myparent):
    dn = 'cn=%s,%s' % (name, myparent)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person'],
                             'sn': name,
                             'cn': name,
                             'telephonenumber': '+1 222 333-4444',
                             'userpassword': pw})))


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
    try:
        ld = ldap.initialize(topo.standalone.get_ldap_uri())
        ld.simple_bind_s(DN_DM, PW_DM)
        
    except ldap.LDAPError as e:
        topo.standalone.log.error('Root DN failed to authenticate: ' + e.args[0]['desc'])
        assert False

    ouname = 'outest'
    username = 'admin'
    passwd = 'Password'
    deniedattr = 'telephonenumber'
    log.info('Add aci which contains extensible filter.')
    aci_text = ('(targetattr = "%s")' % (deniedattr) +
                '(target = "ldap:///%s")' % (DEFAULT_SUFFIX) +
                '(version 3.0;acl "admin-tel-matching-rule-outest";deny (all)' +
                '(userdn = "ldap:///%s??sub?(&(cn=%s)(ou:dn:=%s))");)' % (DEFAULT_SUFFIX, username, ouname))

    try:
        topo.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', ensure_bytes(aci_text))])
    except ldap.LDAPError as e:
        log.error('Failed to add aci: (%s) error %s' % (aci_text, e.args[0]['desc']))
        assert False

    log.info('Add entries ...')
    for idx in range(0, 2):
        ou0 = 'OU%d' % idx
        log.info('adding %s under %s...' % (ou0, DEFAULT_SUFFIX))
        add_ou_entry(topo.standalone, ou0, DEFAULT_SUFFIX)
        parent = 'ou=%s,%s' % (ou0, DEFAULT_SUFFIX)
        log.info('adding %s under %s...' % (ouname, parent))
        add_ou_entry(topo.standalone, ouname, parent)

    for idx in range(0, 2):
        parent = 'ou=%s,ou=OU%d,%s' % (ouname, idx, DEFAULT_SUFFIX)
        log.info('adding %s under %s...' % (username, parent))
        add_user_entry(topo.standalone, username, passwd, parent)

    binddn = 'cn=%s,%s' % (username, parent)
    log.info('Bind as user %s' % binddn)
    try:
        topo.standalone.simple_bind_s(binddn, passwd)
    except ldap.LDAPError as e:
        topo.standalone.log.error(bindn + ' failed to authenticate: ' + e.args[0]['desc'])
        assert False

    cn_filter = '(cn=%s)' % username
    try:
        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, cn_filter, [deniedattr, 'dn'])
        assert 2 == len(entries)
        for idx in range(0, 1):
            if entries[idx].hasAttr(deniedattr):
                log.fatal('aci with extensible filter failed -- %s')
                assert False
    except ldap.LDAPError as e:
        topo.standalone.log.error('Search (%s, %s) failed: ' % (DEFAULT_SUFFIX, cn_filter) + e.args[0]['desc'])
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
