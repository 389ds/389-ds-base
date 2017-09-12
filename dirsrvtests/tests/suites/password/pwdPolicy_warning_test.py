# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import subprocess
from ldap.controls.ppolicy import PasswordPolicyControl
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import (DEFAULT_SUFFIX, DN_CONFIG, PASSWORD, DN_DM,
                              HOST_STANDALONE, PORT_STANDALONE, SERVERID_STANDALONE)
from dateutil.parser import parse as dt_parse
import datetime

CONFIG_ATTR = 'passwordSendExpiringTime'
USER_DN = 'uid=tuser,{:s}'.format(DEFAULT_SUFFIX)
USER_PASSWD = 'secret123'

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture
def global_policy(topology_st, request):
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
        entry = topology_st.standalone.getEntry(DN_CONFIG, ldap.SCOPE_BASE,
                                                '(objectClass=*)', attrs.keys())

        for key in attrs.keys():
            attrs[key] = entry.getValue(key)

        log.info('Set the new values')
        topology_st.standalone.modify_s(DN_CONFIG, [
            (ldap.MOD_REPLACE, 'passwordExp', 'on'),
            (ldap.MOD_REPLACE, 'passwordMaxAge', '172800'),
            (ldap.MOD_REPLACE, 'passwordWarning', '86400'),
            (ldap.MOD_REPLACE, CONFIG_ATTR, 'on')])

    except ldap.LDAPError as ex:
        log.error("Failed to set global password policy, error:{:s}" \
                  .format(ex.message['desc']))
        raise ex

    def fin():
        """Resets the defaults"""

        try:
            log.info('Reset the defaults')
            for key in attrs.keys():
                topology_st.standalone.modify_s(DN_CONFIG, [
                    (ldap.MOD_REPLACE, key, attrs[key])])
        except ldap.LDAPError as ex:
            log.error("Failed to set defaults, error:{:s}".format(ex.message['desc']))
            raise ex

    request.addfinalizer(fin)

    # A short sleep is required after the modifying password policy or cn=config
    time.sleep(0.5)


@pytest.fixture
def global_policy_default(topology_st, request):
    """Sets the required global password policy
    attributes for testing the default behavior
    of password expiry warning time
    """

    attrs = {'passwordExp': '',
             'passwordMaxAge': '',
             'passwordWarning': '',
             CONFIG_ATTR: ''}
    try:
        log.info('Get the default values')
        entry = topology_st.standalone.getEntry(DN_CONFIG, ldap.SCOPE_BASE,
                                                '(objectClass=*)', attrs.keys())
        for key in attrs.keys():
            attrs[key] = entry.getValue(key)

        log.info('Set the new values')
        topology_st.standalone.modify_s(DN_CONFIG, [
            (ldap.MOD_REPLACE, 'passwordExp', 'on'),
            (ldap.MOD_REPLACE, 'passwordMaxAge', '8640000'),
            (ldap.MOD_REPLACE, 'passwordWarning', '86400'),
            (ldap.MOD_REPLACE, CONFIG_ATTR, 'off')])
    except ldap.LDAPError as ex:
        log.error("Failed to set global password policy, error:{:s}" \
                  .format(ex.message['desc']))
        raise ex

    def fin():
        """Resets the defaults"""

        log.info('Reset the defaults')
        try:
            for key in attrs.keys():
                topology_st.standalone.modify_s(DN_CONFIG, [
                    (ldap.MOD_REPLACE, key, attrs[key])
                ])
        except ldap.LDAPError as ex:
            log.error("Failed to reset defaults, error:{:s}" \
                      .format(ex.message['desc']))
            raise ex

    request.addfinalizer(fin)

    # A short sleep is required after modifying password policy or cn=config
    time.sleep(0.5)


@pytest.fixture
def add_user(topology_st, request):
    """Adds a user for binding"""

    user_data = {'objectClass': 'top person inetOrgPerson'.split(),
                 'uid': 'tuser',
                 'cn': 'test user',
                 'sn': 'user',
                 'userPassword': USER_PASSWD}

    log.info('Add the user')
    try:
        topology_st.standalone.add_s(Entry((USER_DN, user_data)))
    except ldap.LDAPError as ex:
        log.error("Failed to add user, error:{:s}".format(ex.message['desc']))
        raise ex

    def fin():
        """Removes the user entry"""

        log.info('Remove the user entry')
        try:
            topology_st.standalone.delete_s(USER_DN)
        except ldap.LDAPError as ex:
            log.error("Failed to remove user, error:{:s}" \
                      .format(ex.message['desc']))
            raise ex

    request.addfinalizer(fin)


