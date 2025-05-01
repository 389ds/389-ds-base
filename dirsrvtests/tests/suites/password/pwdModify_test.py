# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import re
from ldap.controls import LDAPControl
from lib389._constants import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.pwpolicy import PwPolicyManager

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


OLD_PASSWD = 'password'
NEW_PASSWD = 'newpassword'
SHORT_PASSWD = 'wd'
TESTPEOPLE_OU = "TestPeople_bug834047"
USER_ACI = '(targetattr="userpassword")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'


@pytest.fixture(scope="function")
def pwd_policy_setup(topo, request):
    """
    Setup to set passwordStorageScheme as CLEAR
    passwordHistory to on
    passwordStorageScheme to SSHA
    passwordHistory off
    """
    log.info("Change the pwd storage type to clear and change the password once to refresh it(for the rest of tests")
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    topo.standalone.config.set('passwordStorageScheme', 'CLEAR')
    assert topo.standalone.passwd_s(user_2.dn, OLD_PASSWD, NEW_PASSWD)
    topo.standalone.config.set('passwordHistory', 'on')

    def fin():
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        topo.standalone.config.set('passwordStorageScheme', 'SSHA')
        topo.standalone.config.set('passwordHistory', 'off')
    request.addfinalizer(fin)


def test_pwd_modify_with_different_operation(topo):
    """Performing various password modify operation,
    make sure that password is actually modified

    :id: e36d68a8-0960-48e4-932c-6c2f64abaebc
    :setup: Standalone instance and TLS enabled
    :steps:
        1. Attempt for Password change for an entry that does not exists
        2. Attempt for Password change for an entry that exists
        3. Attempt for Password change to old for an entry that exists
        4. Attempt for Password Change with Binddn as testuser but with wrong old password
        5. Attempt for Password Change with Binddn as testuser
        6. Attempt for Password Change without giving newpassword
        7. Checking password change Operation using a Non-Secure connection
        8. Testuser attempts to change password for testuser2(userPassword attribute is Set)
        9. Directory Manager attempts to change password for testuser2(userPassword attribute is Set)
        10. Create a password syntax policy. Attempt to change to password that violates that policy
        11. userPassword mod with control results in ber decode error

    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. Operation should not be successful
        5. Operation should be successful
        6. Operation should be successful
        7. Operation should not be successful
        8. Operation should not be successful
        9. Operation should be successful
        10. Operation should violates the policy
        11. Operation should be successful
     """

    topo.standalone.enable_tls()
    os.environ["LDAPTLS_CACERTDIR"] = topo.standalone.get_ssca_dir()
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    TEST_USER_PROPERTIES['userpassword'] = OLD_PASSWD
    global user
    user = users.create(properties=TEST_USER_PROPERTIES)
    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ou = ous.get('people')
    ou.add('aci', USER_ACI)

    with pytest.raises(ldap.NO_SUCH_OBJECT):
        log.info("Attempt for Password change for an entry that does not exists")
        assert topo.standalone.passwd_s('uid=testuser1,ou=People,dc=example,dc=com', OLD_PASSWD, NEW_PASSWD)
    log.info("Attempt for Password change for an entry that exists")
    assert topo.standalone.passwd_s(user.dn, OLD_PASSWD, NEW_PASSWD)
    log.info("Attempt for Password change to old for an entry that exists")
    assert topo.standalone.passwd_s(user.dn, NEW_PASSWD, OLD_PASSWD)
    log.info("Attempt for Password Change with Binddn as testuser but with wrong old password")
    topo.standalone.simple_bind_s(user.dn, OLD_PASSWD)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        topo.standalone.passwd_s(user.dn, NEW_PASSWD, NEW_PASSWD)
    log.info("Attempt for Password Change with Binddn as testuser")
    assert topo.standalone.passwd_s(user.dn, OLD_PASSWD, NEW_PASSWD)
    log.info("Attempt for Password Change without giving newpassword")
    assert topo.standalone.passwd_s(user.dn, None, OLD_PASSWD)
    assert user.get_attr_val_utf8('uid') == 'testuser'
    log.info("Change password to NEW_PASSWD i.e newpassword")
    assert topo.standalone.passwd_s(user.dn, None, NEW_PASSWD)
    assert topo.standalone.passwd_s(user.dn, NEW_PASSWD, None)
    log.info("Check binding with old/new password")
    password = [OLD_PASSWD, NEW_PASSWD]
    for pass_val in password:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            topo.standalone.simple_bind_s(user.dn, pass_val)
    log.info("Change password back to OLD_PASSWD i.e password")
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert topo.standalone.passwd_s(user.dn, None, NEW_PASSWD)
    log.info("Checking password change Operation using a Non-Secure connection")
    conn = ldap.initialize("ldap://%s:%s" % (HOST_STANDALONE, PORT_STANDALONE))
    with pytest.raises(ldap.CONFIDENTIALITY_REQUIRED):
        conn.passwd_s(user.dn, NEW_PASSWD, OLD_PASSWD)
    log.info("Testuser attempts to change password for testuser2(userPassword attribute is Set)")
    global user_2
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user_2 = users.create(properties={
        'uid': 'testuser2',
        'cn': 'testuser2',
        'sn': 'testuser2',
        'uidNumber': '3000',
        'gidNumber': '4000',
        'homeDirectory': '/home/testuser2',
        'userPassword': OLD_PASSWD
    })

    topo.standalone.simple_bind_s(user.dn, NEW_PASSWD)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        assert topo.standalone.passwd_s(user_2.dn, OLD_PASSWD, NEW_PASSWD)
    log.info("Directory Manager attempts to change password for testuser2(userPassword attribute is Set)")
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    assert topo.standalone.passwd_s(user_2.dn, OLD_PASSWD, NEW_PASSWD)
    log.info("Changing userPassword attribute to Undefined for testuser2")
    topo.standalone.modify_s(user_2.dn, [(ldap.MOD_REPLACE, 'userPassword', None)])
    log.info("Testuser attempts to change password for testuser2(userPassword attribute is Undefined)")
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        topo.standalone.simple_bind_s(user.dn, NEW_PASSWD)
        assert topo.standalone.passwd_s(user_2.dn, None, NEW_PASSWD)

    log.info("Reset password syntax policy")
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    topo.standalone.config.set('PasswordCheckSyntax', 'off')
    log.info("userPassword mod with control results in ber decode error")
    assert topo.standalone.modify_ext_s(user.dn, [(ldap.MOD_REPLACE, 'userpassword', b'abcdefg')],
                                        serverctrls=[LDAPControl('2.16.840.1.113730.3.4.2', 1, None)])
    log.info("Reseting the testuser's password")
    topo.standalone.passwd_s(user.dn, 'abcdefg', OLD_PASSWD)
    topo.standalone.passwd_s(user_2.dn, None, OLD_PASSWD)


