# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import subprocess
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st

log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
from lib389.utils import *

# Skip on older versions
pytestmark = pytest.mark.skipif(ds_is_older('1.3.4'), reason="Not implemented")
ENCRYPTION_DN = 'cn=encryption,%s' % CONFIG_DN
RSA = 'RSA'
RSA_DN = 'cn=%s,%s' % (RSA, ENCRYPTION_DN)
LDAPSPORT = str(DEFAULT_SECURE_PORT)
SERVERCERT = 'Server-Cert'
plus_all_ecount = 0
plus_all_dcount = 0
plus_all_ecount_noweak = 0
plus_all_dcount_noweak = 0


def _header(topology_st, label):
    topology_st.standalone.log.info("\n\n###############################################")
    topology_st.standalone.log.info("####### %s" % label)
    topology_st.standalone.log.info("###############################################")


def test_init(topology_st):
    """
    Generate self signed cert and import it to the DS cert db.
    Enable SSL
    """
    _header(topology_st, 'Testing Ticket 48194 - harden the list of ciphers available by default')

    conf_dir = topology_st.standalone.confdir

    log.info("\n######################### Checking existing certs ######################\n")
    os.system('certutil -L -d %s -n "CA certificate"' % conf_dir)
    os.system('certutil -L -d %s -n "%s"' % (conf_dir, SERVERCERT))

    log.info("\n######################### Create a password file ######################\n")
    pwdfile = '%s/pwdfile.txt' % (conf_dir)
    opasswd = os.popen("(ps -ef ; w ) | sha1sum | awk '{print $1}'", "r")
    passwd = opasswd.readline()
    pwdfd = open(pwdfile, "w")
    pwdfd.write(passwd)
    pwdfd.close()

    log.info("\n######################### Create a noise file ######################\n")
    noisefile = '%s/noise.txt' % (conf_dir)
    noise = os.popen("(w ; ps -ef ; date ) | sha1sum | awk '{print $1}'", "r")
    noisewdfd = open(noisefile, "w")
    noisewdfd.write(noise.readline())
    noisewdfd.close()
    time.sleep(1)

    log.info("\n######################### Create key3.db and cert8.db database ######################\n")
    os.system("ls %s" % pwdfile)
    os.system("cat %s" % pwdfile)
    os.system('certutil -N -d %s -f %s' % (conf_dir, pwdfile))

    log.info("\n######################### Creating encryption key for CA ######################\n")
    os.system('certutil -G -d %s -z %s -f %s' % (conf_dir, noisefile, pwdfile))

    log.info("\n######################### Creating self-signed CA certificate ######################\n")
    os.system(
        '( echo y ; echo ; echo y ) | certutil -S -n "CA certificate" -s "cn=CAcert" -x -t "CT,," -m 1000 -v 120 -d %s -z %s -f %s -2' %
        (conf_dir, noisefile, pwdfile))

    log.info("\n######################### Exporting the CA certificate to cacert.asc ######################\n")
    cafile = '%s/cacert.asc' % conf_dir
    catxt = os.popen('certutil -L -d %s -n "CA certificate" -a' % conf_dir)
    cafd = open(cafile, "w")
    while True:
        line = catxt.readline()
        if (line == ''):
            break
        cafd.write(line)
    cafd.close()

    log.info("\n######################### Generate the server certificate ######################\n")
    ohostname = os.popen('hostname --fqdn', "r")
    myhostname = ohostname.readline()
    os.system(
        'certutil -S -n "%s" -s "cn=%s,ou=389 Directory Server" -c "CA certificate" -t "u,u,u" -m 1001 -v 120 -d %s -z %s -f %s' %
        (SERVERCERT, myhostname.rstrip(), conf_dir, noisefile, pwdfile))

    log.info("\n######################### create the pin file ######################\n")
    pinfile = '%s/pin.txt' % (conf_dir)
    pintxt = 'Internal (Software) Token:%s' % passwd
    pinfd = open(pinfile, "w")
    pinfd.write(pintxt)
    pinfd.close()
    time.sleep(1)

    log.info("\n######################### enable SSL in the directory server with all ciphers ######################\n")
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3', 'off'),
                                                    (ldap.MOD_REPLACE, 'nsTLS1', 'on'),
                                                    (ldap.MOD_REPLACE, 'nsSSLClientAuth', 'allowed'),
                                                    (ldap.MOD_REPLACE, 'allowWeakCipher', 'on'),
                                                    (ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+all')])

    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-security', 'on'),
                                                (ldap.MOD_REPLACE, 'nsslapd-ssl-check-hostname', 'off'),
                                                (ldap.MOD_REPLACE, 'nsslapd-secureport', LDAPSPORT)])

    topology_st.standalone.add_s(Entry((RSA_DN, {'objectclass': "top nsEncryptionModule".split(),
                                                 'cn': RSA,
                                                 'nsSSLPersonalitySSL': SERVERCERT,
                                                 'nsSSLToken': 'internal (software)',
                                                 'nsSSLActivation': 'on'})))


