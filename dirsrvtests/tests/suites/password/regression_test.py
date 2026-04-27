# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import time
import glob
import base64
from lib389._constants import PASSWORD, DN_DM, DEFAULT_SUFFIX
from lib389._constants import SUFFIX, PASSWORD, DN_DM, DN_CONFIG, PLUGIN_RETRO_CHANGELOG, DEFAULT_SUFFIX, DEFAULT_CHANGELOG_DB, DEFAULT_BENAME
from lib389 import Entry
from test389.topologies import topology_m1 as topo_supplier
from lib389.idm.user import UserAccounts, UserAccount
from lib389.utils import ldap, os, logging, ensure_bytes, ds_is_newer, ds_supports_new_changelog
from test389.topologies import topology_st as topo
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.pwpolicy import PwPolicyManager
from lib389.backend import Backend, Backends
from lib389.configurations.sample import create_base_org

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

user_data = {'cn': 'CNpwtest1', 'sn': 'SNpwtest1', 'uid': 'UIDpwtest1', 'mail': 'MAILpwtest1@redhat.com',
             'givenname': 'GNpwtest1'}

TEST_PASSWORDS = list(user_data.values())
# Add substring/token values of "CNpwtest1"
TEST_PASSWORDS += ['CNpwtest1ZZZZ', 'ZZZZZCNpwtest1',
                   'ZCNpwtest1', 'CNpwtest1Z', 'ZCNpwtest1Z',
                   'ZZCNpwtest1', 'CNpwtest1ZZ', 'ZZCNpwtest1ZZ',
                   'ZZZCNpwtest1', 'CNpwtest1ZZZ', 'ZZZCNpwtest1ZZZ',
                   'ZZZZZZCNpwtest1ZZZZZZZZ']

TEST_PASSWORDS2 = (
    'CN12pwtest31', 'SN3pwtest231', 'UID1pwtest123', 'MAIL2pwtest12@redhat.com', '2GN1pwtest123', 'People123')

SUPPORTED_SCHEMES = (
    "{SHA}", "{SSHA}", "{SHA256}", "{SSHA256}",
    "{SHA384}", "{SSHA384}", "{SHA512}", "{SSHA512}",
    "{crypt}", "{NS-MTA-MD5}", "{clear}", "{MD5}",
    "{SMD5}", "{PBKDF2_SHA256}", "{PBKDF2_SHA512}",
    "{GOST_YESCRYPT}", "{PBKDF2-SHA256}", "{PBKDF2-SHA512}" )

def _check_unhashed_userpw(inst, user_dn, is_present=False):
    """Check if unhashed#user#password attribute is present or not in the changelog"""
    unhashed_pwd_attribute = 'unhashed#user#password'

    if ds_supports_new_changelog():
        dbscanOut = inst.dbscan(DEFAULT_BENAME, 'replication_changelog')
    else:
        changelog_dbdir = os.path.join(os.path.dirname(inst.dbdir), DEFAULT_CHANGELOG_DB)
        for changelog_dbfile in glob.glob(f'{changelog_dbdir}*/*.db*'):
            log.info('Changelog dbfile file exist: {}'.format(changelog_dbfile))
            dbscanOut = inst.dbscan(DEFAULT_CHANGELOG_DB, changelog_dbfile)

    for entry in dbscanOut.split(b'dbid: '):
        if ensure_bytes('operation: modify') in entry and ensure_bytes(user_dn) in entry and ensure_bytes('userPassword') in entry:
            if is_present:
                assert ensure_bytes(unhashed_pwd_attribute) in entry
            else:
                assert ensure_bytes(unhashed_pwd_attribute) not in entry

@pytest.fixture(scope="function")
def passw_policy(topo, request):
    """Configure password policy with PasswordCheckSyntax attribute set to on"""

    log.info('Configure Pwpolicy with PasswordCheckSyntax and nsslapd-pwpolicy-local set to on')
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    topo.standalone.config.set('PasswordExp', 'on')
    topo.standalone.config.set('PasswordCheckSyntax', 'off')
    topo.standalone.config.set('nsslapd-pwpolicy-local', 'on')

    subtree = 'ou=people,{}'.format(DEFAULT_SUFFIX)
    log.info('Configure subtree password policy for {}'.format(subtree))
    pwp = PwPolicyManager(topo.standalone)
    pwp.create_subtree_policy(subtree, {'passwordchange': 'on',
                                        'passwordCheckSyntax': 'on',
                                        'passwordLockout': 'on',
                                        'passwordResetFailureCount': '3',
                                        'passwordLockoutDuration': '3',
                                        'passwordMaxFailure': '2'})
    time.sleep(1)

    def fin():
        log.info('Reset pwpolicy configuration settings')
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        pwp.delete_local_policy(subtree)
        topo.standalone.config.set('PasswordExp', 'off')
        topo.standalone.config.set('PasswordCheckSyntax', 'off')
        topo.standalone.config.set('nsslapd-pwpolicy-local', 'off')

    request.addfinalizer(fin)


