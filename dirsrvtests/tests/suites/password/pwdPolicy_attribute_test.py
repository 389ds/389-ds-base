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
import pdb
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
def test_user(topology_st, request):
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
    return user


@pytest.fixture(scope="module")
def password_policy(topology_st, test_user):
    """Set up password policy for subtree and user"""

    pwp = PwPolicyManager(topology_st.standalone)
    policy_props = {}
    log.info('Create password policy for subtree {}'.format(OU_PEOPLE))
    pwp.create_subtree_policy(OU_PEOPLE, policy_props)

    log.info('Create password policy for user {}'.format(TEST_USER_DN))
    pwp.create_user_policy(TEST_USER_DN, policy_props)

@pytest.mark.skipif(ds_is_older('1.4.3.3'), reason="Not implemented")
def test_pwd_reset(topology_st, test_user):
    """Test new password policy attribute "pwdReset"

    :id: 03db357b-4800-411e-a36e-28a534293004
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
def test_change_pwd(topology_st, test_user, password_policy,
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


def test_pwd_min_age(topology_st, test_user, password_policy):
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

def test_global_tpr_maxuse_1(topology_st, test_user, request):
    """Test global TPR policy : passwordTPRMaxUse
    Test that after passwordTPRMaxUse failures to bind
    additional bind with valid password are failing with CONSTRAINT_VIOLATION

    :id: d1b38436-806c-4671-8ccf-c8fdad21f034
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Set passwordTPRMaxUse=5
        3. Set passwordMaxFailure to a higher value to not disturb the test
        4. Bind with a wrong password passwordTPRMaxUse times and check INVALID_CREDENTIALS
        5. Check that passwordTPRRetryCount got to the limit (5)
        6. Bind with a wrong password (CONSTRAINT_VIOLATION)
           and check passwordTPRRetryCount overpass the limit by 1 (6)
        7. Bind with a valid password 5 times and check CONSTRAINT_VIOLATION
           and check passwordTPRRetryCount overpass the limit by 1 (6)
        8. Reset password policy configuration
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """

    try_tpr_failure = 5
    # Set password policy config, passwordMaxFailure being higher than
    # passwordTPRMaxUse so that TPR is enforced first
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordMaxFailure', str(try_tpr_failure + 20))
    topology_st.standalone.config.replace('passwordTPRMaxUse', str(try_tpr_failure))
    time.sleep(.5)

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    our_user.replace('userpassword', PASSWORD)
    time.sleep(.5)

    # look up to passwordTPRMaxUse with failing
    # bind to check that the limits of TPR are enforced
    for i in range(try_tpr_failure):
        # Bind as user with a wrong password
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            our_user.rebind('wrong password')
        time.sleep(.5)

        # Check that pwdReset is TRUE
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        #assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

        # Check that pwdTPRReset is TRUE
        assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
        assert our_user.get_attr_val_utf8('pwdTPRUseCount') == str(i+1)
        log.info("%dth failing bind (INVALID_CREDENTIALS) => pwdTPRUseCount = %d" % (i+1, i+1))


    # Now the #failures reached passwordTPRMaxUse
    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    #assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    assert our_user.get_attr_val_utf8('pwdTPRUseCount') == str(try_tpr_failure)
    log.info("last failing bind (INVALID_CREDENTIALS) => pwdTPRUseCount = %d" % (try_tpr_failure))

    # Bind as user with wrong password --> ldap.CONSTRAINT_VIOLATION
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        our_user.rebind("wrong password")
    time.sleep(.5)

    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    #assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    assert our_user.get_attr_val_utf8('pwdTPRUseCount') == str(try_tpr_failure + 1)
    log.info("failing bind (CONSTRAINT_VIOLATION) => pwdTPRUseCount = %d" % (try_tpr_failure + i))

    # Now check that all next attempts with correct password are all in LDAP_CONSTRAINT_VIOLATION
    # and passwordTPRRetryCount remains unchanged
    # account is now similar to locked
    for i in range(10):
        # Bind as user with valid password
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            our_user.rebind(PASSWORD)
        time.sleep(.5)

        # Check that pwdReset is TRUE
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        #assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

        # Check that pwdTPRReset is TRUE
        # pwdTPRUseCount keeps increasing
        assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
        assert our_user.get_attr_val_utf8('pwdTPRUseCount') == str(try_tpr_failure + i + 2)
        log.info("Rejected bind (CONSTRAINT_VIOLATION) => pwdTPRUseCount = %d" % (try_tpr_failure + i + 2))


    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # Reset user's password
        our_user.replace('userpassword', TEST_USER_PWD)


    request.addfinalizer(fin)

def test_global_tpr_maxuse_2(topology_st, test_user, request):
    """Test global TPR policy : passwordTPRMaxUse
    Test that after less than passwordTPRMaxUse failures to bind
    additional bind with valid password are successfull

    :id: bd18bf8e-f3c3-4612-9009-500cf558317e
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Set passwordTPRMaxUse=5
        3. Set passwordMaxFailure to a higher value to not disturb the test
        4. Bind with a wrong password less than passwordTPRMaxUse times and check INVALID_CREDENTIALS
        7. Bind successfully with a valid password 10 times
           and check passwordTPRRetryCount returns to 0
        8. Reset password policy configuration
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """

    try_tpr_failure = 5
    # Set password policy config, passwordMaxFailure being higher than
    # passwordTPRMaxUse so that TPR is enforced first
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordMaxFailure', str(try_tpr_failure + 20))
    topology_st.standalone.config.replace('passwordTPRMaxUse', str(try_tpr_failure))
    time.sleep(.5)

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    our_user.replace('userpassword', PASSWORD)
    time.sleep(.5)

    # Do less than passwordTPRMaxUse failing bind
    try_tpr_failure = try_tpr_failure - 2
    for i in range(try_tpr_failure):
        # Bind as user with a wrong password
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            our_user.rebind('wrong password')
        time.sleep(.5)

        # Check that pwdReset is TRUE
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        #assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

        # Check that pwdTPRReset is TRUE
        assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
        assert our_user.get_attr_val_utf8('pwdTPRUseCount') == str(i+1)
        log.info("%dth failing bind (INVALID_CREDENTIALS) => pwdTPRUseCount = %d" % (i+1, i+1))


    # Now the #failures has not reached passwordTPRMaxUse
    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    assert our_user.get_attr_val_utf8('pwdTPRUseCount') == str(try_tpr_failure)
    log.info("last failing bind (INVALID_CREDENTIALS) => pwdTPRUseCount = %d" % (try_tpr_failure))

    our_user.rebind(PASSWORD)
    our_user.replace('userpassword', PASSWORD)
    # give time to update the pwp attributes in the entry
    time.sleep(.5)
    # Now check that all next attempts with correct password are successfull
    # and passwordTPRRetryCount reset to 0
    for i in range(10):
        # Bind as user with valid password
        our_user.rebind(PASSWORD)
        time.sleep(.5)

        # Check that pwdReset is TRUE
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        #assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

        # Check that pwdTPRReset is FALSE
        assert our_user.get_attr_val_utf8('pwdTPRReset') == 'FALSE'
        #pdb.set_trace()
        assert not our_user.present('pwdTPRUseCount')


    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # Reset user's password
        our_user.replace('userpassword', TEST_USER_PWD)

    request.addfinalizer(fin)

def test_global_tpr_maxuse_3(topology_st, test_user, request):
    """Test global TPR policy : passwordTPRMaxUse
    Test that after less than passwordTPRMaxUse failures to bind
    A bind with valid password is successfull but passwordMustChange
    does not allow to do a search.
    Changing the password allows to do a search

    :id: 7fd0301a-781e-4db8-a4bd-7b44e0f04bb6
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Set passwordTPRMaxUse=5
        3. Set passwordMaxFailure to a higher value to not disturb the test
        4. Bind with a wrong password less then passwordTPRMaxUse times and check INVALID_CREDENTIALS
        5. Bind with the valid password and check SRCH fail (ldap.UNWILLING_TO_PERFORM)
           because of passwordMustChange
        6. check passwordTPRRetryCount reset to 0
        7. Bindd with valid password and reset the password
        8. Check we can bind again and SRCH succeeds
        9. Reset password policy configuration
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
    """

    try_tpr_failure = 5
    # Set password policy config, passwordMaxFailure being higher than
    # passwordTPRMaxUse so that TPR is enforced first
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordMaxFailure', str(try_tpr_failure + 20))
    topology_st.standalone.config.replace('passwordTPRMaxUse', str(try_tpr_failure))
    time.sleep(.5)

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    our_user.replace('userpassword', PASSWORD)
    # give time to update the pwp attributes in the entry
    time.sleep(.5)

    # Do less than passwordTPRMaxUse failing bind
    try_tpr_failure = try_tpr_failure - 2
    for i in range(try_tpr_failure):
        # Bind as user with a wrong password
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            our_user.rebind('wrong password')
        time.sleep(.5)

        # Check that pwdReset is TRUE
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        #assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

        # Check that pwdTPRReset is TRUE
        assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
        assert our_user.get_attr_val_utf8('pwdTPRUseCount') == str(i+1)
        log.info("%dth failing bind (INVALID_CREDENTIALS) => pwdTPRUseCount = %d" % (i+1, i+1))


    # Now the #failures has not reached passwordTPRMaxUse
    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    assert our_user.get_attr_val_utf8('pwdTPRUseCount') == str(try_tpr_failure)
    log.info("last failing bind (INVALID_CREDENTIALS) => pwdTPRUseCount = %d" % (try_tpr_failure))

    # Bind as user with valid password
    our_user.rebind(PASSWORD)
    time.sleep(.5)

    # We can not do anything else that reset password
    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        user = users.get(TEST_USER_NAME)

    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is FALSE
    assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    assert our_user.get_attr_val_utf8('pwdTPRUseCount') == str(try_tpr_failure + 1)

    # Now reset the password and check we can do fully use the account
    our_user.rebind(PASSWORD)
    our_user.reset_password(TEST_USER_PWD)
    # give time to update the pwp attributes in the entry
    time.sleep(.5)
    our_user.rebind(TEST_USER_PWD)
    time.sleep(.5)
    user = users.get(TEST_USER_NAME)


    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # Reset user's password
        our_user.replace('userpassword', TEST_USER_PWD)

    request.addfinalizer(fin)

def test_global_tpr_maxuse_4(topology_st, test_user, request):
    """Test global TPR policy : passwordTPRMaxUse
    Test that a TPR attribute passwordTPRMaxUse
    can be updated by DM but not the by user itself

    :id: ee698277-9c4e-4f58-8f57-158a6d966fe6
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Set passwordTPRMaxUse=5
        3. Set passwordMaxFailure to a higher value to not disturb the test
        4. Create a user without specific rights to update passwordTPRMaxUse
        5. Reset user password
        6. Do 3 failing (bad password) user authentication -> INVALID_CREDENTIALS
        7. Check that pwdTPRUseCount==3
        8. Bind as user and reset its password
        9. Check that user can not update pwdTPRUseCount => INSUFFICIENT_ACCESS
        10. Check that DM can update pwdTPRUseCount
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. INVALID_CREDENTIALS
        7. Success
        8. Success
        9. INSUFFICIENT_ACCESS
        10. Success
    """

    try_tpr_failure = 5
    USER_NO_ACI_NAME = 'user_no_aci'
    USER_NO_ACI_DN = 'uid={},{}'.format(USER_NO_ACI_NAME, OU_PEOPLE)
    USER_NO_ACI_PWD = 'user_no_aci'
    # Set password policy config, passwordMaxFailure being higher than
    # passwordTPRMaxUse so that TPR is enforced first
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordMaxFailure', str(try_tpr_failure + 20))
    topology_st.standalone.config.replace('passwordTPRMaxUse', str(try_tpr_failure))
    time.sleep(.5)

    # create user account (without aci granting write rights)
    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': USER_NO_ACI_NAME, 'userpassword': USER_NO_ACI_PWD})
    try:
        user = users.create(properties=user_props)
    except:
        pass  # debug only

    # Reset user's password
    user.replace('userpassword', PASSWORD)
    time.sleep(.5)

    # Do less than passwordTPRMaxUse failing bind
    try_tpr_failure = try_tpr_failure - 2
    for i in range(try_tpr_failure):
        # Bind as user with a wrong password
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            user.rebind('wrong password')
        time.sleep(.5)

        # Check that pwdReset is TRUE
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        #assert user.get_attr_val_utf8('pwdReset') == 'TRUE'

        # Check that pwdTPRReset is TRUE
        assert user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
        assert user.get_attr_val_utf8('pwdTPRUseCount') == str(i+1)
        log.info("%dth failing bind (INVALID_CREDENTIALS) => pwdTPRUseCount = %d" % (i+1, i+1))


    # Now the #failures has not reached passwordTPRMaxUse
    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    assert user.get_attr_val_utf8('pwdTPRUseCount') == str(try_tpr_failure)
    log.info("last failing bind (INVALID_CREDENTIALS) => pwdTPRUseCount = %d" % (try_tpr_failure))

    # Bind as user with valid password, reset the password
    # and do simple search
    user.rebind(PASSWORD)
    user.reset_password(USER_NO_ACI_PWD)
    time.sleep(.5)
    user.rebind(USER_NO_ACI_PWD)
    assert user.get_attr_val_utf8('uid')
    time.sleep(.5)

    # Fail to update pwdTPRUseCount being USER_NO_ACI
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace('pwdTPRUseCount', '100')
    assert user.get_attr_val_utf8('pwdTPRUseCount') != '100'

    # Succeeds to update pwdTPRUseCount being DM
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    user.replace('pwdTPRUseCount', '100')
    assert user.get_attr_val_utf8('pwdTPRUseCount') == '100'

    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # Reset user's password
        user.delete()

    request.addfinalizer(fin)

def test_local_tpr_maxuse_5(topology_st, test_user, request):
    """Test TPR local policy overpass global one: passwordTPRMaxUse
    Test that after passwordTPRMaxUse failures to bind
    additional bind with valid password are failing with CONSTRAINT_VIOLATION

    :id: c3919707-d804-445a-8754-8385b1072c42
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Global password policy Enable passwordMustChange
        2. Global password policy Set passwordTPRMaxUse=5
        3. Global password policy Set passwordMaxFailure to a higher value to not disturb the test
        4. Local password policy Enable passwordMustChange
        5. Local password policy Set passwordTPRMaxUse=10 (higher than global)
        6. Bind with a wrong password 10 times and check INVALID_CREDENTIALS
        7. Check that passwordTPRUseCount got to the limit (5)
        8. Bind with a wrong password (CONSTRAINT_VIOLATION)
           and check passwordTPRUseCount overpass the limit by 1 (11)
        9. Bind with a valid password 10 times and check CONSTRAINT_VIOLATION
           and check passwordTPRUseCount increases
        10. Reset password policy configuration and remove local password from user
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
    """

    global_tpr_maxuse = 5
    # Set global password policy config, passwordMaxFailure being higher than
    # passwordTPRMaxUse so that TPR is enforced first
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordMaxFailure', str(global_tpr_maxuse + 20))
    topology_st.standalone.config.replace('passwordTPRMaxUse', str(global_tpr_maxuse))
    time.sleep(.5)

    local_tpr_maxuse = global_tpr_maxuse + 5
    # Reset user's password with a local password policy
    # that has passwordTPRMaxUse higher than global
    #our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    subprocess.call(['%s/dsconf' % topology_st.standalone.get_sbin_dir(),
                     'slapd-standalone1',
                     'localpwp',
                     'adduser',
                     test_user.dn])
    subprocess.call(['%s/dsconf' % topology_st.standalone.get_sbin_dir(),
                     'slapd-standalone1',
                     'localpwp',
                     'set',
                     '--pwptprmaxuse',
                     str(local_tpr_maxuse),
                     '--pwdmustchange',
                     'on',
                     test_user.dn])
    test_user.replace('userpassword', PASSWORD)
    time.sleep(.5)

    # look up to passwordTPRMaxUse with failing
    # bind to check that the limits of TPR are enforced
    for i in range(local_tpr_maxuse):
        # Bind as user with a wrong password
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            test_user.rebind('wrong password')
        time.sleep(.5)

        # Check that pwdReset is TRUE
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        #assert test_user.get_attr_val_utf8('pwdReset') == 'TRUE'

        # Check that pwdTPRReset is TRUE
        assert test_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
        assert test_user.get_attr_val_utf8('pwdTPRUseCount') == str(i+1)
        log.info("%dth failing bind (INVALID_CREDENTIALS) => pwdTPRUseCount = %d" % (i+1, i+1))


    # Now the #failures reached passwordTPRMaxUse
    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # Check that pwdTPRReset is TRUE
    assert test_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    assert test_user.get_attr_val_utf8('pwdTPRUseCount') == str(local_tpr_maxuse)
    log.info("last failing bind (INVALID_CREDENTIALS) => pwdTPRUseCount = %d" % (local_tpr_maxuse))

    # Bind as user with wrong password --> ldap.CONSTRAINT_VIOLATION
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        test_user.rebind("wrong password")
    time.sleep(.5)

    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # Check that pwdTPRReset is TRUE
    assert test_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    assert test_user.get_attr_val_utf8('pwdTPRUseCount') == str(local_tpr_maxuse + 1)
    log.info("failing bind (CONSTRAINT_VIOLATION) => pwdTPRUseCount = %d" % (local_tpr_maxuse + i))

    # Now check that all next attempts with correct password are all in LDAP_CONSTRAINT_VIOLATION
    # and passwordTPRRetryCount remains unchanged
    # account is now similar to locked
    for i in range(10):
        # Bind as user with valid password
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            test_user.rebind(PASSWORD)
        time.sleep(.5)

        # Check that pwdReset is TRUE
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

        # Check that pwdTPRReset is TRUE
        # pwdTPRUseCount keeps increasing
        assert test_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
        assert test_user.get_attr_val_utf8('pwdTPRUseCount') == str(local_tpr_maxuse + i + 2)
        log.info("Rejected bind (CONSTRAINT_VIOLATION) => pwdTPRUseCount = %d" % (local_tpr_maxuse + i + 2))


    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # Remove local password policy from that entry
        subprocess.call(['%s/dsconf' % topology_st.standalone.get_sbin_dir(),
                        'slapd-standalone1',
                        'localpwp',
                        'remove',
                        test_user.dn])

        # Reset user's password
        test_user.replace('userpassword', TEST_USER_PWD)


    request.addfinalizer(fin)

def test_global_tpr_delayValidFrom_1(topology_st, test_user, request):
    """Test global TPR policy : passwordTPRDelayValidFrom
    Test that a TPR password is not valid before reset time +
    passwordTPRDelayValidFrom

    :id: 8420a348-e765-43ec-82c7-7f75cb4bf913
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Set passwordTPRDelayValidFrom=10s
        3. Create a account user
        5. Reset the password
        6. Check that Validity is not reached yet
           pwdTPRValidFrom >= now + passwordTPRDelayValidFrom - 2 (safety)
        7. Bind with valid password, Fails because of CONSTRAINT_VIOLATION
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    ValidFrom = 10
    # Set password policy config, passwordMaxFailure being higher than
    # passwordTPRMaxUse so that TPR is enforced first
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordTPRDelayValidFrom', str(ValidFrom))
    time.sleep(.5)

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    our_user.replace('userpassword', PASSWORD)
    # give time to update the pwp attributes in the entry
    time.sleep(.5)

    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    now = time.mktime(time.gmtime())
    log.info("compare pwdTPRValidFrom (%s) vs now (%s)" % (our_user.get_attr_val_utf8('pwdTPRValidFrom'), time.gmtime()))
    assert (gentime_to_posix_time(our_user.get_attr_val_utf8('pwdTPRValidFrom'))) >= (now + ValidFrom - 2)

    # Bind as user with valid password
    # But too early compare to ValidFrom
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        our_user.rebind(PASSWORD)

    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # Reset user's password
        our_user.replace('userpassword', TEST_USER_PWD)

    request.addfinalizer(fin)

