# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import ldap
import time
from ldap.controls.ppolicy import PasswordPolicyControl
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts
from lib389._constants import (DN_DM, PASSWORD, DEFAULT_SUFFIX)
from lib389.idm.organizationalunit import OrganizationalUnits

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

USER_DN = 'uid=test entry,ou=people,dc=example,dc=com'
USER_PW = b'password123'
USER_ACI = '(targetattr="userpassword")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'


@pytest.fixture
def init_user(topo, request):
    """Initialize a user - Delete and re-add test user
    """
    try:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
        user = users.get('test entry')
        user.delete()
    except ldap.NO_SUCH_OBJECT:
        pass
    except ldap.LDAPError as e:
        log.error("Failed to delete user, error: {}".format(e.message['desc']))
        assert False

    user_data = {'uid': 'test entry',
                 'cn': 'test entry',
                 'sn': 'test entry',
                 'uidNumber': '3000',
                 'gidNumber': '4000',
                 'homeDirectory': '/home/test_entry',
                 'userPassword': USER_PW}
    users.create(properties=user_data)


def change_passwd(topo):
    """Reset users password as the user, then re-bind as Directory Manager
    """
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.get('test entry')
    user.rebind(USER_PW)
    user.reset_password(USER_PW)
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)


def bind_and_get_control(topo, err=0):
    """Bind as the user, and return any controls
    """
    res_type = res_data = res_msgid = res_ctrls = None
    result_id = ''

    try:
        result_id = topo.standalone.simple_bind(USER_DN, USER_PW,
                                                serverctrls=[PasswordPolicyControl()])
        res_type, res_data, res_msgid, res_ctrls = topo.standalone.result3(result_id)
        if err:
            log.fatal('Expected an error, but bind succeeded')
            assert False
    except ldap.LDAPError as e:
        if err:
            log.debug('Got expected error: {}'.format(str(e)))
            pass
        else:
            log.fatal('Did not expect an error: {}'.format(str(e)))
            assert False

    if DEBUGGING and res_ctrls and len(res_ctrls) > 0:
        for ctl in res_ctrls:
            if ctl.timeBeforeExpiration:
                log.debug('control time before expiration: {}'.format(ctl.timeBeforeExpiration))
            if ctl.graceAuthNsRemaining:
                log.debug('control grace login remaining: {}'.format(ctl.graceAuthNsRemaining))
            if ctl.error is not None and ctl.error >= 0:
                log.debug('control error: {}'.format(ctl.error))

    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    return res_ctrls


def test_pwd_must_change(topo, init_user):
    """Test for expiration control when password must be changed because an
    admin reset the password

    :id: a3d99be5-0b69-410d-b72f-04eda8821a56
    :setup: Standalone instance, a user for testing
    :steps:
        1. Configure password policy and reset password as admin
        2. Bind, and check for expired control withthe proper error code "2"
    :expectedresults:
        1. Config update succeeds, adn the password is reset
        2. The EXPIRED control is returned, and we the expected error code "2"
    """

    log.info('Configure password policy with paswordMustChange set to "on"')
    topo.standalone.config.set('passwordExp', 'on')
    topo.standalone.config.set('passwordMaxAge', '200')
    topo.standalone.config.set('passwordGraceLimit', '0')
    topo.standalone.config.set('passwordWarning', '199')
    topo.standalone.config.set('passwordMustChange', 'on')

    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ou = ous.get('people')
    ou.add('aci', USER_ACI)

    log.info('Reset userpassword as Directory Manager')
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.get('test entry')
    user.reset_password(USER_PW)

    log.info('Bind should return ctrl with error code 2 (changeAfterReset)')
    time.sleep(2)
    ctrls = bind_and_get_control(topo)
    if ctrls and len(ctrls) > 0:
        if ctrls[0].error is None:
            log.fatal("Response ctrl error code not set")
            assert False
        elif ctrls[0].error != 2:
            log.fatal("Got unexpected error code: {}".format(ctrls[0].error))
            assert False
    else:
        log.fatal("We did not get a response ctrl")
        assert False


