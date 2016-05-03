import os
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


class TopologyStandalone(object):
    """ Topology class """
    def __init__(self, standalone):
        """ init """
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    """
    Creating standalone instance ...
    """
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Delete each instance in the end
    def fin():
        """ Clean up instance """
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_pwp_history_test(topology):
    """
    Test password policy history feature:
        - Test password history is enforced
        - Test password history works after an Admin resets the password
        - Test that the correct number of passwords are stored in history
    """

    USER_DN = 'uid=testuser,' + DEFAULT_SUFFIX

    #
    # Configure password history policy and add a test user
    #
    try:
        topology.standalone.modify_s("cn=config",
                                     [(ldap.MOD_REPLACE,
                                       'passwordHistory', 'on'),
                                      (ldap.MOD_REPLACE,
                                       'passwordInHistory', '3'),
                                      (ldap.MOD_REPLACE,
                                       'passwordChange', 'on'),
                                      (ldap.MOD_REPLACE,
                                       'passwordStorageScheme', 'CLEAR')])
        log.info('Configured password policy.')
    except ldap.LDAPError as e:
        log.fatal('Failed to configure password policy: ' + str(e))
        assert False

    try:
        topology.standalone.add_s(Entry((USER_DN, {
                                  'objectclass': ['top', 'extensibleObject'],
                                  'sn': 'user',
                                  'cn': 'test user',
                                  'uid': 'testuser',
                                  'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add test user' + USER_DN + ': error ' + str(e))
        assert False

    #
    # Test that password history is enforced.
    #
    try:
        topology.standalone.simple_bind_s(USER_DN, 'password')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user: ' + str(e))
        assert False

    # Attempt to change password to the same password
    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                'userpassword', 'password')])
        log.info('Incorrectly able to to set password to existing password.')
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    #
    # Keep changing password until we fill the password history (3)
    #

    # password1
    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                'userpassword', 'password1')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change password: ' + str(e))
        assert False
    try:
        topology.standalone.simple_bind_s(USER_DN, 'password1')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user using "password1": ' + str(e))
        assert False

    # password2
    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                'userpassword', 'password2')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change password: ' + str(e))
        assert False
    try:
        topology.standalone.simple_bind_s(USER_DN, 'password2')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user using "password2": ' + str(e))
        assert False

    # password3
    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                'userpassword', 'password3')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change password: ' + str(e))
        assert False
    try:
        topology.standalone.simple_bind_s(USER_DN, 'password3')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user using "password3": ' + str(e))
        assert False

    # password4
    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                'userpassword', 'password4')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change password: ' + str(e))
        assert False
    try:
        topology.standalone.simple_bind_s(USER_DN, 'password4')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user using "password4": ' + str(e))
        assert False

    #
    # Check that we only have 3 passwords stored in history\
    #
    try:
        entry = topology.standalone.search_s(USER_DN, ldap.SCOPE_BASE,
                                             'objectclass=*',
                                             ['passwordHistory'])
        pwds = entry[0].getValues('passwordHistory')
        if len(pwds) != 3:
            log.fatal('Incorrect number of passwords stored in histry: %d' %
                      len(pwds))
            assert False
        else:
            log.info('Correct number of passwords found in history.')
    except ldap.LDAPError as e:
        log.fatal('Failed to get user entry: ' + str(e))
        assert False

    #
    # Attempt to change the password to previous passwords
    #
    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                'userpassword', 'password1')])
        log.info('Incorrectly able to to set password to previous password1.')
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                'userpassword', 'password2')])
        log.info('Incorrectly able to to set password to previous password2.')
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False
    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                'userpassword', 'password3')])
        log.info('Incorrectly able to to set password to previous password3.')
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    #
    # Reset password by Directory Manager(admin reset)
    #
    try:
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as rootDN: ' + str(e))
        assert False

    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                'userpassword',
                                                'password-reset')])
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to reset password: ' + str(e))
        assert False

    # Try and change the password to the previous password before the reset
    try:
        topology.standalone.simple_bind_s(USER_DN, 'password-reset')
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as user: ' + str(e))
        assert False

    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                'userpassword', 'password4')])
        log.info('Incorrectly able to to set password to previous password4.')
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Password change correctly rejected')
    except ldap.LDAPError as e:
        log.fatal('Failed to attempt to change password: ' + str(e))
        assert False

    log.info('Test suite PASSED.')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