@pytest.fixture
def local_policy(topology_st, add_user):
    """Sets fine grained policy for user entry"""

    log.info("Setting fine grained policy for user ({:s})".format(USER_DN))
    try:
        subprocess.call(['%s/ns-newpwpolicy.pl' % topology_st.standalone.get_sbin_dir(),
                         '-D', DN_DM,
                         '-w', PASSWORD, '-h', HOST_STANDALONE,
                         '-p', str(PORT_STANDALONE), '-U', USER_DN,
                         '-Z', SERVERID_STANDALONE])
    except subprocess.CalledProcessError as ex:
        log.error("Failed to set fine grained policy, error:{:s}" \
                  .format(str(ex)))
        raise ex

    # A short sleep is required after modifying password policy
    time.sleep(0.5)


def get_password_warning(topology_st):
    """Gets the password expiry warning time for the user"""

    res_type = res_data = res_msgid = res_ctrls = None
    result_id = ''

    log.info('Bind with the user and request the password expiry warning time')
    try:
        result_id = topology_st.standalone.simple_bind(USER_DN, USER_PASSWD,
                                                       serverctrls=[PasswordPolicyControl()])
        res_type, res_data, res_msgid, res_ctrls = \
            topology_st.standalone.result3(result_id)

    # This exception will be thrown when the user's password has expired
    except ldap.INVALID_CREDENTIALS as ex:
        raise ex
    except ldap.LDAPError as ex:
        log.error("Failed to get password expiry warning time, error:{:s}" \
                  .format(ex.message['desc']))
        raise ex

    # Return the control
    return res_ctrls


def set_conf_attr(topology_st, attr, val):
    """Sets the value of a given attribute under cn=config"""

    log.info("Setting {:s} to {:s}".format(attr, val))
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, attr, val)])
    except ldap.LDAPError as ex:
        log.error("Failed to set {:s} to {:s} error:{:s}" \
                  .format(attr, val, ex.message['desc']))
        raise ex

    # A short sleep is required after modifying cn=config
    time.sleep(0.5)


def get_conf_attr(topology_st, attr):
    """Gets the value of a given
    attribute under cn=config entry
    """

    try:
        entry = topology_st.standalone.getEntry(DN_CONFIG, ldap.SCOPE_BASE,
                                                '(objectClass=*)', [attr])
        val = entry.getValue(attr)
    except ldap.LDAPError as ex:
        log.error("Failed to get the value of {:s}, error:{:s}" \
                  .format(attr, ex.message['desc']))
        raise ex

    # Return the value if no exeception is raised
    return val


@pytest.mark.parametrize("value", (' ', 'junk123', 'on', 'off'))
def test_different_values(topology_st, value):
    """Try to set passwordSendExpiringTime attribute
    to various values both valid and invalid

    :ID: 3e6d79fb-b4c8-4860-897e-5b207815a75d
    :feature: Password Expiry Warning Time
    :setup: Standalone DS instance
    :steps: 1. Try to set valid and invalid values
               for passwordSendExpiringTime attribute
               under cn=config entry
            2. Run the search command to check the
               value of passwordSendExpiringTime attribute
    :expectedresults: 1. Invalid values should be rejected with
                      an OPERATIONS_ERROR
                      2. Valid values should be accepted and saved
    """

    log.info('Get the default value')
    defval = get_conf_attr(topology_st, CONFIG_ATTR)

    if value not in ('on', 'off'):
        log.info('An invalid value is being tested')
        with pytest.raises(ldap.OPERATIONS_ERROR):
            set_conf_attr(topology_st, CONFIG_ATTR, value)

        log.info('Now check the value is unchanged')
        assert get_conf_attr(topology_st, CONFIG_ATTR) == defval

        log.info("Invalid value {:s} was rejected correctly".format(value))
    else:
        log.info('A valid value is being tested')
        set_conf_attr(topology_st, CONFIG_ATTR, value)

        log.info('Now check that the value has been changed')
        assert get_conf_attr(topology_st, CONFIG_ATTR) == value

        log.info("{:s} is now set to {:s}".format(CONFIG_ATTR, value))

        log.info('Set passwordSendExpiringTime back to the default value')
        set_conf_attr(topology_st, CONFIG_ATTR, defval)


