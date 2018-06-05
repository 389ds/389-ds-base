# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time

import socket
import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st
from lib389.nss_ssl import NssSsl

log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
from lib389.utils import *

# Skip on older versions
pytestmark = pytest.mark.skipif(ds_is_older('1.3.3'), reason="Not implemented")
ENCRYPTION_DN = 'cn=encryption,%s' % CONFIG_DN
MY_SECURE_PORT = '63601'
RSA = 'RSA'
RSA_DN = 'cn=%s,%s' % (RSA, ENCRYPTION_DN)
SERVERCERT = 'Server-Cert'
plus_all_ecount = 0
plus_all_dcount = 0
plus_all_ecount_noweak = 0
plus_all_dcount_noweak = 0

# Cipher counts tend to change with each new verson of NSS
nss_version = ''
NSS320 = '3.20.0'
NSS321 = '3.21.0'  # RHEL6
NSS323 = '3.23.0'  # F22
NSS325 = '3.25.0'  # F23/F24
NSS327 = '3.27.0'  # F25
NSS330 = '3.30.0'  # F27


def _header(topology_st, label):
    topology_st.standalone.log.info("\n\n###############################################")
    topology_st.standalone.log.info("#######")
    topology_st.standalone.log.info("####### %s" % label)
    topology_st.standalone.log.info("#######")
    topology_st.standalone.log.info("###############################################")


def test_47838_init(topology_st):
    """
    Generate self signed cert and import it to the DS cert db.
    Enable SSL
    """
    _header(topology_st, 'Testing Ticket 47838 - harden the list of ciphers available by default')
    onss_version = os.popen("rpm -q nss | awk -F'-' '{print $2}'", "r")
    global nss_version
    nss_version = onss_version.readline()
    nss_ssl = NssSsl(dbpath=topology_st.standalone.get_cert_dir())
    nss_ssl.reinit()
    nss_ssl.create_rsa_ca()
    nss_ssl.create_rsa_key_and_cert()

    log.info("\n######################### enable SSL in the directory server with all ciphers ######################\n")
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3', b'off'),
                                                    (ldap.MOD_REPLACE, 'nsTLS1', b'on'),
                                                    (ldap.MOD_REPLACE, 'nsSSLClientAuth', b'allowed'),
                                                    (ldap.MOD_REPLACE, 'allowWeakCipher', b'on'),
                                                    (ldap.MOD_REPLACE, 'nsSSL3Ciphers', b'+all')])

    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-security', b'on'),
                                                (ldap.MOD_REPLACE, 'nsslapd-ssl-check-hostname', b'off'),
                                                (ldap.MOD_REPLACE, 'nsslapd-secureport', ensure_bytes(MY_SECURE_PORT))])

    topology_st.standalone.add_s(Entry((RSA_DN, {'objectclass': "top nsEncryptionModule".split(),
                                                 'cn': RSA,
                                                 'nsSSLPersonalitySSL': SERVERCERT,
                                                 'nsSSLToken': 'internal (software)',
                                                 'nsSSLActivation': 'on'})))


def comp_nsSSLEnableCipherCount(topology_st, ecount):
    """
    Check nsSSLEnabledCipher count with ecount
    """
    log.info("Checking nsSSLEnabledCiphers...")
    msgid = topology_st.standalone.search_ext(ENCRYPTION_DN, ldap.SCOPE_BASE, 'cn=*', ['nsSSLEnabledCiphers'])
    enabledciphercnt = 0
    rtype, rdata, rmsgid = topology_st.standalone.result2(msgid)
    topology_st.standalone.log.info("%d results" % len(rdata))

    topology_st.standalone.log.info("Results:")
    for dn, attrs in rdata:
        topology_st.standalone.log.info("dn: %s" % dn)
        if 'nsSSLEnabledCiphers' in attrs:
            enabledciphercnt = len(attrs['nsSSLEnabledCiphers'])
    topology_st.standalone.log.info("enabledCipherCount: %d" % enabledciphercnt)
    assert ecount == enabledciphercnt


