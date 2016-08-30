import os
import sys
import time
import ldap
import logging
import pytest
import subprocess
from lib389 import DirSrv, Entry, tools, tasks
from ldap.controls.ppolicy import PasswordPolicyControl
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

DEBUGGING = False
CONFIG_ATTR = 'passwordSendExpiringTime'
USER_DN = 'uid=tuser,{:s}'.format(DEFAULT_SUFFIX)
USER_PASSWD = 'secret123'

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

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


@pytest.fixture
def global_policy(topology, request):
    """Sets the required global
    password policy attributes under
    cn=config entry
    """

    attrs = {'passwordExp': '',
             'passwordMaxAge': '',
             'passwordWarning': '',
             CONFIG_ATTR: ''}
    try:
        log.info('Get the default values')
        entry = topology.standalone.getEntry(DN_CONFIG, ldap.SCOPE_BASE,
                                             '(objectClass=*)', attrs.keys())

        for key in attrs.keys():
            attrs[key] = entry.getValue(key)

        log.info('Set the new values')
        topology.standalone.modify_s(DN_CONFIG, [
                                     (ldap.MOD_REPLACE, 'passwordExp', 'on'),
                                     (ldap.MOD_REPLACE, 'passwordMaxAge', '172800'),
                                     (ldap.MOD_REPLACE, 'passwordWarning', '86400'),
                                     (ldap.MOD_REPLACE, CONFIG_ATTR, 'on')])

    except ldap.LDAPError as ex:
        log.error("Failed to set global password policy, error:{:s}"\
                  .format(ex.message['desc']))
        raise ex

    def fin():
        """Resets the defaults"""

        try:
            log.info('Reset the defaults')
            for key in attrs.keys():
                topology.standalone.modify_s(DN_CONFIG, [
                    (ldap.MOD_REPLACE, key, attrs[key])])
        except ldap.LDAPError as ex:
            log.error("Failed to set defaults, error:{:s}".format(ex.message['desc']))
            raise ex
    request.addfinalizer(fin)

    # A short sleep is required after the modifying password policy or cn=config
    time.sleep(0.5)


@pytest.fixture
def global_policy_default(topology, request):
    """Sets the required global password policy
    attributes for testing the default behavior
    of password expiry warning time
    """

    attrs = {'passwordExp': '',
             'passwordMaxAge': '',
             'passwordWarning': '',
             CONFIG_ATTR : ''}
    try:
        log.info('Get the default values')
        entry = topology.standalone.getEntry(DN_CONFIG, ldap.SCOPE_BASE,
                                    '(objectClass=*)', attrs.keys())
        for key in attrs.keys():
            attrs[key] = entry.getValue(key)

        log.info('Set the new values')
        topology.standalone.modify_s(DN_CONFIG, [
                                    (ldap.MOD_REPLACE, 'passwordExp', 'on'),
                                    (ldap.MOD_REPLACE, 'passwordMaxAge', '86400'),
                                    (ldap.MOD_REPLACE, 'passwordWarning', '86400'),
                                    (ldap.MOD_REPLACE, CONFIG_ATTR, 'off')])
    except ldap.LDAPError as ex:
        log.error("Failed to set global password policy, error:{:s}"\
                  .format(ex.message['desc']))
        raise ex

    def fin():
        """Resets the defaults"""

        log.info('Reset the defaults')
        try:
            for key in attrs.keys():
                topology.standalone.modify_s(DN_CONFIG, [
                                             (ldap.MOD_REPLACE, key, attrs[key])
                                             ])
        except ldap.LDAPError as ex:
            log.error("Failed to reset defaults, error:{:s}"\
                      .format(ex.message['desc']))
            raise ex
    request.addfinalizer(fin)

    # A short sleep is required after modifying password policy or cn=config
    time.sleep(0.5)


@pytest.fixture
def add_user(topology, request):
    """Adds a user for binding"""

    user_data = {'objectClass': 'top person inetOrgPerson'.split(),
                 'uid': 'tuser',
                 'cn': 'test user',
                 'sn': 'user',
                 'userPassword': USER_PASSWD}

    log.info('Add the user')
    try:
        topology.standalone.add_s(Entry((USER_DN, user_data)))
    except ldap.LDAPError as ex:
        log.error("Failed to add user, error:{:s}".format(ex.message['desc']))
        raise ex

    def fin():
        """Removes the user entry"""

        log.info('Remove the user entry')
        try:
            topology.standalone.delete_s(USER_DN)
        except ldap.LDAPError as ex:
            log.error("Failed to remove user, error:{:s}"\
                      .format(ex.message['desc']))
            raise ex
    request.addfinalizer(fin)


@pytest.fixture
def local_policy(topology, add_user):
    """Sets fine grained policy for user entry"""

    log.info("Setting fine grained policy for user ({:s})".format(USER_DN))
    try:
        subprocess.call(['/usr/sbin/ns-newpwpolicy.pl', '-D', DN_DM,
                         '-w', PASSWORD, '-h', HOST_STANDALONE,
                         '-p', str(PORT_STANDALONE), '-U', USER_DN,
                         '-Z', SERVERID_STANDALONE])
    except subprocess.CalledProcessError as ex:
        log.error("Failed to set fine grained policy, error:{:s}"\
                  .format(str(ex)))
        raise ex

    # A short sleep is required after modifying password policy
    time.sleep(0.5)


