# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
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
import ast

from ldap.controls.ppolicy import PasswordPolicyControl
from ldap.controls.pwdpolicy import PasswordExpiredControl
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts
from lib389._constants import (DN_DM, PASSWORD, DEFAULT_SUFFIX)

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


USER_DN = 'uid=test entry,ou=people,dc=example,dc=com'
USER_PW = b'password123'


@pytest.fixture
def init_user(topo, request):
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user_data = {'uid': 'test entry',
                 'cn': 'test entry',
                 'sn': 'test entry',
                 'uidNumber': '3000',
                 'gidNumber': '4000',
                 'homeDirectory': '/home/test_entry',
                 'userPassword': USER_PW}
    test_user = users.create(properties=user_data)

    def fin():
        log.info('Delete test user')
        if test_user.exists():
            test_user.delete()

    request.addfinalizer(fin)


def bind_and_get_control(topo):
    log.info('Bind as the user, and return any controls')
    res_type = res_data = res_msgid = res_ctrls = None
    result_id = ''

    try:
        result_id = topo.standalone.simple_bind(USER_DN, USER_PW,
                                                serverctrls=[PasswordPolicyControl()])
        res_type, res_data, res_msgid, res_ctrls = topo.standalone.result3(result_id)
    except ldap.LDAPError as e:
        log.info('Got expected error: {}'.format(str(e)))
        res_ctrls = ast.literal_eval(str(e))
        pass

    topo.standalone.simple_bind(DN_DM, PASSWORD)
    return res_ctrls


def change_passwd(topo):
    log.info('Reset user password as the user, then re-bind as Directory Manager')
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.get('test entry')
    user.rebind(USER_PW)
    user.reset_password(USER_PW)
    topo.standalone.simple_bind(DN_DM, PASSWORD)


@pytest.mark.bz1724914
@pytest.mark.ds3585
def test_controltype_expired_grace_limit(topo, init_user):
    """Test for expiration control when password is expired with available and exhausted grace login

    :id: 0392a73c-6467-49f9-bdb6-3648f6971896
    :setup: Standalone instance, a user for testing
    :steps:
    	1. Configure password policy, reset password and allow it to expire
    	2. Bind and check sequence of controlType
    	3. Bind (one grace login remaining) and check sequence of controlType
    	4. Bind (grace login exhausted) and check sequence of controlType
    :expectedresults:
    	1. Config update and password reset are successful
    	2. ControlType sequence is in correct order
    	3. ControlType sequence is in correct order
    	4. ControlType sequence is in correct order
    """

    log.info('Configure password policy with grace limit set to 2')
    topo.standalone.config.set('passwordExp', 'on')
    topo.standalone.config.set('passwordMaxAge', '5')
    topo.standalone.config.set('passwordGraceLimit', '2')

    log.info('Change password and wait for it to expire')
    change_passwd(topo)
    time.sleep(6)

    log.info('Bind and use up one grace login (only one left)')
    controls = bind_and_get_control(topo)
    assert (controls[0].controlType == "1.3.6.1.4.1.42.2.27.8.5.1")
    assert (controls[1].controlType == "2.16.840.1.113730.3.4.4")

    log.info('Bind again and check the sequence')
    controls = bind_and_get_control(topo)
    assert (controls[0].controlType == "1.3.6.1.4.1.42.2.27.8.5.1")
    assert (controls[1].controlType == "2.16.840.1.113730.3.4.4")

    log.info('Bind with expired grace login and check the sequence')
    # No grace login available, bind should fail, controls will be returned in error message
    controls = bind_and_get_control(topo)
    assert (controls['ctrls'][0][0] == "1.3.6.1.4.1.42.2.27.8.5.1")
    assert (controls['ctrls'][1][0] == "2.16.840.1.113730.3.4.4")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