@pytest.fixture(scope="module")
def create_user(topo, request):
    """Add test users using UserAccounts"""

    log.info('Adding user-uid={},ou=people,{}'.format(user_data['uid'], DEFAULT_SUFFIX))
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user_properties = {
        'uidNumber': '1001',
        'gidNumber': '2001',
        'cn': 'pwtest1',
        'userpassword': PASSWORD,
        'homeDirectory': '/home/pwtest1'}
    user_properties.update(user_data)
    tuser = users.create(properties=user_properties)

    def fin():
        log.info('Deleting user-{}'.format(tuser.dn))
        tuser.delete()

    request.addfinalizer(fin)
    return tuser


def test_pwp_local_unlock(topo, passw_policy, create_user):
    """Test subtree policies use the same global default for passwordUnlock

    :id: 741a8417-5f65-4012-b9ed-87987ce3ca1b
    :setup: Standalone instance
    :steps:
        1. Test user can bind
        2. Bind with bad passwords to lockout account, and verify account is locked
        3. Wait for lockout interval, and bind with valid password
    :expectedresults:
        1. Bind successful
        2. Entry is locked
        3. Entry can bind with correct password
    """
    # Add aci so users can change their own password
    USER_ACI = '(targetattr="userpassword")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'
    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ou = ous.get('people')
    ou.add('aci', USER_ACI)

    log.info("Verify user can bind...")
    create_user.bind(PASSWORD)

    log.info('Test passwordUnlock default - user should be able to reset password after lockout')
    for i in range(0, 2):
        try:
            create_user.bind("bad-password")
        except ldap.INVALID_CREDENTIALS:
            # expected
            pass
        except ldap.LDAPError as e:
            log.fatal("Got unexpected failure: " + str(e))
            raise e

    log.info('Verify account is locked')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        create_user.bind(PASSWORD)

    log.info('Wait for lockout duration...')
    time.sleep(4)

    log.info('Check if user can now bind with correct password')
    create_user.bind(PASSWORD)


@pytest.mark.parametrize("user_pasw", TEST_PASSWORDS)
def test_trivial_passw_check(topo, passw_policy, create_user, user_pasw):
    """PasswordCheckSyntax attribute fails to validate cn, sn, uid, givenname, ou and mail attributes

    :id: bf9fe1ef-56cb-46a3-a6f8-5530398a06dc
    :parametrized: yes
    :setup: Standalone instance.
    :steps:
        1. Configure local password policy with PasswordCheckSyntax set to on.
        2. Add users with cn, sn, uid, givenname, mail and userPassword attributes.
        3. Configure subtree password policy for ou=people subtree.
        4. Reset userPassword with trivial values like cn, sn, uid, givenname, ou and mail attributes.
    :expectedresults:
        1. Enabling PasswordCheckSyntax should PASS.
        2. Add users should PASS.
        3. Configure subtree password policy should PASS.
        4. Resetting userPassword to cn, sn, uid and mail should be rejected.
    """

    create_user.rebind(PASSWORD)
    log.info('Replace userPassword attribute with {}'.format(user_pasw))
    with pytest.raises(ldap.CONSTRAINT_VIOLATION) as excinfo:
        create_user.reset_password(user_pasw)
        log.fatal('Failed: Userpassword with {} is accepted'.format(user_pasw))
    assert 'password based off of user entry' in str(excinfo.value)

    # reset password
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    create_user.set('userPassword', PASSWORD)


