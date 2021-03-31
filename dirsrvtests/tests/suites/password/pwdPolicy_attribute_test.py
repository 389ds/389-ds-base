# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.pwpolicy import PwPolicyManager
from lib389.idm.user import UserAccount, UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389._constants import (DEFAULT_SUFFIX, DN_DM, PASSWORD)

pytestmark = pytest.mark.tier1

OU_PEOPLE = 'ou=people,{}'.format(DEFAULT_SUFFIX)
TEST_USER_NAME = 'simplepaged_test'
TEST_USER_DN = 'uid={},{}'.format(TEST_USER_NAME, OU_PEOPLE)
TEST_USER_PWD = 'simplepaged_test'
PW_POLICY_CONT_USER = 'cn="cn=nsPwPolicyEntry,uid=simplepaged_test,' \
                      'ou=people,dc=example,dc=com",' \
                      'cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'
PW_POLICY_CONT_PEOPLE = 'cn="cn=nsPwPolicyEntry,' \
                        'ou=people,dc=example,dc=com",' \
                        'cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def create_user(topology_st, request):
    """User for binding operation"""
    topology_st.standalone.config.set('nsslapd-auditlog-logging-enabled', 'on')
    log.info('Adding test user {}')
    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': TEST_USER_NAME, 'userpassword': TEST_USER_PWD})
    try:
        user = users.create(properties=user_props)
    except:
        pass  # debug only

    USER_ACI = '(targetattr="*")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///%s";)' % user.dn
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    ou_people = ous.get('people')
    ou_people.add('aci', USER_ACI)

    def fin():
        log.info('Deleting user {}'.format(user.dn))
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    request.addfinalizer(fin)


@pytest.fixture(scope="module")
def password_policy(topology_st, create_user):
    """Set up password policy for subtree and user"""

    pwp = PwPolicyManager(topology_st.standalone)
    policy_props = {}
    log.info('Create password policy for subtree {}'.format(OU_PEOPLE))
    pwp.create_subtree_policy(OU_PEOPLE, policy_props)

    log.info('Create password policy for user {}'.format(TEST_USER_DN))
    pwp.create_user_policy(TEST_USER_DN, policy_props)


@pytest.mark.skipif(ds_is_older('1.4.3.3'), reason="Not implemented")
def test_pwdReset_by_user_DM(topology_st, create_user):
    """Test new password policy attribute "pwdReset by DM user"
    :id: 232bc7dc-8cb6-11eb-9791-98fa9ba19b65
    :customerscenario: True
    :setup:
        1. Standalone instance
        2. Add a new user with a password 
    :steps:
        1. Enable passwordMustChange
        2. Bind as the user and change the password
        3. Check that the pwdReset attribute is set to TRUE
        4. Bind as the Directory manager and attempt to change the pwdReset to FALSE
        5. Check that pwdReset is NOT SET to FALSE
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success - should get a UNWILLING_TO_PERFORM 
        5. Success
    """

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    log.info('Set password policy passwordMustChange on')
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    our_user.replace('userpassword', PASSWORD)
    time.sleep(5)

    # Check that pwdReset is TRUE
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    log.info('Binding as the Directory manager and attempt to change the pwdReset to FALSE')
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology_st.standalone.config.replace('pwdReset', 'FALSE')

    log.info('Check that pwdReset is NOT SET to FALSE')
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    log.info('Resetting password for {}'.format(TEST_USER_PWD))
    our_user.reset_password(TEST_USER_PWD)