def test_global_tpr_delayValidFrom_2(topology_st, test_user, request):
    """Test global TPR policy : passwordTPRDelayValidFrom
    Test that a TPR password is valid after reset time +
    passwordTPRDelayValidFrom

    :id: 8fa9f6f7-9be2-47c0-bf92-d9fe78ddbc34
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Set passwordTPRDelayValidFrom=6s
        3. Create a account user
        5. Reset the password
        6. Wait for passwordTPRDelayValidFrom=6s
        7. Bind with valid password, reset password
           to allow further searches
        8. Check bound user can search attribute ('uid')
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """

    ValidFrom = 6
    # Set password policy config, passwordMaxFailure being higher than
    # passwordTPRMaxUse so that TPR is enforced first
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordTPRDelayValidFrom', str(ValidFrom))
    time.sleep(.5)

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    our_user.replace('userpassword', PASSWORD)
    # give time to update the pwp attributes in the entry
    time.sleep(.5)

    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    now = time.mktime(time.gmtime())
    log.info("compare pwdTPRValidFrom (%s) vs now (%s)" % (our_user.get_attr_val_utf8('pwdTPRValidFrom'), time.gmtime()))
    assert (gentime_to_posix_time(our_user.get_attr_val_utf8('pwdTPRValidFrom'))) >= (now + ValidFrom - 2)

    # wait for pwdTPRValidFrom
    time.sleep(ValidFrom + 1)

    # Bind as user with valid password, reset the password
    # and do simple search
    our_user.rebind(PASSWORD)
    our_user.reset_password(TEST_USER_PWD)
    our_user.rebind(TEST_USER_PWD)
    assert our_user.get_attr_val_utf8('uid')

    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # Reset user's password
        our_user.replace('userpassword', TEST_USER_PWD)

    request.addfinalizer(fin)

