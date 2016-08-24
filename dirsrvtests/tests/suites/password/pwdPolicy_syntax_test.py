# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

DEBUGGING = False

USER_DN = 'uid=user,ou=People,%s' % DEFAULT_SUFFIX

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)


log = logging.getLogger(__name__)


class TopologyStandalone(object):
    """The DS Topology Class"""
    def __init__(self, standalone):
        """Init"""
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    """Create DS Deployment"""

    # Creating standalone instance ...
    if DEBUGGING:
        standalone = DirSrv(verbose=True)
    else:
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

    def fin():
        """If we are debugging just stop the instances, otherwise remove
        them
        """
        if DEBUGGING:
            standalone.stop()
        else:
            standalone.delete()

    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def _create_user(inst):
    """Create the test user."""
    inst.add_s(Entry((
                USER_DN, {
                    'objectClass': 'top account simplesecurityobject'.split(),
                     'uid': 'user',
                     'userpassword': PASSWORD
                })))


def setPolicy(inst, attr, value):
    """Bind as ROot DN, set polcy, and then bind as user"""
    try:
        inst.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal("Failed to bind as Directory Manager: " + str(e))
        assert False

    value = str(value)
    """
    if value == '0':
        # Remove the policy attribute
        try:
            inst.modify_s("cn=config",
                [(ldap.MOD_DELETE, attr, None)])
        except ldap.LDAPError as e:
            log.fatal("Failed to rmeove password policy %s: %s" %
                      (attr, str(e)))
            assert False
    else:
    """
    # Set the policy value
    inst.config.set(attr, value)

    try:
        inst.simple_bind_s(USER_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal("Failed to bind: " + str(e))
        assert False


def resetPasswd(inst):
    """Reset the user password for the next test"""

    # First, bind as the ROOT DN so we can set the password
    try:
        inst.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal("Failed to bind as Directory Manager: " + str(e))
        assert False

    # Now set the password
    try:
        inst.modify_s(USER_DN,
            [(ldap.MOD_REPLACE, 'userpassword', PASSWORD)])
    except ldap.LDAPError as e:
        log.fatal("Failed to reset user password: " + str(e))
        assert False


def tryPassword(inst, policy_attr, value, reset_value, pw_bad, pw_good, msg):
    """Attempt to change the users password
    inst: DirSrv Object
    password: password
    msg - error message if failure
    """

    setPolicy(inst, policy_attr, value)
    try:
        inst.modify_s(USER_DN,
            [(ldap.MOD_REPLACE, 'userpassword', pw_bad)])
        log.fatal('Invalid password was unexpectedly accepted (%s)' %
                  (policy_attr))
        assert False
    except ldap.CONSTRAINT_VIOLATION:
        log.info('Invalid password correctly rejected by %s:  %s' %
                 (policy_attr, msg))
        pass
    except ldap.LDAPError as e:
        log.fatal("Failed to change password: " + str(e))
        assert False

    # Change password that is allowed
    try:
        inst.modify_s(USER_DN,
            [(ldap.MOD_REPLACE, 'userpassword', pw_good)])
    except ldap.LDAPError as e:
        log.fatal("Failed to change password: " + str(e))
        assert False

    # Reset for the next test
    resetPasswd(inst)
    setPolicy(inst, policy_attr, reset_value)


def test_pwdPolicy_syntax(topology):
    '''
    Password policy test: Ensure that on a password change, the policy syntax
    is enforced correctly.
    '''

    # Create a user
    _create_user(topology.standalone)

    # Set the password policy globally
    topology.standalone.config.set('passwordCheckSyntax', 'on')
    topology.standalone.config.set('nsslapd-pwpolicy-local', 'off')
    topology.standalone.config.set('passwordMinCategories', '1')

    #
    # Test each syntax catagory
    #

    # Min Length
    tryPassword(topology.standalone, 'passwordMinLength', 10, 2, 'passwd',
                'password123', 'length too short')
    # Min Digit
    tryPassword(topology.standalone, 'passwordMinDigits', 2, 0, 'passwd',
                'password123', 'does not contain minimum number of digits')
    # Min Alphas
    tryPassword(topology.standalone, 'passwordMinAlphas', 2, 0, 'p123456789',
                'password123', 'does not contain minimum number of alphas')
    # Max Repeats
    tryPassword(topology.standalone, 'passwordMaxRepeats', 2, 0, 'passsword',
                'pasword123', 'too many repeating characters')
    # Min Specials
    tryPassword(topology.standalone, 'passwordMinSpecials', 2, 0, 'passwd',
                'password_#$',
                'does not contain minimum number of special characters')
    # Min Lowers
    tryPassword(topology.standalone, 'passwordMinLowers', 2, 0, 'PASSWORD123',
                'password123',
                'does not contain minimum number of lowercase characters')
    # Min Uppers
    tryPassword(topology.standalone, 'passwordMinUppers', 2, 0, 'password',
                'PASSWORD',
                'does not contain minimum number of lowercase characters')
    # Min 8-bits - "ldap" package only accepts ascii strings at the moment

    log.info('pwdPolicy tests PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