@pytest.mark.parametrize("user_pasw", TEST_PASSWORDS)
def test_global_vs_local(topo, passw_policy, create_user, user_pasw):
    """Passwords rejected if its similar to uid, cn, sn, givenname, ou and mail attributes

    :id: dfd6cf5d-8bcd-4895-a691-a43ad9ec1be8
    :parametrized: yes
    :setup: Standalone instance
    :steps:
        1. Configure global password policy with PasswordCheckSyntax set to off
        2. Add users with cn, sn, uid, mail, givenname and userPassword attributes
        3. Replace userPassword similar to cn, sn, uid, givenname, ou and mail attributes
    :expectedresults:
        1. Disabling the local policy should PASS.
        2. Add users should PASS.
        3. Resetting userPasswords similar to cn, sn, uid, givenname, ou and mail attributes should PASS.
    """

    log.info('Configure Pwpolicy with PasswordCheckSyntax and nsslapd-pwpolicy-local set to off')
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    topo.standalone.config.set('nsslapd-pwpolicy-local', 'off')

    create_user.rebind(PASSWORD)
    log.info('Replace userPassword attribute with {}'.format(user_pasw))
    create_user.reset_password(user_pasw)

    # reset password
    create_user.set('userPassword', PASSWORD)

def test_unhashed_pw_switch(topo_supplier):
    """Check that nsslapd-unhashed-pw-switch works corrently

    :id: e5aba180-d174-424d-92b0-14fe7bb0b92a
    :setup: Supplier Instance
    :steps:
        1. A Supplier is created, enable retrocl (not  used here)
        2. Create a set of users
        3. update userpassword of user1 and check that unhashed#user#password is not logged (default)
        4. udpate userpassword of user2 and check that unhashed#user#password is not logged ('nolog')
        5. udpate userpassword of user3 and check that unhashed#user#password is logged ('on')
    :expectedresults:
        1. Success
        2. Success
        3.  Success (unhashed#user#password is not logged in the replication changelog)
        4. Success (unhashed#user#password is not logged in the replication changelog)
        5. Success (unhashed#user#password is logged in the replication changelog)
    """
    MAX_USERS = 10
    PEOPLE_DN = ("ou=people," + DEFAULT_SUFFIX)

    inst = topo_supplier.ms["supplier1"]
    inst.modify_s("cn=Retro Changelog Plugin,cn=plugins,cn=config",
                                        [(ldap.MOD_REPLACE, 'nsslapd-changelogmaxage', b'2m'),
                                         (ldap.MOD_REPLACE, 'nsslapd-changelog-trim-interval', b"5s"),
                                         (ldap.MOD_REPLACE, 'nsslapd-logAccess', b'on')])
    inst.config.loglevel(vals=[256 + 4], service='access')
    inst.restart()
    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # enable dynamic plugins, memberof and retro cl plugin
    #
    log.info('Enable plugins...')
    try:
        inst.modify_s(DN_CONFIG,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-dynamic-plugins',
                                          b'on')])
    except ldap.LDAPError as e:
        ldap.error('Failed to enable dynamic plugins! ' + e.message['desc'])
        assert False

    #topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    inst.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)
    #topology_st.standalone.modify_s("cn=changelog,cn=ldbm database,cn=plugins,cn=config", [(ldap.MOD_REPLACE, 'nsslapd-cachememsize', str(100000))])
    inst.restart()

    log.info('create users and group...')
    for idx in range(1, MAX_USERS):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, PEOPLE_DN))
            inst.add_s(Entry((USER_DN,
                                                {'objectclass': 'top extensibleObject'.split(),
                                                 'uid': 'member%d' % (idx)})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add user (%s): error %s' % (USER_DN, e.message['desc']))
            assert False

    # Check default is that unhashed#user#password is not logged on 1.4.1.6+
    user = "uid=member1,%s" % (PEOPLE_DN)
    inst.modify_s(user, [(ldap.MOD_REPLACE,
                                          'userpassword',
                                          PASSWORD.encode())])
    inst.stop()
    if ds_is_newer('1.4.1.6'):
        _check_unhashed_userpw(inst, user, is_present=False)
    else:
        _check_unhashed_userpw(inst, user, is_present=True)
    inst.start()

    #  Check with nolog that unhashed#user#password is not logged
    inst.modify_s(DN_CONFIG,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-unhashed-pw-switch',
                                          b'nolog')])
    inst.restart()
    user = "uid=member2,%s" % (PEOPLE_DN)
    inst.modify_s(user, [(ldap.MOD_REPLACE,
                                          'userpassword',
                                          PASSWORD.encode())])
    inst.stop()
    _check_unhashed_userpw(inst, user, is_present=False)
    inst.start()

    #  Check with value 'on' that unhashed#user#password is logged
    inst.modify_s(DN_CONFIG,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-unhashed-pw-switch',
                                          b'on')])
    inst.restart()
    user = "uid=member3,%s" % (PEOPLE_DN)
    inst.modify_s(user, [(ldap.MOD_REPLACE,
                                          'userpassword',
                                          PASSWORD.encode())])
    inst.stop()
    _check_unhashed_userpw(inst, user, is_present=True)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