def test_expiry_time(topology_st, global_policy, add_user):
    """Test whether the password expiry warning
    time for a user is returned appropriately

    :ID: 7adfd395-9b25-4cc0-9b71-14710dc1a28c
    :feature: Pasword Expiry Warning Time
    :setup: Standalone DS instance with,
            1. Global password policy configured as below:
               passwordExp: on
               passwordMaxAge: 172800
               passwordWarning: 86400
               passwordSendExpiringTime: on
            2. User entry for binding
    :steps: 1. Bind as the user
            2. Request the control for the user
    :expectedresults: The password expiry warning time for the user should be
                      returned
    """

    res_ctrls = None
    try:
        log.info('Get the password expiry warning time')
        log.info("Binding with ({:s}) and requesting the password expiry warning time" \
                 .format(USER_DN))
        res_ctrls = get_password_warning(topology_st)

        log.info('Check whether the time is returned')
        assert res_ctrls

        log.info("user's password will expire in {:d} seconds" \
                 .format(res_ctrls[0].timeBeforeExpiration))
    finally:
        log.info("Rebinding as DM")
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)


@pytest.mark.parametrize("attr,val", [(CONFIG_ATTR, 'off'),
                                      ('passwordWarning', '3600')])
def test_password_warning(topology_st, global_policy, add_user, attr, val):
    """Test password expiry warning time by
    setting passwordSendExpiringTime to off
    and setting passwordWarning to a short value

    :ID: 39f54b3c-8c80-43ca-856a-174d81c56ce8
    :feature: Password Expiry Warning Time
    :setup: Standalone DS instance with,
           1. Global password policy configures as below:
              passwordExp: on
              passwordMaxAge: 172800
              passwordWarning: 86400
              passwordSendExpiringTime: on
           2. User entry for binding
    :steps: 1a. Set passwordSendExpiringTime attribute to off.
            1b. In the another attempt, try to set passwordWarning
               to a small value (for eg: 3600 seconds)
            2. Bind as the user
            3a. Request the control for the user
            3b. Request the password expiry warning time
    :expectedresults: a. Password expiry warning time should not be returned
                      b. Password expiry warning time should be returned
    """

    try:
        log.info('Set configuration parameter')
        set_conf_attr(topology_st, attr, val)

        log.info("Binding with ({:s}) and requesting password expiry warning time" \
                 .format(USER_DN))
        res_ctrls = get_password_warning(topology_st)

        log.info('Check the state of the control')
        if not res_ctrls:
            log.info("Password Expiry warning time is not returned as {:s} is set to {:s}" \
                     .format(attr, val))
        else:
            log.info("({:s}) password will expire in {:d} seconds" \
                     .format(USER_DN, res_ctrls[0].timeBeforeExpiration))
    finally:
        log.info("Rebinding as DM")
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)


def test_with_different_password_states(topology_st, global_policy, add_user):
    """Test the control with different password states

    :ID: d297fb1a-661f-4d52-bb43-2a2a340b8b0e
    :feature: Password Expiry Warning Time
    :setup: Standalone DS instance with,
            1. Global password policy configured as below:
               passwordExp: on
               passwordMaxAge: 172800
               passwordWarning: 86400
               passwordSendExpiringTime: on
            2. User entry for binding to the server
    :steps: 1. Expire user's password by setting the system
               date past the valid period for the password
            2. Try to bind to the server with the user entry
            3. Set the system date to the current day
            4. Try to bind with the user entry and request
               the control
    :expectedresults:
            1. In the first try, the bind should fail with an
            INVALID_CREDENTIALS error
            2. In the second try, the bind should be successful
            and the password expiry warning time should be
            returned
    """

    res_ctrls = None

    log.info("Expiring user's password by changing" \
             "passwordExpirationTime timestamp")
    old_ts = topology_st.standalone.search_s(USER_DN, ldap.SCOPE_SUBTREE,
             '(objectClass=*)', ['passwordExpirationTime'])[0].getValue('passwordExpirationTime')
    log.info("Old passwordExpirationTime: {:s}".format(old_ts))
    new_ts = (dt_parse(old_ts) - datetime.timedelta(31)).strftime('%Y%m%d%H%M%SZ')
    log.info("New passwordExpirationTime: {:s}".format(new_ts))
    topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'passwordExpirationTime', new_ts)])

    try:
        log.info("Attempting to bind with user {:s} and retrive the password" \
                 " expiry warning time".format(USER_DN))
        with pytest.raises(ldap.INVALID_CREDENTIALS) as ex:
            res_ctrls = get_password_warning(topology_st)

        log.info("Bind Failed, error: {:s}".format(str(ex)))

    finally:
        log.info("Rebinding as DM")
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    log.info("Reverting back user's passwordExpirationTime")
    topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'passwordExpirationTime', old_ts)])

    try:
        log.info("Rebinding with {:s} and retrieving the password" \
                 " expiry warning time".format(USER_DN))
        res_ctrls = get_password_warning(topology_st)

        log.info('Check that the control is returned')
        assert res_ctrls

        log.info("user's password will expire in {:d} seconds" \
                 .format(res_ctrls[0].timeBeforeExpiration))
    finally:
        log.info("Rebinding as DM")
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)