def connectWithOpenssl(topology_st, cipher, expect):
    """
    Connect with the given cipher
    Condition:
    If expect is True, the handshake should be successful.
    If expect is False, the handshake should be refused with
       access log: "Cannot communicate securely with peer:
                   no common encryption algorithm(s)."
    """
    log.info("Testing %s -- expect to handshake %s", cipher, "successfully" if expect else "failed")

    myurl = 'localhost:%s' % LDAPSPORT
    cmdline = ['/usr/bin/openssl', 's_client', '-connect', myurl, '-cipher', cipher]

    strcmdline = '/usr/bin/openssl s_client -connect localhost:%s -cipher %s' % (LDAPSPORT, cipher)
    log.info("Running cmdline: %s", strcmdline)

    try:
        proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr=subprocess.STDOUT)
    except ValueError:
        log.info("%s failed: %s", cmdline, ValueError)
        proc.kill()

    while True:
        l = proc.stdout.readline()
        if l == "":
            break
        if 'Cipher is' in l:
            log.info("Found: %s", l)
            if expect:
                if '(NONE)' in l:
                    assert False
                else:
                    proc.stdin.close()
                    assert True
            else:
                if '(NONE)' in l:
                    assert True
                else:
                    proc.stdin.close()
                    assert False


def test_run_0(topology_st):
    """
    Check nsSSL3Ciphers: +all
    All ciphers are enabled except null.
    Note: allowWeakCipher: on
    """
    _header(topology_st, 'Test Case 1 - Check the ciphers availability for "+all"; allowWeakCipher: on')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '64')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.restart(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', True)
    connectWithOpenssl(topology_st, 'AES256-SHA256', True)


def test_run_1(topology_st):
    """
    Check nsSSL3Ciphers: +all
    All ciphers are enabled except null.
    Note: default allowWeakCipher (i.e., off) for +all
    """
    _header(topology_st, 'Test Case 2 - Check the ciphers availability for "+all" with default allowWeakCiphers')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '64')])
    # Make sure allowWeakCipher is not set.
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_DELETE, 'allowWeakCipher', None)])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_0' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(2)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', False)
    connectWithOpenssl(topology_st, 'AES256-SHA256', True)