def get_password_warning(topology):
    """Gets the password expiry warning time for the user"""

    res_type = res_data = res_msgid = res_ctrls = None
    result_id = ''

    log.info('Bind with the user and request the password expiry warning time')
    try:
        result_id = topology.standalone.simple_bind(USER_DN, USER_PASSWD,
                                        serverctrls = [PasswordPolicyControl()])
        res_type, res_data, res_msgid, res_ctrls =\
                                        topology.standalone.result3(result_id)

    # This exception will be thrown when the user's password has expired
    except ldap.INVALID_CREDENTIALS as ex:
        raise ex
    except ldap.LDAPError as ex:
        log.error("Failed to get password expiry warning time, error:{:s}"\
                      .format(ex.message['desc']))
        raise ex

    # Return the control
    return res_ctrls


def set_conf_attr(topology, attr, val):
    """Sets the value of a given attribute under cn=config"""

    log.info("Setting {:s} to {:s}".format(attr, val))
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, attr, val)])
    except ldap.LDAPError as ex:
        log.error("Failed to set {:s} to {:s} error:{:s}"\
                  .format(attr, val, ex.message['desc']))
        raise ex

    # A short sleep is required after modifying cn=config
    time.sleep(0.5)


def get_conf_attr(topology, attr):
    """Gets the value of a given
    attribute under cn=config entry
    """

    try:
        entry = topology.standalone.getEntry(DN_CONFIG, ldap.SCOPE_BASE,
                                             '(objectClass=*)', [attr])
        val = entry.getValue(attr)
    except ldap.LDAPError as ex:
        log.error("Failed to get the value of {:s}, error:{:s}"\
                  .format(attr, ex.message['desc']))
        raise ex

    # Return the value if no exeception is raised
    return val


@pytest.mark.parametrize("value", (' ' , 'junk123', 'on', 'off'))
def test_different_values(topology, value):
    """Try to set passwordSendExpiringTime attribute
    to various values both valid and invalid

    :Feature: Password Expiry Warning Time

    :Setup: Standalone DS instance

    :Steps: 1. Try to set valid and invalid values
               for passwordSendExpiringTime attribute
               under cn=config entry
            2. Run the search command to check the
               value of passwordSendExpiringTime attribute

    :Assert: 1. Invalid values should be rejected with
                an OPERATIONS_ERROR
             2. Valid values should be accepted and saved
    """

    log.info('Get the default value')
    defval  = get_conf_attr(topology, CONFIG_ATTR)

    if value not in ('on', 'off'):
        log.info('An invalid value is being tested')
        with pytest.raises(ldap.OPERATIONS_ERROR):
            set_conf_attr(topology, CONFIG_ATTR, value)

        log.info('Now check the value is unchanged')
        assert get_conf_attr(topology, CONFIG_ATTR) == defval

        log.info("Invalid value {:s} was rejected correctly".format(value))
    else:
        log.info('A valid value is being tested')
        set_conf_attr(topology, CONFIG_ATTR, value)

        log.info('Now check that the value has been changed')
        assert get_conf_attr(topology, CONFIG_ATTR) == value

        log.info("{:s} is now set to {:s}".format(CONFIG_ATTR, value))

        log.info('Set passwordSendExpiringTime back to the default value')
        set_conf_attr(topology, CONFIG_ATTR, defval)


def test_expiry_time(topology, global_policy, add_user):
    """Test whether the password expiry warning
    time for a user is returned appropriately

    :Feature: Pasword Expiry Warning Time

    :Setup: Standalone DS instance with,
            1. Global password policy configured as below:
               passwordExp: on
               passwordMaxAge: 172800
               passwordWarning: 86400
               passwordSendExpiringTime: on
            2. User entry for binding

    :Steps: 1. Bind as the user
            2. Request the control for the user

    :Assert: The password expiry warning time for the user should be
             returned
    """

    res_ctrls = None
    try:
        log.info('Get the password expiry warning time')
        log.info("Binding with ({:s}) and requesting the password expiry warning time"\
                 .format(USER_DN))
        res_ctrls = get_password_warning(topology)

        log.info('Check whether the time is returned')
        assert res_ctrls

        log.info("user's password will expire in {:d} seconds"\
                 .format(res_ctrls[0].timeBeforeExpiration))
    finally:
        log.info("Rebinding as DM")
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)


@pytest.mark.parametrize("attr,val",[(CONFIG_ATTR, 'off'),
                                     ('passwordWarning', '3600')])