@pytest.mark.skipif(ds_is_older('1.4.3.3'), reason="Not implemented")
def test_pwd_reset(topology_st, create_user):
    """Test new password policy attribute "pwdReset"

    :id: 03db357b-4800-411e-a36e-28a534293004
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Reset user's password
        3. Check that the pwdReset attribute is set to TRUE
        4. Bind as the user and change its password
        5. Check that pwdReset is now set to FALSE
        6. Reset password policy configuration
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    # Set password policy config
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    time.sleep(.5)

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    our_user.replace('userpassword', PASSWORD)
    time.sleep(.5)

    # Check that pwdReset is TRUE
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Bind as user and change its own password
    our_user.rebind(PASSWORD)
    our_user.replace('userpassword', PASSWORD)
    time.sleep(.5)

    # Check that pwdReset is FALSE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert our_user.get_attr_val_utf8('pwdReset') == 'FALSE'

    # Reset password policy config
    topology_st.standalone.config.replace('passwordMustChange', 'off')

    # Reset user's password
    our_user.replace('userpassword', TEST_USER_PWD)


@pytest.mark.parametrize('subtree_pwchange,user_pwchange,exception',
                         [('on', 'off', ldap.UNWILLING_TO_PERFORM),
                          ('off', 'off', ldap.UNWILLING_TO_PERFORM),
                          ('off', 'on', False), ('on', 'on', False)])
def test_change_pwd(topology_st, create_user, password_policy,
                    subtree_pwchange, user_pwchange, exception):
    """Verify that 'passwordChange' attr works as expected
    User should have a priority over a subtree.

    :id: 2c884432-2ba1-4662-8e5d-2cd49f77e5fa
    :parametrized: yes
    :setup: Standalone instance, a test user,
            password policy entries for a user and a subtree
    :steps:
        1. Set passwordChange on the user and the subtree
           to various combinations
        2. Bind as test user
        3. Try to change password
        4. Clean up - change the password to default while bound as DM
    :expectedresults:
        1. passwordChange should be successfully set
        2. Bind should be successful
        3. Subtree/User passwordChange - result, accordingly:
           off/on, on/on - success;
           on/off, off/off - UNWILLING_TO_PERFORM
        4. Operation should be successful
    """

    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user = users.get(TEST_USER_NAME)

    log.info('Set passwordChange to "{}" - {}'.format(subtree_pwchange, OU_PEOPLE))
    pwp = PwPolicyManager(topology_st.standalone)
    subtree_policy = pwp.get_pwpolicy_entry(OU_PEOPLE)
    subtree_policy.set('passwordChange', subtree_pwchange)

    time.sleep(1)

    log.info('Set passwordChange to "{}" - {}'.format(user_pwchange, TEST_USER_DN))
    pwp2 = PwPolicyManager(topology_st.standalone)
    user_policy = pwp2.get_pwpolicy_entry(TEST_USER_DN)
    user_policy.set('passwordChange', user_pwchange)
    user_policy.set('passwordExp', 'on')

    time.sleep(1)

    try:
        log.info('Bind as user and modify userPassword')
        user.rebind(TEST_USER_PWD)
        if exception:
            with pytest.raises(exception):
                user.reset_password('new_pass')
        else:
            user.reset_password('new_pass')
    except ldap.LDAPError as e:
        log.error('Failed to change userpassword for {}: error {}'.format(
            TEST_USER_DN, e.args[0]['info']))
        raise e
    finally:
        log.info('Bind as DM')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        user.reset_password(TEST_USER_PWD)


def test_pwd_min_age(topology_st, create_user, password_policy):
    """If we set passwordMinAge to some value, for example to 10, then it
    should not allow the user to change the password within 10 seconds after
    his previous change.

    :id: 85b98516-8c82-45bd-b9ec-90bd1245e09c
    :setup: Standalone instance, a test user,
            password policy entries for a user and a subtree
    :steps:
        1. Set passwordMinAge to 10 on the user pwpolicy entry
        2. Set passwordMinAge to 10 on the subtree pwpolicy entry
        3. Set passwordMinAge to 10 on the cn=config entry
        4. Bind as test user
        5. Try to change the password two times in a row
        6. Wait 12 seconds
        7. Try to change the password
        8. Clean up - change the password to default while bound as DM
    :expectedresults:
        1. passwordMinAge should be successfully set on the user pwpolicy entry
        2. passwordMinAge should be successfully set on the subtree pwpolicy entry
        3. passwordMinAge should be successfully set on the cn=config entry
        4. Bind should be successful
        5. The password should be successfully changed
        6. 12 seconds have passed
        7. Constraint Violation error should be raised
        8. Operation should be successful
    """

    num_seconds = '10'
    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user = users.get(TEST_USER_NAME)

    log.info('Set passwordminage to "{}" - {}'.format(num_seconds, OU_PEOPLE))
    pwp = PwPolicyManager(topology_st.standalone)
    subtree_policy = pwp.get_pwpolicy_entry(OU_PEOPLE)
    subtree_policy.set('passwordminage', num_seconds)

    log.info('Set passwordminage to "{}" - {}'.format(num_seconds, TEST_USER_DN))
    user_policy = pwp.get_pwpolicy_entry(TEST_USER_DN)
    user_policy.set('passwordminage', num_seconds)

    log.info('Set passwordminage to "{}" - {}'.format(num_seconds, DN_CONFIG))
    topology_st.standalone.config.set('passwordminage', num_seconds)

    time.sleep(1)

    log.info('Bind as user and modify userPassword')
    user.rebind(TEST_USER_PWD)
    user.reset_password('new_pass')

    time.sleep(1)

    log.info('Bind as user and modify userPassword straight away after previous change')
    user.rebind('new_pass')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user.reset_password('new_new_pass')

    log.info('Wait {} second'.format(int(num_seconds) + 2))
    time.sleep(int(num_seconds) + 2)

    try:
        log.info('Bind as user and modify userPassword')
        user.rebind('new_pass')
        user.reset_password(TEST_USER_PWD)
    except ldap.LDAPError as e:
        log.error('Failed to change userpassword for {}: error {}'.format(
            TEST_USER_DN, e.args[0]['info']))
        raise e
    finally:
        log.info('Bind as DM')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        user.reset_password(TEST_USER_PWD)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