def test_global_tpr_delayValidFrom_3(topology_st, test_user, request):
    """Test global TPR policy : passwordTPRDelayValidFrom
    Test that a TPR attribute passwordTPRDelayValidFrom
    can be updated by DM but not the by user itself

    :id: c599aea2-bbad-4158-b32e-307e5c6fca2d
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Set passwordTPRDelayValidFrom=6s
        3. Create a account user
        5. Reset the password
        6. Check pwdReset/pwdTPRReset/pwdTPRValidFrom
        7. wait for 6s to let the new TPR password being valid
        8. Bind with valid password, reset password
           to allow further searches
        9. Check bound user can search attribute ('uid')
        10. Bound as user, check user has not the rights to
            modify pwdTPRValidFrom
        11. Bound as DM, check user has the right to
            modify pwdTPRValidFrom

    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. ldap.INSUFFICIENT_ACCESS
        11. Success
    """

    ValidFrom = 6
    USER_NO_ACI_NAME = 'user_no_aci'
    USER_NO_ACI_DN = 'uid={},{}'.format(USER_NO_ACI_NAME, OU_PEOPLE)
    USER_NO_ACI_PWD = 'user_no_aci'
    # Set password policy config
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordTPRDelayValidFrom', str(ValidFrom))
    time.sleep(.5)

    # create user account (without aci granting write rights)
    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': USER_NO_ACI_NAME, 'userpassword': USER_NO_ACI_PWD})
    try:
        user = users.create(properties=user_props)
    except:
        pass  # debug only

    # Reset user's password
    #our_user = UserAccount(topology_st.standalone, USER_NO_ACI_DN)
    user.replace('userpassword', PASSWORD)
    # give time to update the pwp attributes in the entry
    time.sleep(.5)

    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    now = time.mktime(time.gmtime())
    log.info("compare pwdTPRValidFrom (%s) vs now (%s)" % (user.get_attr_val_utf8('pwdTPRValidFrom'), time.gmtime()))
    assert (gentime_to_posix_time(user.get_attr_val_utf8('pwdTPRValidFrom'))) >= (now + ValidFrom - 2)

    # wait for pwdTPRValidFrom
    time.sleep(ValidFrom + 1)

    # Bind as user with valid password, reset the password
    # and do simple search
    user.rebind(PASSWORD)
    user.reset_password(USER_NO_ACI_PWD)
    user.rebind(USER_NO_ACI_PWD)
    assert user.get_attr_val_utf8('uid')

    # Fail to update pwdTPRValidFrom being USER_NO_ACI
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace('pwdTPRValidFrom', '1234567890Z')
    assert user.get_attr_val_utf8('pwdTPRValidFrom') != '1234567890Z'

    # Succeeds to update pwdTPRValidFrom being DM
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    user.replace('pwdTPRValidFrom', '1234567890Z')
    assert user.get_attr_val_utf8('pwdTPRValidFrom') == '1234567890Z'

    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # delete the no aci entry
        user.delete()

    request.addfinalizer(fin)