def test_pwd_expired_grace_limit(topo, init_user):
    """Test for expiration control when password is expired, but there are
    remaining grace logins

    :id: a3d99be5-0b69-410d-b72f-04eda8821a51
    :setup: Standalone instance, a user for testing
    :steps:
        1. Configure password policy and reset password,adn allow it to expire
        2. Bind, and check for expired control, and grace limit
        3. Bind again, consuming the last grace login, control should be returned
        4. Bind again, it should fail, and no control returned
    :expectedresults:
        1. Config update and password reset are successful
        2. The EXPIRED control is returned, and we get the expected number
           of grace logins in the control
        3. The response control has the expected value for grace logins
        4. The bind fails with error 49, and no contorl is returned
    """

    log.info('Configure password policy with grace limit set tot 2')
    topo.standalone.config.set('passwordExp', 'on')
    topo.standalone.config.set('passwordMaxAge', '5')
    topo.standalone.config.set('passwordGraceLimit', '2')

    log.info('Change password and wait for it to expire')
    change_passwd(topo)
    time.sleep(6)

    log.info('Bind and use up one grace login (only one left)')
    ctrls = bind_and_get_control(topo)
    if ctrls is None or len(ctrls) == 0:
        log.fatal('Did not get EXPIRED control in resposne')
        assert False
    else:
        if int(ctrls[0].graceAuthNsRemaining) != 1:
            log.fatal('Got unexpected value for grace logins: {}'.format(ctrls[0].graceAuthNsRemaining))
            assert False

    log.info('Use up last grace login, should get control')
    ctrls = bind_and_get_control(topo)
    if ctrls is None or len(ctrls) == 0:
        log.fatal('Did not get control in response')
        assert False

    log.info('No grace login available, bind should fail, and no control should be returned')
    ctrls = bind_and_get_control(topo, err=49)
    if ctrls and len(ctrls) > 0:
        log.fatal('Incorrectly got control in response')
        assert False


def test_pwd_expiring_with_warning(topo, init_user):
    """Test expiring control response before and after warning is sent

    :id: 3594431f-e681-4a04-8edb-33ad2d9dad5b
    :setup: Standalone instance, a user for testing
    :steps:
        1. Configure password policy, and reset password
        2. Check for EXPIRING control, and the "time to expire"
        3. Bind again, as a warning has now been sent, and check the "time to expire"
    :expectedresults:
        1. Configuration update and password reset are successful
        2. Get the EXPIRING control, and the expected "time to expire" values
        3. Get the EXPIRING control, and the expected "time to expire" values
    """

    log.info('Configure password policy')
    topo.standalone.config.set('passwordExp', 'on')
    topo.standalone.config.set('passwordMaxAge', '50')
    topo.standalone.config.set('passwordWarning', '50')

    log.info('Change password and get controls')
    change_passwd(topo)
    ctrls = bind_and_get_control(topo)
    if ctrls is None or len(ctrls) == 0:
        log.fatal('Did not get EXPIRING control in response')
        assert False

    if int(ctrls[0].timeBeforeExpiration) < 50:
        log.fatal('Got unexpected value for timeBeforeExpiration: {}'.format(ctrls[0].timeBeforeExpiration))
        assert False

    log.info('Warning has been sent, try the bind again, and recheck the expiring time')
    time.sleep(5)
    ctrls = bind_and_get_control(topo)
    if ctrls is None or len(ctrls) == 0:
        log.fatal('Did not get EXPIRING control in resposne')
        assert False

    if int(ctrls[0].timeBeforeExpiration) > 50:
        log.fatal('Got unexpected value for timeBeforeExpiration: {}'.format(ctrls[0].timeBeforeExpiration))
        assert False


def test_pwd_expiring_with_no_warning(topo, init_user):
    """Test expiring control response when no warning is sent

    :id: a3d99be5-0b69-410d-b72f-04eda8821a54
    :setup: Standalone instance, a user for testing
    :steps:
        1. Configure password policy, and reset password
        2. Bind, and check that no controls are returned
        3. Set passwordSendExpiringTime to "on", bind, and check that the
           EXPIRING control is returned
    :expectedresults:
        1. Configuration update and passwordreset are successful
        2. No control is returned from bind
        3. A control is returned after setting "passwordSendExpiringTime"
    """

    log.info('Configure password policy')
    topo.standalone.config.set('passwordExp', 'on')
    topo.standalone.config.set('passwordMaxAge', '50')
    topo.standalone.config.set('passwordWarning', '5')

    log.info('When the warning is less than the max age, we never send expiring control response')
    change_passwd(topo)
    ctrls = bind_and_get_control(topo)
    if len(ctrls) > 0:
        log.fatal('Incorrectly got a response control: {}'.format(ctrls))
        assert False

    log.info('Turn on sending expiring control regardless of warning')
    topo.standalone.config.set('passwordSendExpiringTime', 'on')

    ctrls = bind_and_get_control(topo)
    if ctrls is None or len(ctrls) == 0:
        log.fatal('Did not get EXPIRED control in response')
        assert False

    if int(ctrls[0].timeBeforeExpiration) < 49:
        log.fatal('Got unexpected value for time before expiration: {}'.format(ctrls[0].timeBeforeExpiration))
        assert False

    log.info('Check expiring time again')
    time.sleep(6)
    ctrls = bind_and_get_control(topo)
    if ctrls is None or len(ctrls) == 0:
        log.fatal('Did not get EXPIRED control in resposne')
        assert False

    if int(ctrls[0].timeBeforeExpiration) > 51:
        log.fatal('Got unexpected value for time before expiration: {}'.format(ctrls[0].timeBeforeExpiration))
        assert False

    log.info('Turn off sending expiring control (restore the default setting)')
    topo.standalone.config.set('passwordSendExpiringTime', 'off')

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
