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


DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

USER_DN = 'uid=user,dc=example,dc=com'
CONFIG_DN = 'cn=config'
ENCRYPTION_DN = 'cn=encryption,%s' % CONFIG_DN
RSA = 'RSA'
RSA_DN = 'cn=%s,%s' % (RSA, ENCRYPTION_DN)
ISSUER = 'cn=CAcert'
CACERT = 'CAcertificate'
SERVERCERT = 'Server-Cert'


def ssl_init(topo):
    """ Setup TLS
    """
    topo.standalone.stop()
    topo.standalone.nss_ssl.reinit()
    topo.standalone.nss_ssl.create_rsa_ca()
    topo.standalone.nss_ssl.create_rsa_key_and_cert()
    topo.standalone.start()

    topo.standalone.modify_s(ENCRYPTION_DN,
                             [(ldap.MOD_REPLACE, 'nsSSL3', 'off'),
                              (ldap.MOD_REPLACE, 'nsTLS1', 'on'),
                              (ldap.MOD_REPLACE, 'nsSSLClientAuth', 'allowed'),
                              (ldap.MOD_REPLACE, 'allowWeakCipher', 'on'),
                              (ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+all')])

    time.sleep(1)
    topo.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-security', 'on'),
                                         (ldap.MOD_REPLACE, 'nsslapd-ssl-check-hostname', 'off'),
                                         (ldap.MOD_REPLACE, 'nsslapd-secureport', '636')])

    time.sleep(1)
    topo.standalone.add_s(Entry((RSA_DN, {'objectclass': "top nsEncryptionModule".split(),
                                          'cn': RSA,
                                          'nsSSLPersonalitySSL': SERVERCERT,
                                          'nsSSLToken': 'internal (software)',
                                          'nsSSLActivation': 'on'})))
    time.sleep(1)
    topo.standalone.restart()

    log.info("SSL setup complete\n")


def test_ticket49039(topo):
    """Test "password must change" verses "password min age".  Min age should not
    block password update if the password was reset.
    """

    # Setup SSL (for ldappasswd test)
    ssl_init(topo)

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

    ###################################
    # Make sure ldappasswd also works
    ###################################

    # Reset password as RootDN
    try:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as rootdn: error ' + e.message['desc'])
        assert False

    try:
        topo.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'userpassword', PASSWORD)])
    except ldap.LDAPError as e:
        log.fatal('Failed to bind: error ' + e.message['desc'])
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
        log.fatal('Failed to bind: error ' + e.message['desc'])
        assert False

    log.info('Test Passed')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

