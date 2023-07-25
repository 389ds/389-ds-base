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
from lib389._constants import PASSWORD, DN_DM, DEFAULT_SUFFIX
from lib389._constants import SUFFIX, PASSWORD, DN_DM, DN_CONFIG, PLUGIN_RETRO_CHANGELOG, DEFAULT_SUFFIX, DEFAULT_CHANGELOG_DB, DEFAULT_BENAME
from lib389 import Entry
from lib389.topologies import topology_m1 as topo_supplier
from lib389.idm.user import UserAccounts
from lib389.utils import ldap, os, logging, ds_is_newer, ds_supports_new_changelog
from lib389.topologies import topology_st as topo
from lib389.idm.organizationalunit import OrganizationalUnits

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

    for entry in dbscanOut.split('dbid: '):
        if 'operation: modify' in entry and user_dn in entry and 'userPassword' in entry:
            if is_present:
                assert unhashed_pwd_attribute in entry
            else:
                assert unhashed_pwd_attribute not in entry

@pytest.fixture(scope="module")
def passw_policy(topo, request):
    """Configure password policy with PasswordCheckSyntax attribute set to on"""

    log.info('Configure Pwpolicy with PasswordCheckSyntax and nsslapd-pwpolicy-local set to on')
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    topo.standalone.config.set('PasswordExp', 'on')
    topo.standalone.config.set('PasswordCheckSyntax', 'off')
    topo.standalone.config.set('nsslapd-pwpolicy-local', 'on')

    subtree = 'ou=people,{}'.format(DEFAULT_SUFFIX)
    log.info('Configure subtree password policy for {}'.format(subtree))
    topo.standalone.subtreePwdPolicy(subtree, {'passwordchange': b'on',
                                               'passwordCheckSyntax': b'on',
                                               'passwordLockout': b'on',
                                               'passwordResetFailureCount': b'3',
                                               'passwordLockoutDuration': b'3',
                                               'passwordMaxFailure': b'2'})
    time.sleep(1)

    def fin():
        log.info('Reset pwpolicy configuration settings')
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
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


@pytest.mark.bz1465600
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

@pytest.mark.ds49789
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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