def test_password_warning(topology, global_policy, add_user, attr, val):
    """Test password expiry warning time by
    setting passwordSendExpiringTime to off
    and setting passwordWarning to a short value

    :Feature: Password Expiry Warning Time

    :Setup: Standalone DS instance with,
           1. Global password policy configures as below:
              passwordExp: on
              passwordMaxAge: 172800
              passwordWarning: 86400
              passwordSendExpiringTime: on
           2. User entry for binding

    :Steps: 1. Set passwordSendExpiringTime attribute to off
            2. Bind as the user
            3. Request the control for the user

    :Assert: Password expiry warning time should not be returned

    :Steps: 1. Set passwordWarning to a small value
               (for eg: 3600 seconds)
            2. Bind with the user and request the password expiry warning
               time

    :Assert: Password expiry warning time should be returned
    """

    try:
        log.info('Set configuration parameter')
        set_conf_attr(topology, attr, val)

        log.info("Binding with ({:s}) and requesting password expiry warning time"\
                 .format(USER_DN))
        res_ctrls = get_password_warning(topology)

        log.info('Check the state of the control')
        if not res_ctrls:
            log.info("Password Expiry warning time is not returned as {:s} is set to {:s}"\
                     .format(attr, val))
        else:
            log.info("({:s}) password will expire in {:d} seconds"\
                     .format(USER_DN, res_ctrls[0].timeBeforeExpiration))
    finally:
        log.info("Rebinding as DM")
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)


def test_with_different_password_states(topology, global_policy, add_user):
    """Test the control with different password states

    :Feature: Password Expiry Warning Time

    :Setup: Standalone DS instance with,
            1. Global password policy configured as below:
               passwordExp: on
               passwordMaxAge: 172800
               passwordWarning: 86400
               passwordSendExpiringTime: on
            2. User entry for binding to the server

    :Steps: 1. Expire user's password by setting the system
               date past the valid period for the password
            2. Try to bind to the server with the user entry
            3. Set the system date to the current day
            4. Try to bind with the user entry and request
               the control

    :Assert: 1. In the first try, the bind should fail with an
                INVALID_CREDENTIALS error
             2. In the second try, the bind should be successful
                and the password expiry warning time should be
                returned
    """

    res_ctrls = None
    try:
        log.info("Expiring user's password by moving the"\
                 " system date past the valid period")
        subprocess.check_call(['/usr/bin/date', '-s', 'next month'])

        log.info('Wait for the server to pick up new date')
        time.sleep(5)

        log.info("Attempting to bind with user {:s} and retrive the password"\
                 " expiry warning time".format(USER_DN))
        with pytest.raises(ldap.INVALID_CREDENTIALS) as ex:
            res_ctrls = get_password_warning(topology)

        log.info("Bind Failed, error: {:s}".format(str(ex)))

        log.info("Resetting the system date")
        subprocess.check_call(['/usr/bin/date', '-s', 'last month'])

        log.info('Wait for the server to pick up new date')
        time.sleep(5)

        log.info("Rebinding with {:s} and retrieving the password"\
                 " expiry warning time".format(USER_DN))
        res_ctrls = get_password_warning(topology)

        log.info('Check that the control is returned')
        assert res_ctrls

        log.info("user's password will expire in {:d} seconds"\
                 .format(res_ctrls[0].timeBeforeExpiration))
    finally:
        log.info("Rebinding as DM")
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)


def test_default_behavior(topology, global_policy_default, add_user):
    """Test the default behavior of password
    expiry warning time

    :Feature: Password Expiry Warning Time

    :Setup: Standalone DS instance with,
            1. Global password policy configured as follows,
               passwordExp: on
               passwordMaxAge: 86400
               passwordWarning: 86400
               passwordSendExpiringTime: off
            2. User entry for binding to the server

    :Steps: 1. Bind as the user
            2. Request the control for the user

    :Assert: Password expiry warning time should be returned by the
             server by the server since passwordMaxAge and
             passwordWarning are set to the same value
    """

    res_ctrls = None
    try:
        log.info("Binding with {:s} and requesting the password expiry warning time"\
                 .format(USER_DN))
        res_ctrls = get_password_warning(topology)

        log.info('Check that control is returned even'
                 'if passwordSendExpiringTime is set to off')
        assert res_ctrls

        log.info("user's password will expire in {:d} seconds"\
                 .format(res_ctrls[0].timeBeforeExpiration))
    finally:
        log.info("Rebinding as DM")
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)


def test_with_local_policy(topology, global_policy, local_policy):
    """Test the attribute with fine grained policy
    set for the user

    :Feature: Password Expiry Warning Time

    :Setup: Standalone DS instance with,
            1. Global password policy configured as below,
               passwordExp: on
               passwordMaxAge: 172800
               passwordWarning: 86400
               passwordSendExpiringTime: on
            2. User entry for binding to the server
            3. Configure fine grained password policy for the user
               as below:
               ns-newpwpolicy.pl -D 'cn=Directory Manager' -w secret123
               -h localhost -p 389 -U 'uid=tuser,dc=example,dc=com'

    :Steps: 1. Bind as the user
            2. Request the control for the user

    :Assert: Password expiry warning time should not be returned for the
             user
    """

    res_ctrls = None
    try:
        log.info("Attempting to get password expiry warning time for"\
                 " user {:s}".format(USER_DN))
        res_ctrls = get_password_warning(topology)

        log.info('Check that the control is not returned')
        assert not res_ctrls

        log.info("Password expiry warning time is not returned")
    finally:
        log.info("Rebinding as DM")
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