def test_47838_run_0(topology_st):
    """
    Check nsSSL3Ciphers: +all
    All ciphers are enabled except null.
    Note: allowWeakCipher: on
    """
    _header(topology_st, 'Test Case 1 - Check the ciphers availability for "+all"; allowWeakCipher: on')
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'64')])
    time.sleep(5)
    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.restart(timeout=120)
    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    disabled = os.popen('egrep "SSL info:" %s | egrep \": disabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    if nss_version >= NSS320:
        assert ecount >= 53
        assert dcount <= 17
    else:
        assert ecount >= 60
        assert dcount <= 7

    global plus_all_ecount
    global plus_all_dcount
    plus_all_ecount = ecount
    plus_all_dcount = dcount
    weak = os.popen('egrep "SSL info:" %s | egrep "WEAK CIPHER" | wc -l' % topology_st.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers: %d" % wcount)
    assert wcount <= 29

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_1(topology_st):
    """
    Check nsSSL3Ciphers: +all
    All ciphers are enabled except null.
    Note: default allowWeakCipher (i.e., off) for +all
    """
    _header(topology_st, 'Test Case 2 - Check the ciphers availability for "+all" with default allowWeakCiphers')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'64')])
    time.sleep(1)
    # Make sure allowWeakCipher is not set.
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_DELETE, 'allowWeakCipher', None)])

    log.info("\n######################### Restarting the server ######################\n")
    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_0' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    disabled = os.popen('egrep "SSL info:" %s | egrep \": disabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    global plus_all_ecount_noweak
    global plus_all_dcount_noweak
    plus_all_ecount_noweak = ecount
    plus_all_dcount_noweak = dcount

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    assert ecount >= 31
    assert dcount <= 36
    weak = os.popen('egrep "SSL info:" %s | egrep "WEAK CIPHER" | wc -l' % topology_st.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers: %d" % wcount)
    assert wcount <= 29

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_2(topology_st):
    """
    Check nsSSL3Ciphers: +rsa_aes_128_sha,+rsa_aes_256_sha
    rsa_aes_128_sha, tls_rsa_aes_128_sha, rsa_aes_256_sha, tls_rsa_aes_256_sha are enabled.
    default allowWeakCipher
    """
    _header(topology_st,
            'Test Case 3 - Check the ciphers availability for "+rsa_aes_128_sha,+rsa_aes_256_sha" with default allowWeakCipher')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN,
                                    [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', b'+rsa_aes_128_sha,+rsa_aes_256_sha')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_1' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    disabled = os.popen('egrep "SSL info:" %s | egrep \": disabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    assert ecount == 2
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_3(topology_st):
    """
    Check nsSSL3Ciphers: -all
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology_st, 'Test Case 4 - Check the ciphers availability for "-all"')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', b'-all')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_2' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    global plus_all_ecount
    assert ecount == 0

    disabledmsg = os.popen('egrep "Disabling SSL" %s' % topology_st.standalone.errlog)
    log.info("Disabling SSL message?: %s" % disabledmsg.readline())
    assert disabledmsg != ''

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_4(topology_st):
    """
    Check no nsSSL3Ciphers
    Default ciphers are enabled.
    default allowWeakCipher
    """
    _header(topology_st, 'Test Case 5 - Check no nsSSL3Ciphers (default setting) with default allowWeakCipher')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_DELETE, 'nsSSL3Ciphers', b'-all')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_3' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)
    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    disabled = os.popen('egrep "SSL info:" %s | egrep \": disabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    if nss_version >= NSS330:
        assert ecount == 28
    elif nss_version >= NSS323:
        assert ecount == 29
    else:
        assert ecount == 20
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)
    weak = os.popen(
        'egrep "SSL info:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology_st.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    assert wcount == 0

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_5(topology_st):
    """
    Check nsSSL3Ciphers: default
    Default ciphers are enabled.
    default allowWeakCipher
    """
    _header(topology_st, 'Test Case 6 - Check default nsSSL3Ciphers (default setting) with default allowWeakCipher')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', b'default')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_4' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    disabled = os.popen('egrep "SSL info:" %s | egrep \": disabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    if nss_version >= NSS330:
        assert ecount == 28
    elif nss_version >= NSS323:
        assert ecount == 29
    else:
        assert ecount == 23
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)
    weak = os.popen(
        'egrep "SSL info:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology_st.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    assert wcount == 0

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_6(topology_st):
    """
    Check nsSSL3Ciphers: +all,-rsa_rc4_128_md5
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology_st,
            'Test Case 7 - Check nsSSL3Ciphers: +all,-tls_dhe_rsa_aes_128_gcm_sha with default allowWeakCipher')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN,
                                    [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', b'+all,-tls_dhe_rsa_aes_128_gcm_sha')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_5' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    disabled = os.popen('egrep "SSL info:" %s | egrep \": disabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount_noweak
    global plus_all_dcount_noweak
    log.info("ALL Ecount: %d" % plus_all_ecount_noweak)
    log.info("ALL Dcount: %d" % plus_all_dcount_noweak)
    assert ecount == (plus_all_ecount_noweak - 1)
    assert dcount == (plus_all_dcount_noweak + 1)

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_7(topology_st):
    """
    Check nsSSL3Ciphers: -all,+rsa_rc4_128_md5
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology_st, 'Test Case 8 - Check nsSSL3Ciphers: -all,+rsa_rc4_128_md5 with default allowWeakCipher')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', b'-all,+rsa_rc4_128_md5')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_6' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    disabled = os.popen('egrep "SSL info:" %s | egrep \": disabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    assert ecount == 1
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_8(topology_st):
    """
    Check nsSSL3Ciphers: default + allowWeakCipher: off
    Strong Default ciphers are enabled.
    """
    _header(topology_st, 'Test Case 9 - Check default nsSSL3Ciphers (default setting + allowWeakCipher: off)')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', b'default'),
                                                    (ldap.MOD_REPLACE, 'allowWeakCipher', b'off')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_7' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    disabled = os.popen('egrep "SSL info:" %s | egrep \": disabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    if nss_version >= NSS330:
        assert ecount == 28
    elif nss_version >= NSS323:
        assert ecount == 29
    else:
        assert ecount == 23
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)
    weak = os.popen(
        'egrep "SSL info:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology_st.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    assert wcount == 0

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_9(topology_st):
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
                                                    (ldap.MOD_REPLACE, 'allowWeakCipher', b'on')])
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', None)])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_8' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    disabled = os.popen('egrep "SSL info:" %s | egrep \": disabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    if nss_version >= NSS330:
        assert ecount == 33
    elif nss_version >= NSS327:
        assert ecount == 34
    elif nss_version >= NSS323:
        assert ecount == 36
    else:
        assert ecount == 30
    assert dcount == 0
    weak = os.popen(
        'egrep "SSL info:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology_st.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    if nss_version >= NSS327:
        assert wcount == 5
    elif nss_version >= NSS320:
        assert wcount == 7
    else:
        assert wcount == 11

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_10(topology_st):
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
                                                     b'-TLS_RSA_WITH_NULL_MD5,+TLS_RSA_WITH_RC4_128_MD5,+TLS_RSA_EXPORT_WITH_RC4_40_MD5,+TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5,+TLS_DHE_RSA_WITH_DES_CBC_SHA,+SSL_RSA_FIPS_WITH_DES_CBC_SHA,+TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA,+SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA,+TLS_RSA_EXPORT1024_WITH_RC4_56_SHA,+TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA,-SSL_CK_RC4_128_WITH_MD5,-SSL_CK_RC4_128_EXPORT40_WITH_MD5,-SSL_CK_RC2_128_CBC_WITH_MD5,-SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5,-SSL_CK_DES_64_CBC_WITH_MD5,-SSL_CK_DES_192_EDE3_CBC_WITH_MD5')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_9' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL info:" %s | egrep \": enabled\" | wc -l' % topology_st.standalone.errlog)
    disabled = os.popen('egrep "SSL info:" %s | egrep \": disabled\" | wc -l' % topology_st.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    if nss_version >= NSS330:
        assert ecount == 3
    else:
        assert ecount == 9
    assert dcount == 0
    weak = os.popen(
        'egrep "SSL info:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology_st.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)

    topology_st.standalone.log.info("ticket47838 was successfully verified.")

    comp_nsSSLEnableCipherCount(topology_st, ecount)


def test_47838_run_11(topology_st):
    """
    Check nsSSL3Ciphers: +fortezza
    SSL_GetImplementedCiphers does not return this as a secuire cipher suite
    """
    _header(topology_st, 'Test Case 12 - Check nsSSL3Ciphers: +fortezza, which is not supported')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', b'+fortezza')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_10' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    errmsg = os.popen('egrep "SSL info:" %s | egrep "is not available in NSS"' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected error message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected error message was not found")
        assert False

    comp_nsSSLEnableCipherCount(topology_st, 0)


def test_47928_run_0(topology_st):
    """
    No SSL version config parameters.
    Check SSL3 (TLS1.0) is off.
    """
    _header(topology_st, 'Test Case 13 - No SSL version config parameters')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    # add them once and remove them
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3', b'off'),
                                                    (ldap.MOD_REPLACE, 'nsTLS1', b'on'),
                                                    (ldap.MOD_REPLACE, 'sslVersionMin', b'TLS1.1'),
                                                    (ldap.MOD_REPLACE, 'sslVersionMax', b'TLS1.2')])
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_DELETE, 'nsSSL3', None),
                                                    (ldap.MOD_DELETE, 'nsTLS1', None),
                                                    (ldap.MOD_DELETE, 'sslVersionMin', None),
                                                    (ldap.MOD_DELETE, 'sslVersionMax', None)])
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'64')])
    time.sleep(5)

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_11' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    errmsg = os.popen(
        'egrep "SSL info:" %s | egrep "Default SSL Version settings; Configuring the version range as min: TLS1.1"' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False


def test_47928_run_1(topology_st):
    """
    No nsSSL3, nsTLS1; sslVersionMin > sslVersionMax
    Check sslVersionMax is ignored.
    """
    _header(topology_st, 'Test Case 14 - No nsSSL3, nsTLS1; sslVersionMin > sslVersionMax')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'sslVersionMin', b'TLS1.2'),
                                                    (ldap.MOD_REPLACE, 'sslVersionMax', b'TLS1.1')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_12' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    topology_st.standalone.start(timeout=120)

    errmsg = os.popen(
        'egrep "SSL info:" %s | egrep "The min value of NSS version range"' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False

    errmsg = os.popen(
        'egrep "SSL Initialization" %s | egrep "Configured SSL version range: min: TLS1.2, max: TLS1"' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False


def test_47928_run_2(topology_st):
    """
    nsSSL3: on; sslVersionMin: TLS1.1; sslVersionMax: TLS1.2
    Conflict between nsSSL3 and range; nsSSL3 is disabled
    """
    _header(topology_st, 'Test Case 15 - nsSSL3: on; sslVersionMin: TLS1.1; sslVersionMax: TLS1.2')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'sslVersionMin', b'TLS1.1'),
                                                    (ldap.MOD_REPLACE, 'sslVersionMax', b'TLS1.2'),
                                                    (ldap.MOD_REPLACE, 'nsSSL3', b'on')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_13' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    errmsg = os.popen(
        'egrep "SSL info:" %s | egrep "Found unsecure configuration: nsSSL3: on"' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False

    errmsg = os.popen('egrep "SSL info:" %s | egrep "Respect the supported range."' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False

    errmsg = os.popen(
        'egrep "SSL Initialization" %s | egrep "Configured SSL version range: min: TLS1.1, max: TLS1"' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False


def test_47928_run_3(topology_st):
    """
    nsSSL3: on; nsTLS1: off; sslVersionMin: TLS1.1; sslVersionMax: TLS1.2
    Conflict between nsSSL3/nsTLS1 and range; nsSSL3 is disabled; nsTLS1 is enabled.
    """
    _header(topology_st, 'Test Case 16 - nsSSL3: on; nsTLS1: off; sslVersionMin: TLS1.1; sslVersionMax: TLS1.2')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'sslVersionMin', b'TLS1.1'),
                                                    (ldap.MOD_REPLACE, 'sslVersionMax', b'TLS1.2'),
                                                    (ldap.MOD_REPLACE, 'nsSSL3', b'on'),
                                                    (ldap.MOD_REPLACE, 'nsTLS1', b'off')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_14' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    errmsg = os.popen(
        'egrep "SSL info:" %s | egrep "Found unsecure configuration: nsSSL3: on"' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False

    errmsg = os.popen('egrep "SSL info:" %s | egrep "Respect the configured range."' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False

    errmsg = os.popen(
        'egrep "SSL Initialization" %s | egrep "Configured SSL version range: min: TLS1.1, max: TLS1"' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False


def test_47838_run_last(topology_st):
    """
    Check nsSSL3Ciphers: all <== invalid value
    All ciphers are disabled.
    """
    _header(topology_st, 'Test Case 17 - Check nsSSL3Ciphers: all, which is invalid')

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', None)])
    topology_st.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', b'all')])

    log.info("\n######################### Restarting the server ######################\n")
    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_15' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    time.sleep(1)
    topology_st.standalone.start(timeout=120)

    errmsg = os.popen('egrep "SSL info:" %s | egrep "invalid ciphers"' % topology_st.standalone.errlog)
    if errmsg != "":
        log.info("Expected error message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected error message was not found")
        assert False

    comp_nsSSLEnableCipherCount(topology_st, 0)

    topology_st.standalone.log.info("ticket47838, 47880, 47908, 47928 were successfully verified.")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