def test_global_tpr_delayExpireAt_1(topology_st, test_user, request):
    """Test global TPR policy : passwordTPRDelayExpireAt
    Test that a TPR password is not valid after reset time +
    passwordTPRDelayExpireAt

    :id: b98def32-4e30-49fd-893b-8f959ba72b98
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Set passwordTPRDelayExpireAt=6s
        3. Create a account user
        5. Reset the password
        6. Wait for passwordTPRDelayExpireAt=6s + 2s (safety)
        7. Bind with valid password should fail with ldap.CONSTRAINT_VIOLATION
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    ExpireAt = 6
    # Set password policy config, passwordMaxFailure being higher than
    # passwordTPRMaxUse so that TPR is enforced first
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordTPRMaxUse', str(-1))
    topology_st.standalone.config.replace('passwordTPRDelayValidFrom', str(-1))
    topology_st.standalone.config.replace('passwordTPRDelayExpireAt', str(ExpireAt))
    time.sleep(.5)

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    our_user.replace('userpassword', PASSWORD)
    # give time to update the pwp attributes in the entry
    time.sleep(.5)

    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    now = time.mktime(time.gmtime())
    log.info("compare pwdTPRExpireAt (%s) vs now (%s)" % (our_user.get_attr_val_utf8('pwdTPRExpireAt'), time.gmtime()))
    assert (gentime_to_posix_time(our_user.get_attr_val_utf8('pwdTPRExpireAt'))) >= (now + ExpireAt - 2)

    # wait for pwdTPRExpireAt
    time.sleep(ExpireAt + 2)

    # Bind as user with valid password but too late
    # for pwdTPRExpireAt
    # and do simple search
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        our_user.rebind(PASSWORD)

    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # Reset user's password
        our_user.replace('userpassword', TEST_USER_PWD)

    request.addfinalizer(fin)

def test_global_tpr_delayExpireAt_2(topology_st, test_user, request):
    """Test global TPR policy : passwordTPRDelayExpireAt
    Test that a TPR password is valid before reset time +
    passwordTPRDelayExpireAt

    :id: 9df320de-ebf6-4ed0-a619-51b1a05a560c
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Set passwordTPRDelayExpireAt=6s
        3. Create a account user
        5. Reset the password
        6. Wait for 1s
        7. Bind with valid password should succeeds
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    ExpireAt = 6
    # Set password policy config, passwordMaxFailure being higher than
    # passwordTPRMaxUse so that TPR is enforced first
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordTPRMaxUse', str(-1))
    topology_st.standalone.config.replace('passwordTPRDelayValidFrom', str(-1))
    topology_st.standalone.config.replace('passwordTPRDelayExpireAt', str(ExpireAt))
    time.sleep(.5)

    # Reset user's password
    our_user = UserAccount(topology_st.standalone, TEST_USER_DN)
    our_user.replace('userpassword', PASSWORD)
    # give time to update the pwp attributes in the entry
    time.sleep(.5)

    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert our_user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert our_user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    now = time.mktime(time.gmtime())
    log.info("compare pwdTPRExpireAt (%s) vs now (%s)" % (our_user.get_attr_val_utf8('pwdTPRExpireAt'), time.gmtime()))
    assert (gentime_to_posix_time(our_user.get_attr_val_utf8('pwdTPRExpireAt'))) >= (now + ExpireAt - 2)

    # wait for 1s
    time.sleep(1)

    # Bind as user with valid password, reset the password
    # and do simple search
    our_user.rebind(PASSWORD)
    our_user.reset_password(TEST_USER_PWD)
    time.sleep(.5)
    our_user.rebind(TEST_USER_PWD)
    assert our_user.get_attr_val_utf8('uid')

    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # Reset user's password
        our_user.replace('userpassword', TEST_USER_PWD)

    request.addfinalizer(fin)

