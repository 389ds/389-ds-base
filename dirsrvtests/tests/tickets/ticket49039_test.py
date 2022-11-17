# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import ldap
import logging
import pytest
import os
from lib389 import Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st as topo
from lib389.pwpolicy import PwPolicyManager


pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

USER_DN = 'uid=user,dc=example,dc=com'


def test_ticket49039(topo):
    """Test "password must change" verses "password min age".  Min age should not
    block password update if the password was reset.
    """

    # Setup SSL (for ldappasswd test)
    topo.standalone.enable_tls()

    # Configure password policy
    try:
        policy = PwPolicyManager(topo.standalone)
        policy.set_global_policy(properties={'nsslapd-pwpolicy-local': 'on',
                                             'passwordMustChange': 'on',
                                             'passwordExp': 'on',
                                             'passwordMaxAge': '86400000',
                                             'passwordMinAge': '8640000',
                                             'passwordChange': 'on'})
    except ldap.LDAPError as e:
        log.fatal('Failed to set password policy: ' + str(e))

    # Add user, bind, and set password
    try:
        topo.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'user1',
            'userpassword': PASSWORD
        })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add user: error ' + e.args[0]['desc'])
        assert False

    # Reset password as RootDN
    try:
        topo.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'userpassword', ensure_bytes(PASSWORD))])
    except ldap.LDAPError as e:
        log.fatal('Failed to bind: error ' + e.args[0]['desc'])
        assert False

    time.sleep(1)

    # Reset password as user
    try:
        topo.standalone.simple_bind_s(USER_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind: error ' + e.args[0]['desc'])
        assert False

    try:
        topo.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'userpassword', ensure_bytes(PASSWORD))])
    except ldap.LDAPError as e:
        log.fatal('Failed to change password: error ' + e.args[0]['desc'])
        assert False

    ###################################
    # Make sure ldappasswd also works
    ###################################

    # Reset password as RootDN
    try:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as rootdn: error ' + e.args[0]['desc'])
        assert False

    try:
        topo.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'userpassword', ensure_bytes(PASSWORD))])
    except ldap.LDAPError as e:
        log.fatal('Failed to bind: error ' + e.args[0]['desc'])
        assert False

    time.sleep(1)

    # Run ldappasswd as the User.
    os.environ["LDAPTLS_CACERTDIR"] = topo.standalone.get_cert_dir()
    cmd = ('ldappasswd' + ' -h ' + topo.standalone.host + ' -Z -p 38901 -D ' + USER_DN +
           ' -w password -a password -s password2 ' + USER_DN)
    os.system(cmd)
    time.sleep(1)

    try:
        topo.standalone.simple_bind_s(USER_DN, "password2")
    except ldap.LDAPError as e:
        log.fatal('Failed to bind: error ' + e.args[0]['desc'])
        assert False

    log.info('Test Passed')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