def test_default_behavior(topology_st, global_policy_default, add_user):
    """Test the default behavior of password
    expiry warning time

    :ID: c47fa824-ee08-4b78-885f-bca4c42bb655
    :feature: Password Expiry Warning Time
    :setup: Standalone DS instance with,
            1. Global password policy configured as follows,
               passwordExp: on
               passwordMaxAge: 8640000
               passwordWarning: 86400
               passwordSendExpiringTime: off
            2. User entry for binding to the server
    :steps: 1. Bind as the user
            2. Request the control for the user
    :expectedresults:
            1. Bind should be successful
            2. No control should be returned
    """

    res_ctrls = None
    try:
        log.info("Binding with {:s} and requesting the password expiry warning time" \
                 .format(USER_DN))
        res_ctrls = get_password_warning(topology_st)

        log.info('Check that no control is returned')
        assert not res_ctrls

    finally:
        log.info("Rebinding as DM")
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)


def test_when_maxage_and_warning_are_the_same(topology_st, global_policy_default, add_user):
    """Test the warning expiry when passwordMaxAge and
    passwordWarning are set to the same value.

    :id: e57a1b1c-96fc-11e7-a91b-28d244694824
    :feature: Password Expiry Warning Time
    :setup: Standalone DS instance with,
            1. Global password policy configured as follows,
               passwordExp: on
               passwordMaxAge: 86400
               passwordWarning: 86400
               passwordSendExpiringTime: off
            2. User entry for binding to the server
    :steps: 1. Bind as the user
            2. Change user's password to reset its password expiration time
            3. Request the control for the user
    :expectedresults:
            1. Bind should be successful
            2. Password should be changed and password's expiration time reset
            3. Password expiry warning time should be returned by the
            server since passwordMaxAge and passwordWarning are set
            to the same value
    """

    log.info('Set the new values')
    topology_st.standalone.modify_s(DN_CONFIG, [
            (ldap.MOD_REPLACE, 'passwordMaxAge', '86400')])
    res_ctrls = None
    try:
        log.info("First change user's password to reset its password expiration time")
        topology_st.standalone.simple_bind_s(USER_DN, USER_PASSWD)

        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                        'userPassword', USER_PASSWD)])
        log.info("Binding with {:s} and requesting the password expiry warning time" \
                 .format(USER_DN))
        res_ctrls = get_password_warning(topology_st)

        log.info('Check that control is returned even'
                 'if passwordSendExpiringTime is set to off')
        assert res_ctrls

        log.info("user's password will expire in {:d} seconds" \
                 .format(res_ctrls[0].timeBeforeExpiration))
    finally:
        log.info("Rebinding as DM")
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)


def test_with_local_policy(topology_st, global_policy, local_policy):
    """Test the attribute with fine grained policy
    set for the user

    :ID: ab7d9f86-8cfe-48c3-8baa-739e599f006a
    :feature: Password Expiry Warning Time
    :setup: Standalone DS instance with,
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
    :steps: 1. Bind as the user
            2. Request the control for the user
    :expectedresults: Password expiry warning time should not be returned for the
                      user
    """

    res_ctrls = None
    try:
        log.info("Attempting to get password expiry warning time for" \
                 " user {:s}".format(USER_DN))
        res_ctrls = get_password_warning(topology_st)

        log.info('Check that the control is not returned')
        assert not res_ctrls

        log.info("Password expiry warning time is not returned")
    finally:
        log.info("Rebinding as DM")
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