def test_global_tpr_delayExpireAt_3(topology_st, test_user, request):
    """Test global TPR policy : passwordTPRDelayExpireAt
    Test that a TPR attribute passwordTPRDelayExpireAt
    can be updated by DM but not the by user itself

    :id: 22bb5dd8-d8f6-4484-988e-6de0ef704391
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Enable passwordMustChange
        2. Set passwordTPRDelayExpireAt=6s
        3. Create a account user
        5. Reset the password
        6. Check pwdReset/pwdTPRReset/pwdTPRValidFrom
        7. wait for 1s so that TPR has not expired
        8. Bind with valid password, reset password
           to allow further searches
        9. Check bound user can search attribute ('uid')
        10. Bound as user, check user has not the rights to
            modify pwdTPRExpireAt
        11. Bound as DM, check user has the right to
            modify pwdTPRExpireAt

    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. ldap.INSUFFICIENT_ACCESS
        11. Success
    """

    ExpireAt = 6
    USER_NO_ACI_NAME = 'user_no_aci'
    USER_NO_ACI_DN = 'uid={},{}'.format(USER_NO_ACI_NAME, OU_PEOPLE)
    USER_NO_ACI_PWD = 'user_no_aci'
    # Set password policy config
    topology_st.standalone.config.replace('passwordMustChange', 'on')
    topology_st.standalone.config.replace('passwordTPRDelayValidFrom', str(-1))
    topology_st.standalone.config.replace('passwordTPRDelayExpireAt', str(ExpireAt))
    topology_st.standalone.config.replace('passwordTPRDelayValidFrom', str(-1))
    time.sleep(.5)

    # create user account (without aci granting write rights)
    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': USER_NO_ACI_NAME, 'userpassword': USER_NO_ACI_PWD})
    try:
        user = users.create(properties=user_props)
    except:
        pass  # debug only

    # Reset user's password
    user.replace('userpassword', PASSWORD)
    # give time to update the pwp attributes in the entry
    time.sleep(.5)

    # Check that pwdReset is TRUE
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert user.get_attr_val_utf8('pwdReset') == 'TRUE'

    # Check that pwdTPRReset is TRUE
    assert user.get_attr_val_utf8('pwdTPRReset') == 'TRUE'
    now = time.mktime(time.gmtime())
    log.info("compare pwdTPRExpireAt (%s) vs now (%s)" % (user.get_attr_val_utf8('pwdTPRExpireAt'), time.gmtime()))
    assert (gentime_to_posix_time(user.get_attr_val_utf8('pwdTPRExpireAt'))) >= (now + ExpireAt - 2)

    # wait for 1s
    time.sleep(1)

    # Bind as user with valid password, reset the password
    # and do simple search
    user.rebind(PASSWORD)
    user.reset_password(USER_NO_ACI_PWD)
    time.sleep(.5)
    user.rebind(USER_NO_ACI_PWD)
    assert user.get_attr_val_utf8('uid')
    time.sleep(.5)

    # Fail to update pwdTPRExpireAt being USER_NO_ACI
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        user.replace('pwdTPRExpireAt', '1234567890Z')
    assert user.get_attr_val_utf8('pwdTPRExpireAt') != '1234567890Z'

    # Succeeds to update pwdTPRExpireAt being DM
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    user.replace('pwdTPRExpireAt', '1234567890Z')
    assert user.get_attr_val_utf8('pwdTPRExpireAt') == '1234567890Z'

    def fin():
        topology_st.standalone.restart()
        # Reset password policy config
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology_st.standalone.config.replace('passwordMustChange', 'off')

        # delete the no aci entry
        user.delete()

    request.addfinalizer(fin)

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
