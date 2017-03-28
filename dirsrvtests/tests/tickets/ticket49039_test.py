import time
import ldap
import logging
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st as topo

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

    # Configure password policy
    try:
        topo.standalone.modify_s("cn=config", [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', 'on'),
                                               (ldap.MOD_REPLACE, 'passwordMustChange', 'on'),
                                               (ldap.MOD_REPLACE, 'passwordExp', 'on'),
                                               (ldap.MOD_REPLACE, 'passwordMaxAge', '86400000'),
                                               (ldap.MOD_REPLACE, 'passwordMinAge', '8640000'),
                                               (ldap.MOD_REPLACE, 'passwordChange', 'on')])
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
        log.fatal('Failed to add user: error ' + e.message['desc'])
        assert False

    # Reset password as RootDN
    try:
        topo.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'userpassword', PASSWORD)])
    except ldap.LDAPError as e:
        log.fatal('Failed to bind: error ' + e.message['desc'])
        assert False

    time.sleep(1)

    # Reset password as user
    try:
        topo.standalone.simple_bind_s(USER_DN, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind: error ' + e.message['desc'])
        assert False

    try:
        topo.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'userpassword', PASSWORD)])
    except ldap.LDAPError as e:
        log.fatal('Failed to change password: error ' + e.message['desc'])
        assert False

    log.info('Test Passed')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

