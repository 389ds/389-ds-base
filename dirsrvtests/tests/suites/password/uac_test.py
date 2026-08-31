# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import ldap
import datetime as dt
from lib389.tasks import *
from lib389.utils import *
from test389.topologies import topology_st
from lib389.idm.user import UserAccounts
from lib389._constants import (DEFAULT_SUFFIX, DN_CONFIG, PASSWORD, DN_DM)
from lib389._controls import AccountUsabilityControl

pytestmark = pytest.mark.tier1

CONFIG_ATTR = 'passwordSendExpiringTime'
USER_DN = 'uid=tuser,ou=people,{}'.format(DEFAULT_SUFFIX)
USER_RDN = 'tuser'
USER_PASSWD = 'secret123'
USER_ACI = '(targetattr="userpassword")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'
MAX_AGE = 172800
WARNING_DURATION = 86400

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture
def global_policy(topology_st):
    """Sets the required global
    password policy attributes under
    cn=config entry
    """

    inst = topology_st.standalone

    attrs = {'passwordExp': '',
             'passwordMaxAge': '',
             'passwordWarning': '',
             'passwordlockout': '',
             'passwordlockoutduration': '',
             'passwordresetfailurecount': '',
             'passwordmaxfailure': '',
             CONFIG_ATTR: ''}


    log.info('Get the default values')
    entry = inst.getEntry(DN_CONFIG, ldap.SCOPE_BASE,
                          '(objectClass=*)', attrs.keys())

    for key in attrs.keys():
        attrs[key] = entry.getValue(key)

    log.info('Set the new values')
    inst.config.replace_many(('passwordExp', 'on'),
                             ('passwordMaxAge', str(MAX_AGE)),
                             ('passwordWarning', str(WARNING_DURATION)),
                             ('passwordlockout', 'on'),
                             ('passwordlockoutduration', '3600'),
                             ('passwordresetfailurecount', '3600'),
                             ('passwordmaxfailure', '3'),
                             (CONFIG_ATTR, 'on'))

    yield
    log.info('Reset the defaults')
    inst.simple_bind_s(DN_DM, PASSWORD)
    for key in attrs.keys():
        inst.config.replace(key, attrs[key])

    # A short sleep is required after the modifying password policy or cn=config
    time.sleep(0.5)


@pytest.fixture
def add_user(topology_st, request):
    """Adds a user for binding"""

    log.info('Add the user')

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': USER_RDN,
        'cn': USER_RDN,
        'sn': USER_RDN,
        'uidNumber': '3000',
        'gidNumber': '4000',
        'homeDirectory': '/home/user',
        'description': 'd_e_s_c',
        'userPassword': USER_PASSWD
    })

    yield user

    log.info('Remove the user entry')
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    user.delete()


def read_uac(inst, user):
    """Perform a search with user account usability control"""
    attrlist = [ 'dn', 'passwordExpirationTime', 'accountUsability' ]
    auc = AccountUsabilityControl()
    resp_ctrl_classes = {}
    auc.registerClass(resp_ctrl_classes)
    msgid = inst.search_ext(user.dn, ldap.SCOPE_BASE, '(uid=*)', attrlist=attrlist,
                            serverctrls=[auc,], escapehatch='i am sure');
    resp = inst.result4(msgid, add_ctrls=1, resp_ctrl_classes=resp_ctrl_classes)
    rdata = resp[1]
    assert len(rdata) == 1
    dn, attrs, entry_ctrls = rdata[0]
    entry = { 'dn': dn }
    for attr,vals in attrs.items():
        assert len(vals) == 1
        entry[attr] = vals[0]
    rctrl = None
    for ctrl in entry_ctrls:
        if ctrl.controlType == auc.controlType:
            rctrl = auc.decodeResponseControl(ctrl.encodedControlValue)
    log.info(f'Entry: {entry} UAC: {rctrl}')
    return (entry, rctrl)