@pytest.mark.parametrize("scheme", SUPPORTED_SCHEMES )
def test_long_hashed_password(topo, create_user, scheme):
    """Check that hashed password with very long value does not cause trouble

    :id: 252a1f76-114b-11ef-8a7a-482ae39447e5
    :setup: standalone Instance
    :parametrized: yes
    :steps:
        1. Add a test user user
        2. Set a long password with requested scheme
        3. Bind on that user using a wrong password
        4. Check that instance is still alive
        5. Remove the added user
    :expectedresults:
        1. Success
        2. Success
        3. Should get ldap.INVALID_CREDENTIALS exception
        4. Success
        5. Success
    """
    inst = topo.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    # Make sure that server is started as this test may crash it
    inst.start()
    # Adding Test user (It may already exists if previous test failed)
    user2 = UserAccount(inst, dn='uid=test_user_1002,ou=People,dc=example,dc=com')
    if not user2.exists():
        user2 = users.create_test_user(uid=1002, gid=2002)
    # Setting hashed password
    passwd = 'A'*4000
    hashed_passwd = scheme.encode('utf-8') + base64.b64encode(passwd.encode('utf-8'))
    user2.replace('userpassword', hashed_passwd)
    # Bind on that user using a wrong password
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        conn = user2.bind(PASSWORD)
    # Check that instance is still alive
    assert inst.status()
    # Remove the added user
    user2.delete()


SECOND_SUFFIX_COS = b'o=netscaperoot'
BE_NAME_COS = 'netscaperoot'


def test_cos_vattr_cache_invalidation(topo, create_user):
    """COS def changes must be reflected in users (vattr cache invalidation).

    With multiple suffixes, if the last suffix checked has no COS entries, the vattr cache
    may not be invalidated, so users could keep stale pwdpolicysubentry after delete/re-add
    of policy. This test verifies the virtual attribute updates correctly.

    :id: 8f4e2a1c-9b3d-4e6a-8c7f-1d2e3a4b5c6d
    :setup: Standalone instance, create_user fixture (user under ou=People)
    :steps:
        1. Create a second suffix with no COS entries
        2. Enable local pwpolicy and add subtree policy for ou=people via PwPolicyManager
        3. Assert create_user has pwdpolicysubentry
        4. Delete the subtree policy
        5. Assert create_user no longer has pwdpolicysubentry
        6. Re-add the subtree policy
        7. Assert create_user has pwdpolicysubentry again
        8. Cleanup: delete the subtree policy and second backend
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """
    inst = topo.standalone
    people_dn = f'ou=people,{DEFAULT_SUFFIX}'


    inst.simple_bind_s(DN_DM, PASSWORD)

    # Create second suffix using lib389 Backend (backend + mapping tree) and suffix root entry.
    # The Suffix class is for querying (list, toBackend, getParent); creation uses Backend + sample.
    try:
        be = Backend(inst)
        be.create(properties={'cn': BE_NAME_COS, 'nsslapd-suffix': SECOND_SUFFIX_COS})
    except ldap.ALREADY_EXISTS:
        pass
    try:
        create_base_org(inst, SECOND_SUFFIX_COS)
    except ldap.ALREADY_EXISTS:
        pass
    assert SECOND_SUFFIX_COS in inst.suffix.list(), 'Second suffix should be in suffix list'

    # Set up subtree password policy
    inst.config.set('nsslapd-pwpolicy-local', 'on')

    pwp = PwPolicyManager(inst)
    policy_props = {
        'passwordMustChange': 'off',
        'passwordExp': 'off',
        'passwordHistory': 'off',
        'passwordMinAge': '0',
        'passwordChange': 'off',
        'passwordStorageScheme': 'ssha',
    }
    pwp.create_subtree_policy(people_dn, policy_props)

    # User must have pwdpolicysubentry
    assert len(create_user.get_attr_vals_utf8('pwdpolicysubentry')) > 0, (
        'User should have pwdpolicysubentry after adding policy'
    )

    # Delete subtree policy
    pwp.delete_local_policy(people_dn)
    time.sleep(0.5)

    # User must no longer have pwdpolicysubentry
    assert len(create_user.get_attr_vals_utf8('pwdpolicysubentry')) == 0, (
        'User should not have pwdpolicysubentry after deleting policy'
    )

    # Re-add subtree policy
    pwp.create_subtree_policy(people_dn, policy_props)
    time.sleep(0.5)

    # User must have pwdpolicysubentry again
    assert len(create_user.get_attr_vals_utf8('pwdpolicysubentry')) > 0, (
        'User should have pwdpolicysubentry after re-adding policy'
    )

    # Cleanup: remove subtree policy and second backend
    pwp.delete_local_policy(people_dn)
    inst.config.set('nsslapd-pwpolicy-local', 'off')
    Backends(inst).get(SECOND_SUFFIX_COS.decode('utf-8')).delete()


