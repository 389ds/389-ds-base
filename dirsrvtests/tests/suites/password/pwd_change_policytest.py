import os
import sys
import time
import subprocess
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

DEBUGGING = False
OU_PEOPLE = 'ou=people,{}'.format(DEFAULT_SUFFIX)
TEST_USER_NAME = 'simplepaged_test'
TEST_USER_DN = 'uid={},{}'.format(TEST_USER_NAME, OU_PEOPLE)
TEST_USER_PWD = 'simplepaged_test'
PW_POLICY_CONT_USER = 'cn="cn=nsPwPolicyEntry,uid=simplepaged_test,'\
                      'ou=people,dc=example,dc=com",'\
                      'cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'
PW_POLICY_CONT_PEOPLE = 'cn="cn=nsPwPolicyEntry,'\
                        'ou=people,dc=example,dc=com",'\
                        'cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'

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


@pytest.fixture(scope="module")
def test_user(topology, request):
    """User for binding operation"""

    log.info('Adding user {}'.format(TEST_USER_DN))
    try:
        topology.standalone.add_s(Entry((TEST_USER_DN, {
                                        'objectclass': 'top person'.split(),
                                        'objectclass': 'organizationalPerson',
                                        'objectclass': 'inetorgperson',
                                        'cn': TEST_USER_NAME,
                                        'sn': TEST_USER_NAME,
                                        'userpassword': TEST_USER_PWD,
                                        'mail': '%s@redhat.com' % TEST_USER_NAME,
                                        'uid': TEST_USER_NAME
                                        })))
    except ldap.LDAPError as e:
        log.error('Failed to add user (%s): error (%s)' % (TEST_USER_DN,
                                                           e.message['desc']))
        raise e

    def fin():
        log.info('Deleting user {}'.format(TEST_USER_DN))
        topology.standalone.delete_s(TEST_USER_DN)
    request.addfinalizer(fin)


@pytest.fixture(scope="module")
def password_policy(topology, test_user):
    """Set up password policy for subtree and user"""

    log.info('Enable fine-grained policy')
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                                  'nsslapd-pwpolicy-local',
                                                  'on')])
    except ldap.LDAPError as e:
        log.error('Failed to set fine-grained policy: error {}'.format(
            e.message['desc']))
        raise e

    log.info('Create password policy for subtree {}'.format(OU_PEOPLE))
    try:
        subprocess.call(['ns-newpwpolicy.pl', '-D', DN_DM, '-w', PASSWORD,
                         '-p', str(PORT_STANDALONE), '-h', HOST_STANDALONE,
                         '-S', OU_PEOPLE, '-Z', SERVERID_STANDALONE])
    except subprocess.CalledProcessError as e:
        log.error('Failed to create pw policy policy for {}: error {}'.format(
            OU_PEOPLE, e.message['desc']))
        raise e

    log.info('Add pwdpolicysubentry attribute to {}'.format(OU_PEOPLE))
    try:
        topology.standalone.modify_s(OU_PEOPLE, [(ldap.MOD_REPLACE,
                                                  'pwdpolicysubentry',
                                                  PW_POLICY_CONT_PEOPLE)])
    except ldap.LDAPError as e:
        log.error('Failed to pwdpolicysubentry pw policy '\
                  'policy for {}: error {}'.format(OU_PEOPLE,
                                                   e.message['desc']))
        raise e

    log.info('Create password policy for subtree {}'.format(TEST_USER_DN))
    try:
        subprocess.call(['ns-newpwpolicy.pl', '-D', DN_DM, '-w', PASSWORD,
                         '-p', str(PORT_STANDALONE), '-h', HOST_STANDALONE,
                         '-U', TEST_USER_DN, '-Z', SERVERID_STANDALONE])
    except subprocess.CalledProcessError as e:
        log.error('Failed to create pw policy policy for {}: error {}'.format(
            TEST_USER_DN, e.message['desc']))
        raise e

    log.info('Add pwdpolicysubentry attribute to {}'.format(TEST_USER_DN))
    try:
        topology.standalone.modify_s(TEST_USER_DN, [(ldap.MOD_REPLACE,
                                                     'pwdpolicysubentry',
                                                     PW_POLICY_CONT_USER)])
    except ldap.LDAPError as e:
        log.error('Failed to pwdpolicysubentry pw policy '\
                  'policy for {}: error {}'.format(TEST_USER_DN,
                                                   e.message['desc']))
        raise e


@pytest.mark.parametrize('subtree_pwchange,user_pwchange,exception',
                         [('off', 'on', None), ('on', 'on', None),
                          ('on', 'off', ldap.UNWILLING_TO_PERFORM),
                          ('off', 'off', ldap.UNWILLING_TO_PERFORM)])
def test_change_pwd(topology, test_user, password_policy,
                    subtree_pwchange, user_pwchange, exception):
    """Verify that 'passwordChange' attr works as expected
    User should have a priority over a subtree.

    :Feature: Password policy

    :Setup: Standalone instance, test user,
            password policy entries for a user and a subtree

    :Steps: 1. Set passwordChange on the user and the subtree
               to various combinations
            2. Bind as test user
            3. Try to change password

    :Assert: Subtree/User passwordChange - result
             off/on, on/on - success
             on/off, off/off - UNWILLING_TO_PERFORM
    """

    log.info('Set passwordChange to "{}" - {}'.format(subtree_pwchange,
                                                      PW_POLICY_CONT_PEOPLE))
    try:
        topology.standalone.modify_s(PW_POLICY_CONT_PEOPLE, [(ldap.MOD_REPLACE,
                                                             'passwordChange',
                                                             subtree_pwchange)])
    except ldap.LDAPError as e:
        log.error('Failed to set passwordChange '\
                  'policy for {}: error {}'.format(PW_POLICY_CONT_PEOPLE,
                                                   e.message['desc']))
        raise e


    log.info('Set passwordChange to "{}" - {}'.format(user_pwchange,
                                                      PW_POLICY_CONT_USER))
    try:
        topology.standalone.modify_s(PW_POLICY_CONT_USER, [(ldap.MOD_REPLACE,
                                                            'passwordChange',
                                                            user_pwchange)])
    except ldap.LDAPError as e:
        log.error('Failed to set passwordChange '\
                  'policy for {}: error {}'.format(PW_POLICY_CONT_USER,
                                                   e.message['desc']))
        raise e

    try:
        log.info('Bind as user and modify userPassword')
        topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)
        if exception:
            with pytest.raises(exception):
                topology.standalone.modify_s(TEST_USER_DN, [(ldap.MOD_REPLACE,
                                                            'userPassword',
                                                            'new_pass')])
        else:
            topology.standalone.modify_s(TEST_USER_DN, [(ldap.MOD_REPLACE,
                                                        'userPassword',
                                                        'new_pass')])
    except ldap.LDAPError as e:
        log.error('Failed to change userpassword for {}: error {}'.format(
            TEST_USER_DN, e.message['info']))
        raise e
    finally:
        log.info('Bind as DM')
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)
        topology.standalone.modify_s(TEST_USER_DN, [(ldap.MOD_REPLACE,
                                                     'userPassword',
                                                     TEST_USER_PWD)])


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