def test_pwd_modify_with_password_policy(topo, pwd_policy_setup):
    """Performing various password modify operation,
    with passwordStorageScheme as CLEAR
    passwordHistory to on

    :id: 200bf0fd-20ab-4dde-849e-54067e98b917
    :setup: Standalone instance (TLS enabled) with pwd_policy_setup
    :steps:
        1. Change the password and check that a new entry has been added to the history
        2. Try changing password to one stored in history
        3. Change the password several times in a row, and try binding after each change
        4. Try to bind using short password

    :expectedresults:
        1. Operation should be successful
        2. Operation should be unsuccessful
        3. Operation should be successful
        4. Operation should be unsuccessful
     """
    log.info("Change the password and check that a new entry has been added to the history")
    topo.standalone.passwd_s(user_2.dn, NEW_PASSWD, OLD_PASSWD)
    regex = re.search('Z(.+)', user_2.get_attr_val_utf8('passwordhistory'))
    assert NEW_PASSWD == regex.group(1)

    log.info("Try changing password to one stored in history.  Should fail")
    assert topo.standalone.simple_bind_s(user_2.dn, OLD_PASSWD)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        assert topo.standalone.passwd_s(user_2.dn, OLD_PASSWD, NEW_PASSWD)
    log.info("Change the password several times in a row, and try binding after each change")
    assert topo.standalone.simple_bind_s(user.dn, OLD_PASSWD)
    topo.standalone.passwd_s(user.dn, OLD_PASSWD, NEW_PASSWD)
    topo.standalone.passwd_s(user.dn, NEW_PASSWD, SHORT_PASSWD)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        topo.standalone.passwd_s(user.dn, SHORT_PASSWD, NEW_PASSWD)


def test_pwd_modify_with_subsuffix(topo):
    """Performing various password modify operation.

     :id: 2255b4e6-3546-4ec5-84a5-cd8b3d894ac5
     :setup: Standalone instance (TLS enabled)
     :steps:
         1. Add a new SubSuffix & password policy
         2. Add two New users under the SubEntry
         3. Change password of uid=test_user0,ou=TestPeople_bug834047,dc=example,dc=com to newpassword
         4. Try to delete password- case when password is specified
         5. Try to delete password- case when password is not specified

     :expectedresults:
         1. Operation should be successful
         2. Operation should be successful
         3. Operation should be successful
         4. Operation should be successful
         5. Operation should be successful
      """

    log.info("Add a new SubSuffix")
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ou_temp = ous.create(properties={'ou': TESTPEOPLE_OU})
    ou_temp.add('aci', USER_ACI)

    log.info("Add the container & create password policies")
    policy = PwPolicyManager(topo.standalone)
    policy.create_subtree_policy(ou_temp.dn, properties={
        'passwordHistory': 'on',
        'passwordInHistory': '6',
        'passwordChange': 'on',
        'passwordStorageScheme': 'CLEAR'})

    log.info("Add two New users under the SubEntry")
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=TestPeople_bug834047')
    test_user0 = user.create(properties={
        'uid': 'test_user0',
        'cn': 'test0',
        'sn': 'test0',
        'uidNumber': '3002',
        'gidNumber': '4002',
        'homeDirectory': '/home/test_user0',
        'userPassword': OLD_PASSWD
        })

    test_user1 = user.create(properties={
        'uid': 'test_user1',
        'cn': 'test1',
        'sn': 'test1',
        'uidNumber': '3003',
        'gidNumber': '4003',
        'homeDirectory': '/home/test_user3',
        'userPassword': OLD_PASSWD
        })

    log.info("Changing password of {} to newpassword".format(test_user0.dn))
    test_user0.rebind(OLD_PASSWD)
    test_user0.reset_password(NEW_PASSWD)
    test_user0.rebind(NEW_PASSWD)

    log.info("Try to delete password- case when password is specified")
    test_user0.remove('userPassword', NEW_PASSWD)

    test_user1.rebind(OLD_PASSWD)
    log.info("Try to delete password- case when password is not specified")
    test_user1.remove_all('userPassword')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
