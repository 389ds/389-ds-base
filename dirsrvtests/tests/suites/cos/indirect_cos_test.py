import logging
import pytest
import os
import ldap
import time
import subprocess

from lib389 import Entry
from lib389.idm.user import UserAccounts
from lib389.idm.domain import Domain
from lib389.topologies import topology_st as topo
from lib389._constants import (DEFAULT_SUFFIX, DN_DM, PASSWORD, HOST_STANDALONE,
                               SERVERID_STANDALONE, PORT_STANDALONE)

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

TEST_USER_DN = "uid=test_user,ou=people,dc=example,dc=com"
OU_PEOPLE = 'ou=people,{}'.format(DEFAULT_SUFFIX)

PW_POLICY_CONT_PEOPLE = 'cn="cn=nsPwPolicyEntry,' \
                        'ou=people,dc=example,dc=com",' \
                        'cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'

PW_POLICY_CONT_PEOPLE2 = 'cn="cn=nsPwPolicyEntry,' \
                        'dc=example,dc=com",' \
                        'cn=nsPwPolicyContainerdc=example,dc=com'


def check_user(inst):
    """Search the test user and make sure it has the execpted attrs
    """
    try:
        entries = inst.search_s('dc=example,dc=com', ldap.SCOPE_SUBTREE, "uid=test_user")
        log.debug('user: \n' + str(entries[0]))
        assert entries[0].hasAttr('ou'), "Entry is missing ou cos attribute"
        assert entries[0].hasAttr('x-department'), "Entry is missing description cos attribute"
        assert entries[0].hasAttr('x-en-ou'), "Entry is missing givenname cos attribute"
    except ldap.LDAPError as e:
        log.fatal('Failed to search for user: ' + str(e))
        raise e


def setup_subtree_policy(topo):
    """Set up subtree password policy
    """

    topo.standalone.config.set('nsslapd-pwpolicy-local', 'on')

    log.info('Create password policy for subtree {}'.format(OU_PEOPLE))
    try:
        subprocess.call(['%s/ns-newpwpolicy.pl' % topo.standalone.get_sbin_dir(),
                         '-D', DN_DM, '-w', PASSWORD,
                         '-p', str(PORT_STANDALONE), '-h', HOST_STANDALONE,
                         '-S', DEFAULT_SUFFIX, '-Z', SERVERID_STANDALONE])
    except subprocess.CalledProcessError as e:
        log.error('Failed to create pw policy policy for {}: error {}'.format(
            OU_PEOPLE, e.message['desc']))
        raise e

    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    domain.replace('pwdpolicysubentry', PW_POLICY_CONT_PEOPLE2)

    time.sleep(1)


def setup_indirect_cos(topo):
    """Setup indirect COS definition and template
    """
    cosDef = Entry(('cn=cosDefinition,dc=example,dc=com',
                    {'objectclass': ['top', 'ldapsubentry',
                                     'cossuperdefinition',
                                     'cosIndirectDefinition'],
                     'cosAttribute': ['ou merge-schemes',
                                      'x-department merge-schemes',
                                      'x-en-ou merge-schemes'],
                     'cosIndirectSpecifier': 'seeAlso',
                     'cn': 'cosDefinition'}))

    cosTemplate = Entry(('cn=cosTemplate,dc=example,dc=com',
                         {'objectclass': ['top',
                                          'extensibleObject',
                                          'cosTemplate'],
                          'ou': 'My COS Org',
                          'x-department': 'My COS x-department',
                          'x-en-ou': 'my COS x-en-ou',
                          'cn': 'cosTemplate'}))
    try:
        topo.standalone.add_s(cosDef)
        topo.standalone.add_s(cosTemplate)
    except ldap.LDAPError as e:
        log.fatal('Failed to add cos: error ' + str(e))
        raise e
    time.sleep(1)


@pytest.fixture(scope="module")
def setup(topo, request):
    """Add schema, and test user
    """
    log.info('Add custom schema...')
    try:
        ATTR_1 = (b"( 1.3.6.1.4.1.409.389.2.189 NAME 'x-department' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN 'user defined' )")
        ATTR_2 = (b"( 1.3.6.1.4.1.409.389.2.187 NAME 'x-en-ou' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN 'user defined' )")
        OC = (b"( xPerson-oid NAME 'xPerson' DESC '' SUP person STRUCTURAL MAY ( x-department $ x-en-ou ) X-ORIGIN 'user defined' )")
        topo.standalone.modify_s("cn=schema", [(ldap.MOD_ADD, 'attributeTypes', ATTR_1),
                                               (ldap.MOD_ADD, 'attributeTypes', ATTR_2),
                                               (ldap.MOD_ADD, 'objectClasses', OC)])
    except ldap.LDAPError as e:
        log.fatal('Failed to add custom schema')
        raise e
    time.sleep(1)

    log.info('Add test user...')
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)

    user_properties = {
        'uid': 'test_user',
        'cn': 'test user',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/test_user',
        'seeAlso': 'cn=cosTemplate,dc=example,dc=com'
    }
    user = users.create(properties=user_properties)

    user.add('objectClass', 'xPerson')

    # Setup COS
    log.info("Setup indirect COS...")
    setup_indirect_cos(topo)


def test_indirect_cos(topo, setup):
    """Test indirect cos

    :id: 890d5929-7d52-4a56-956e-129611b4649a
    :setup: standalone
    :steps:
        1. Test cos is working for test user
        2. Add subtree password policy
        3. Test cos is working for test user
    :expectedresults:
        1. User has expected cos attrs
        2. Substree password policy setup is successful
        3. User still has expected cos attrs
    """

    # Step 1 - Search user and see if the COS attrs are included
    log.info('Checking user...')
    check_user(topo.standalone)

    # Step 2 - Add subtree password policy (Second COS - operational attribute)
    setup_subtree_policy(topo)

    # Step 3 - Check user again now hat we have a mix of vattrs
    log.info('Checking user...')
    check_user(topo.standalone)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