def test_uac_far_expiry(topology_st, global_policy, add_user):
    """Test account usability control with password expiration set two years
    in the future

    :id: 2d4f7987-dcc9-4195-bc80-1760c86950c2
    :setup: Standalone instance with password expiration policy enabled
    :steps:
        1. Set the user's passwordExpirationTime to now + 2 years
        2. Read the account usability control
    :expectedresults:
        1. The attribute is set successfully
        2. Account is usable with secondsBeforeExpiration close to 2 years
    """

    inst = topology_st.standalone
    user = add_user

    two_years_secs = 2 * 365 * 24 * 3600
    future = dt.datetime.utcnow() + dt.timedelta(seconds=two_years_secs)
    ts = future.strftime('%Y%m%d%H%M%SZ')

    log.info('Set passwordExpirationTime to %s (now + 2 years)', ts)
    user.replace('passwordExpirationTime', ts)

    entry, rctrl = read_uac(inst, user)
    assert rctrl is not None
    assert rctrl['usable'] is True
    assert abs(rctrl['secondsBeforeExpiration'] - two_years_secs) < 60


def test_uac_password_expired(topology_st, global_policy, add_user):
    """Test account usability control with an expired password

    :id: 4460b574-9bb7-4757-83df-dba94beb5b33
    :setup: Standalone instance with password expiration policy enabled
    :steps:
        1. Set the user's passwordExpirationTime to a past date
        2. Read the account usability control
    :expectedresults:
        1. The attribute is set successfully
        2. Account is not usable and passwordIsExpired is True
    """

    inst = topology_st.standalone
    user = add_user

    past = dt.datetime.utcnow() - dt.timedelta(days=1)
    ts = past.strftime('%Y%m%d%H%M%SZ')

    log.info('Set passwordExpirationTime to %s (yesterday)', ts)
    user.replace('passwordExpirationTime', ts)

    entry, rctrl = read_uac(inst, user)
    assert rctrl is not None
    assert rctrl['usable'] is False
    assert rctrl['passwordIsExpired'] is True


def test_uac_grace_logins(topology_st, global_policy, add_user):
    """Test account usability control reports remaining grace logins
    when password is expired and grace login limit is set

    :id: 226bae5e-3cff-4980-a742-2518dab095e9
    :setup: Standalone instance with password expiration policy enabled
    :steps:
        1. Set passwordGraceLimit to 5
        2. Set the user's passwordExpirationTime to a past date
        3. Read the account usability control
    :expectedresults:
        1. The config is set successfully
        2. The attribute is set successfully
        3. Account is not usable, passwordIsExpired is True,
           and remainingGraceLogins is 5
    """

    inst = topology_st.standalone
    user = add_user

    grace_limit = 5
    log.info('Set passwordGraceLimit to %d', grace_limit)
    inst.config.replace('passwordGraceLimit', str(grace_limit))

    try:
        past = dt.datetime.utcnow() - dt.timedelta(days=1)
        ts = past.strftime('%Y%m%d%H%M%SZ')

        log.info('Set passwordExpirationTime to %s (yesterday)', ts)
        user.replace('passwordExpirationTime', ts)

        entry, rctrl = read_uac(inst, user)
        assert rctrl is not None
        assert rctrl['usable'] is False
        assert rctrl['passwordIsExpired'] is True
        assert rctrl['remainingGraceLogins'] == grace_limit
    finally:
        inst.config.replace('passwordGraceLimit', '0')


def test_uac_inactive_account(topology_st, global_policy, add_user):
    """Test account usability control reports an inactive account

    :id: 70aa21d2-9d4d-4bb7-bdf9-c6e1b1c01d40
    :setup: Standalone instance with password expiration policy enabled
    :steps:
        1. Lock the user account by setting nsAccountLock to true
        2. Read the account usability control
    :expectedresults:
        1. The attribute is set successfully
        2. Account is not usable and accountIsInactive is True
    """

    inst = topology_st.standalone
    user = add_user

    log.info('Lock the user account')
    for _ in range(3):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            inst.simple_bind_s(user.dn, "BadPassword")
    # The accound is locked so the bind exception change
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        inst.simple_bind_s(user.dn, "BadPassword")
    # Rebind to directory mananger to read the entry
    inst.simple_bind_s(DN_DM, PASSWORD)

    entry, rctrl = read_uac(inst, user)
    assert rctrl is not None
    assert rctrl['usable'] is False
    assert rctrl['accountIsInactive'] is True


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