def test_nested_cos_ordering(topo):
    """Multiple nested COS pointer defs must apply correct policy per user.

    The COS plugin sorts attribute indexes by subtree; each user must receive the
    pwdpolicysubentry from their own (closest) branch. This test verifies ordering.

    :id: c7d8e9f0-1a2b-4c5d-9e8f-7a6b5c4d3e2f
    :setup: Standalone instance
    :steps:
        1. Create six nested OUs
        2. Create one user in each branch
        3. Enable local pwpolicy and add a subtree policy per branch
        4. For each user, assert pwdpolicysubentry equals the policy DN for that user's branch
        5. Cleanup: remove created users and policies
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    inst = topo.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)

    branch1 = 'ou=level1,' + DEFAULT_SUFFIX
    branch2 = 'ou=level2,' + branch1
    branch3 = 'ou=level3,' + branch2
    branch4 = 'ou=people,' + DEFAULT_SUFFIX
    branch5 = 'ou=lower,' + branch4
    branch6 = 'ou=lower,' + branch5

    # Define nested branches: (branch_dn, ou_rdn_value) for creation order
    branches = [branch1, branch2, branch3, branch4, branch5, branch6]
    user_rdns = ['user1', 'user2', 'user3', 'user4', 'user5', 'user6']
    policy_props = {
        'passwordMustChange': 'off',
        'passwordExp': 'off',
        'passwordHistory': 'off',
        'passwordMinAge': '0',
        'passwordChange': 'off',
        'passwordStorageScheme': 'ssha',
    }

    # Create branch OUs using lib389 (parent before children)
    ous_top = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    ous_top.ensure_state(rdn='ou=level1', properties={'ou': 'level1'})
    OrganizationalUnits(inst, branch1).ensure_state(rdn='ou=level2', properties={'ou': 'level2'})
    OrganizationalUnits(inst, branch2).ensure_state(rdn='ou=level3', properties={'ou': 'level3'})
    ous_top.ensure_state(rdn='ou=people', properties={'ou': 'people'})
    OrganizationalUnits(inst, branch4).ensure_state(rdn='ou=lower', properties={'ou': 'lower'})
    OrganizationalUnits(inst, branch5).ensure_state(rdn='ou=lower', properties={'ou': 'lower'})

    # Create users in each branch using lib389 UserAccounts
    user_dns = []
    for i, branch_dn in enumerate(branches):
        users = UserAccounts(inst, branch_dn, rdn=None)
        user_props = {
            'uid': user_rdns[i],
            'cn': user_rdns[i],
            'sn': user_rdns[i],
            'uidNumber': str(1000 + i),
            'gidNumber': '2000',
            'homeDirectory': f'/home/{user_rdns[i]}',
        }
        users.ensure_state(rdn=f'uid={user_rdns[i]}', properties=user_props)
        user_dns.append(f'uid={user_rdns[i]},{branch_dn}')

    inst.config.set('nsslapd-pwpolicy-local', 'on')
    time.sleep(0.5)

    # Add subtree policies
    pwp = PwPolicyManager(inst)
    expected_policy_dns = []
    for branch_dn in branches:
        pwp_entry = pwp.create_subtree_policy(branch_dn, policy_props)
        expected_policy_dns.append(pwp_entry.get_attr_val_utf8('dsEntryDN'))

    time.sleep(1)

    # Each user must have the policy DN for their branch (via COS pwdpolicysubentry).
    # Read via UserAccount and assert the attribute value.
    for i, user_dn in enumerate(user_dns):
        user = UserAccount(inst, user_dn)
        actual_policy = user.get_attr_val_utf8('pwdpolicysubentry')
        assert actual_policy == expected_policy_dns[i], (
            f'User {user_dn} should have pwdpolicysubentry {expected_policy_dns[i]}, got {actual_policy}'
        )

    # Cleanup: remove created users and policies
    for user_dn in user_dns:
        user = UserAccount(inst, user_dn)
        user.delete()
    for branch in branches:
        pwp.delete_local_policy(branch)
    inst.config.set('nsslapd-pwpolicy-local', 'off')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