def test_run_2(topology_st):
    """
    Check nsSSL3Ciphers: +rsa_aes_128_sha,+rsa_aes_256_sha
    rsa_aes_128_sha, tls_rsa_aes_128_sha, rsa_aes_256_sha, tls_rsa_aes_256_sha are enabled.
    default allowWeakCipher
    """
    _header(topology_st,
            'Test Case 3 - Check the ciphers availability for "+rsa_aes_128_sha,+rsa_aes_256_sha" with default allowWeakCipher')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN,
                                    [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+rsa_aes_128_sha,+rsa_aes_256_sha')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_1' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(2)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', False)
    connectWithOpenssl(topology_st, 'AES256-SHA256', False)
    connectWithOpenssl(topology_st, 'AES128-SHA', True)
    connectWithOpenssl(topology_st, 'AES256-SHA', True)


def test_run_3(topology_st):
    """
    Check nsSSL3Ciphers: -all
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology_st, 'Test Case 4 - Check the ciphers availability for "-all"')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', '-all')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_2' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', False)
    connectWithOpenssl(topology_st, 'AES256-SHA256', False)


def test_run_4(topology_st):
    """
    Check no nsSSL3Ciphers
    Default ciphers are enabled.
    default allowWeakCipher
    """
    _header(topology_st, 'Test Case 5 - Check no nsSSL3Ciphers (-all) with default allowWeakCipher')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_DELETE, 'nsSSL3Ciphers', '-all')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_3' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(2)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', False)
    connectWithOpenssl(topology_st, 'AES256-SHA256', True)


def test_run_5(topology_st):
    """
    Check nsSSL3Ciphers: default
    Default ciphers are enabled.
    default allowWeakCipher
    """
    _header(topology_st, 'Test Case 6 - Check default nsSSL3Ciphers (default setting) with default allowWeakCipher')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', 'default')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_4' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(2)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', False)
    connectWithOpenssl(topology_st, 'AES256-SHA256', True)


def test_run_6(topology_st):
    """
    Check nsSSL3Ciphers: +all,-TLS_RSA_WITH_AES_256_CBC_SHA256
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology_st,
            'Test Case 7 - Check nsSSL3Ciphers: +all,-TLS_RSA_WITH_AES_256_CBC_SHA256  with default allowWeakCipher')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN,
                                    [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+all,-TLS_RSA_WITH_AES_256_CBC_SHA256')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_5' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(2)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', False)
    connectWithOpenssl(topology_st, 'AES256-SHA256', False)
    connectWithOpenssl(topology_st, 'AES128-SHA', True)


def test_run_7(topology_st):
    """
    Check nsSSL3Ciphers: -all,+rsa_rc4_128_md5
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology_st, 'Test Case 8 - Check nsSSL3Ciphers: -all,+rsa_rc4_128_md5 with default allowWeakCipher')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', '-all,+rsa_rc4_128_md5')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_6' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(2)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', False)
    connectWithOpenssl(topology_st, 'AES256-SHA256', False)
    connectWithOpenssl(topology_st, 'RC4-MD5', True)


def test_run_8(topology_st):
    """
    Check nsSSL3Ciphers: default + allowWeakCipher: off
    Strong Default ciphers are enabled.
    """
    _header(topology_st, 'Test Case 9 - Check default nsSSL3Ciphers (default setting + allowWeakCipher: off)')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', 'default'),
                                                    (ldap.MOD_REPLACE, 'allowWeakCipher', 'off')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_7' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(2)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', False)
    connectWithOpenssl(topology_st, 'AES256-SHA256', True)


def test_run_9(topology_st):
    """
    Check no nsSSL3Ciphers
    Default ciphers are enabled.
    allowWeakCipher: on
    nsslapd-errorlog-level: 0
    """
    _header(topology_st,
            'Test Case 10 - Check no nsSSL3Ciphers (default setting) with no errorlog-level & allowWeakCipher on')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', None),
                                                    (ldap.MOD_REPLACE, 'allowWeakCipher', 'on')])
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', None)])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_8' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(2)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', True)
    connectWithOpenssl(topology_st, 'AES256-SHA256', True)


def test_run_10(topology_st):
    """
    Check nsSSL3Ciphers: -TLS_RSA_WITH_NULL_MD5,+TLS_RSA_WITH_RC4_128_MD5,
        +TLS_RSA_EXPORT_WITH_RC4_40_MD5,+TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5,
        +TLS_DHE_RSA_WITH_DES_CBC_SHA,+SSL_RSA_FIPS_WITH_DES_CBC_SHA,
        +TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA,+SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA,
        +TLS_RSA_EXPORT1024_WITH_RC4_56_SHA,+TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA,
        -SSL_CK_RC4_128_WITH_MD5,-SSL_CK_RC4_128_EXPORT40_WITH_MD5,
        -SSL_CK_RC2_128_CBC_WITH_MD5,-SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5,
        -SSL_CK_DES_64_CBC_WITH_MD5,-SSL_CK_DES_192_EDE3_CBC_WITH_MD5
    allowWeakCipher: on
    nsslapd-errorlog-level: 0
    """
    _header(topology_st,
            'Test Case 11 - Check nsSSL3Ciphers: long list using the NSS Cipher Suite name with allowWeakCipher on')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers',
                                                     '-TLS_RSA_WITH_NULL_MD5,+TLS_RSA_WITH_RC4_128_MD5,+TLS_RSA_EXPORT_WITH_RC4_40_MD5,+TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5,+TLS_DHE_RSA_WITH_DES_CBC_SHA,+SSL_RSA_FIPS_WITH_DES_CBC_SHA,+TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA,+SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA,+TLS_RSA_EXPORT1024_WITH_RC4_56_SHA,+TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA,-SSL_CK_RC4_128_WITH_MD5,-SSL_CK_RC4_128_EXPORT40_WITH_MD5,-SSL_CK_RC2_128_CBC_WITH_MD5,-SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5,-SSL_CK_DES_64_CBC_WITH_MD5,-SSL_CK_DES_192_EDE3_CBC_WITH_MD5')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_9' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', False)
    connectWithOpenssl(topology_st, 'RC4-MD5', True)
    connectWithOpenssl(topology_st, 'AES256-SHA256', False)


def test_run_11(topology_st):
    """
    Check nsSSL3Ciphers: +fortezza
    SSL_GetImplementedCiphers does not return this as a secuire cipher suite
    """
    _header(topology_st, 'Test Case 12 - Check nsSSL3Ciphers: +fortezza, which is not supported')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+fortezza')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.48194_10' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    connectWithOpenssl(topology_st, 'RC4-SHA', False)
    connectWithOpenssl(topology_st, 'AES256-SHA256', False)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
