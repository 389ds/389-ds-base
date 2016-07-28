# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
import shutil
from lib389 import DirSrv, Entry, tools
from lib389 import DirSrvTools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *

log = logging.getLogger(__name__)

installation_prefix = None

CONFIG_DN = 'cn=config'
ENCRYPTION_DN = 'cn=encryption,%s' % CONFIG_DN
MY_SECURE_PORT = '36363'
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


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
    '''
    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix

    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def _header(topology, label):
    topology.standalone.log.info("\n\n###############################################")
    topology.standalone.log.info("#######")
    topology.standalone.log.info("####### %s" % label)
    topology.standalone.log.info("#######")
    topology.standalone.log.info("###############################################")


def test_47838_init(topology):
    """
    Generate self signed cert and import it to the DS cert db.
    Enable SSL
    """
    _header(topology, 'Testing Ticket 47838 - harden the list of ciphers available by default')

    onss_version = os.popen("rpm -q nss | awk -F'-' '{print $2}'", "r")
    global nss_version
    nss_version = onss_version.readline()

    conf_dir = topology.standalone.confdir

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

    log.info("\n######################### Create key3.db and cert8.db database ######################\n")
    os.system("ls %s" % pwdfile)
    os.system("cat %s" % pwdfile)
    os.system('certutil -N -d %s -f %s' % (conf_dir, pwdfile))

    log.info("\n######################### Creating encryption key for CA ######################\n")
    os.system('certutil -G -d %s -z %s -f %s' % (conf_dir, noisefile, pwdfile))

    log.info("\n######################### Creating self-signed CA certificate ######################\n")
    os.system('( echo y ; echo ; echo y ) | certutil -S -n "CA certificate" -s "cn=CAcert" -x -t "CT,," -m 1000 -v 120 -d %s -z %s -f %s -2' % (conf_dir, noisefile, pwdfile))

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
    os.system('certutil -S -n "%s" -s "cn=%s,ou=389 Directory Server" -c "CA certificate" -t "u,u,u" -m 1001 -v 120 -d %s -z %s -f %s' % (SERVERCERT, myhostname.rstrip(), conf_dir, noisefile, pwdfile))

    log.info("\n######################### create the pin file ######################\n")
    pinfile = '%s/pin.txt' % (conf_dir)
    pintxt = 'Internal (Software) Token:%s' % passwd
    pinfd = open(pinfile, "w")
    pinfd.write(pintxt)
    pinfd.close()

    log.info("\n######################### enable SSL in the directory server with all ciphers ######################\n")
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3', 'off'),
                                                 (ldap.MOD_REPLACE, 'nsTLS1', 'on'),
                                                 (ldap.MOD_REPLACE, 'nsSSLClientAuth', 'allowed'),
                                                 (ldap.MOD_REPLACE, 'allowWeakCipher', 'on'),
                                                 (ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+all')])

    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-security', 'on'),
                                             (ldap.MOD_REPLACE, 'nsslapd-ssl-check-hostname', 'off'),
                                             (ldap.MOD_REPLACE, 'nsslapd-secureport', MY_SECURE_PORT)])

    topology.standalone.add_s(Entry((RSA_DN, {'objectclass': "top nsEncryptionModule".split(),
                                              'cn': RSA,
                                              'nsSSLPersonalitySSL': SERVERCERT,
                                              'nsSSLToken': 'internal (software)',
                                              'nsSSLActivation': 'on'})))


def comp_nsSSLEnableCipherCount(topology, ecount):
    """
    Check nsSSLEnabledCipher count with ecount
    """
    log.info("Checking nsSSLEnabledCiphers...")
    msgid = topology.standalone.search_ext(ENCRYPTION_DN, ldap.SCOPE_BASE, 'cn=*', ['nsSSLEnabledCiphers'])
    enabledciphercnt = 0
    rtype, rdata, rmsgid = topology.standalone.result2(msgid)
    topology.standalone.log.info("%d results" % len(rdata))

    topology.standalone.log.info("Results:")
    for dn, attrs in rdata:
        topology.standalone.log.info("dn: %s" % dn)
        if 'nsSSLEnabledCiphers' in attrs:
            enabledciphercnt = len(attrs['nsSSLEnabledCiphers'])
    topology.standalone.log.info("enabledCipherCount: %d" % enabledciphercnt)
    assert ecount == enabledciphercnt


def test_47838_run_0(topology):
    """
    Check nsSSL3Ciphers: +all
    All ciphers are enabled except null.
    Note: allowWeakCipher: on
    """
    _header(topology, 'Test Case 1 - Check the ciphers availability for "+all"; allowWeakCipher: on')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '64')])
    time.sleep(5)
    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.restart(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
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
    weak = os.popen('egrep "SSL alert:" %s | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers: %d" % wcount)
    assert wcount <= 29

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_1(topology):
    """
    Check nsSSL3Ciphers: +all
    All ciphers are enabled except null.
    Note: default allowWeakCipher (i.e., off) for +all
    """
    _header(topology, 'Test Case 2 - Check the ciphers availability for "+all" with default allowWeakCiphers')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '64')])
    time.sleep(5)
    # Make sure allowWeakCipher is not set.
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_DELETE, 'allowWeakCipher', None)])

    log.info("\n######################### Restarting the server ######################\n")
    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_0' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
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
    weak = os.popen('egrep "SSL alert:" %s | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers: %d" % wcount)
    assert wcount <= 29

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_2(topology):
    """
    Check nsSSL3Ciphers: +rsa_aes_128_sha,+rsa_aes_256_sha
    rsa_aes_128_sha, tls_rsa_aes_128_sha, rsa_aes_256_sha, tls_rsa_aes_256_sha are enabled.
    default allowWeakCipher
    """
    _header(topology, 'Test Case 3 - Check the ciphers availability for "+rsa_aes_128_sha,+rsa_aes_256_sha" with default allowWeakCipher')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+rsa_aes_128_sha,+rsa_aes_256_sha')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_1' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    assert ecount == 2
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_3(topology):
    """
    Check nsSSL3Ciphers: -all
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology, 'Test Case 4 - Check the ciphers availability for "-all"')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', '-all')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_2' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    ecount = int(enabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    global plus_all_ecount
    assert ecount == 0

    disabledmsg = os.popen('egrep "Disabling SSL" %s' % topology.standalone.errlog)
    log.info("Disabling SSL message?: %s" % disabledmsg.readline())
    assert disabledmsg != ''

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_4(topology):
    """
    Check no nsSSL3Ciphers
    Default ciphers are enabled.
    default allowWeakCipher
    """
    _header(topology, 'Test Case 5 - Check no nsSSL3Ciphers (default setting) with default allowWeakCipher')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_DELETE, 'nsSSL3Ciphers', '-all')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_3' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    if nss_version >= NSS323:
        assert ecount == 23
    else:
        assert ecount == 20
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)
    weak = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    assert wcount == 0

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_5(topology):
    """
    Check nsSSL3Ciphers: default
    Default ciphers are enabled.
    default allowWeakCipher
    """
    _header(topology, 'Test Case 6 - Check default nsSSL3Ciphers (default setting) with default allowWeakCipher')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', 'default')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_4' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    if nss_version >= NSS320:
        assert ecount == 23
    else:
        assert ecount == 12
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)
    weak = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    assert wcount == 0

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_6(topology):
    """
    Check nsSSL3Ciphers: +all,-rsa_rc4_128_md5
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology, 'Test Case 7 - Check nsSSL3Ciphers: +all,-tls_dhe_rsa_aes_128_gcm_sha with default allowWeakCipher')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+all,-tls_dhe_rsa_aes_128_gcm_sha')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_5' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
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

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_7(topology):
    """
    Check nsSSL3Ciphers: -all,+rsa_rc4_128_md5
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology, 'Test Case 8 - Check nsSSL3Ciphers: -all,+rsa_rc4_128_md5 with default allowWeakCipher')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', '-all,+rsa_rc4_128_md5')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_6' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    assert ecount == 1
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_8(topology):
    """
    Check nsSSL3Ciphers: default + allowWeakCipher: off
    Strong Default ciphers are enabled.
    """
    _header(topology, 'Test Case 9 - Check default nsSSL3Ciphers (default setting + allowWeakCipher: off)')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', 'default'),
                                                 (ldap.MOD_REPLACE, 'allowWeakCipher', 'off')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_7' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    if nss_version >= NSS320:
       assert ecount == 23
    else:
       assert ecount == 12
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)
    weak = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    assert wcount == 0

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_9(topology):
    """
    Check no nsSSL3Ciphers
    Default ciphers are enabled.
    allowWeakCipher: on
    nsslapd-errorlog-level: 0
    """
    _header(topology, 'Test Case 10 - Check no nsSSL3Ciphers (default setting) with no errorlog-level & allowWeakCipher on')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', None),
                                                 (ldap.MOD_REPLACE, 'allowWeakCipher', 'on')])
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', None)])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_8' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    if nss_version >= NSS320:
        assert ecount == 30
    else:
        assert ecount == 23
    assert dcount == 0
    weak = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    if nss_version >= NSS320:
        assert wcount == 7
    else:
        assert wcount == 11

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_10(topology):
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
    _header(topology, 'Test Case 11 - Check nsSSL3Ciphers: long list using the NSS Cipher Suite name with allowWeakCipher on')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers',
      '-TLS_RSA_WITH_NULL_MD5,+TLS_RSA_WITH_RC4_128_MD5,+TLS_RSA_EXPORT_WITH_RC4_40_MD5,+TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5,+TLS_DHE_RSA_WITH_DES_CBC_SHA,+SSL_RSA_FIPS_WITH_DES_CBC_SHA,+TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA,+SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA,+TLS_RSA_EXPORT1024_WITH_RC4_56_SHA,+TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA,-SSL_CK_RC4_128_WITH_MD5,-SSL_CK_RC4_128_EXPORT40_WITH_MD5,-SSL_CK_RC2_128_CBC_WITH_MD5,-SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5,-SSL_CK_DES_64_CBC_WITH_MD5,-SSL_CK_DES_192_EDE3_CBC_WITH_MD5')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_9' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
    global plus_all_ecount
    global plus_all_dcount
    if nss_version >= NSS320:
        assert ecount == 5
    else:
        assert ecount == 9
    assert dcount == 0
    weak = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)

    topology.standalone.log.info("ticket47838 was successfully verified.")

    comp_nsSSLEnableCipherCount(topology, ecount)


def test_47838_run_11(topology):
    """
    Check nsSSL3Ciphers: +fortezza
    SSL_GetImplementedCiphers does not return this as a secuire cipher suite
    """
    _header(topology, 'Test Case 12 - Check nsSSL3Ciphers: +fortezza, which is not supported')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+fortezza')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_10' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    errmsg = os.popen('egrep "SSL alert:" %s | egrep "is not available in NSS"' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected error message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected error message was not found")
        assert False

    comp_nsSSLEnableCipherCount(topology, 0)


def test_47928_run_0(topology):
    """
    No SSL version config parameters.
    Check SSL3 (TLS1.0) is off.
    """
    _header(topology, 'Test Case 13 - No SSL version config parameters')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    # add them once and remove them
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3', 'off'),
                                                 (ldap.MOD_REPLACE, 'nsTLS1', 'on'),
                                                 (ldap.MOD_REPLACE, 'sslVersionMin', 'TLS1.1'),
                                                 (ldap.MOD_REPLACE, 'sslVersionMax', 'TLS1.2')])
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_DELETE, 'nsSSL3', None),
                                                 (ldap.MOD_DELETE, 'nsTLS1', None),
                                                 (ldap.MOD_DELETE, 'sslVersionMin', None),
                                                 (ldap.MOD_DELETE, 'sslVersionMax', None)])
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '64')])
    time.sleep(5)

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_11' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    errmsg = os.popen('egrep "SSL alert:" %s | egrep "Default SSL Version settings; Configuring the version range as min: TLS1.1"' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False


def test_47928_run_1(topology):
    """
    No nsSSL3, nsTLS1; sslVersionMin > sslVersionMax
    Check sslVersionMax is ignored.
    """
    _header(topology, 'Test Case 14 - No nsSSL3, nsTLS1; sslVersionMin > sslVersionMax')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'sslVersionMin', 'TLS1.2'),
                                                 (ldap.MOD_REPLACE, 'sslVersionMax', 'TLS1.1')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_12' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    errmsg = os.popen('egrep "SSL alert:" %s | egrep "The min value of NSS version range"' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False

    errmsg = os.popen('egrep "SSL Initialization" %s | egrep "Configured SSL version range: min: TLS1.2, max: TLS1"' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False


def test_47928_run_2(topology):
    """
    nsSSL3: on; sslVersionMin: TLS1.1; sslVersionMax: TLS1.2
    Conflict between nsSSL3 and range; nsSSL3 is disabled
    """
    _header(topology, 'Test Case 15 - nsSSL3: on; sslVersionMin: TLS1.1; sslVersionMax: TLS1.2')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'sslVersionMin', 'TLS1.1'),
                                                 (ldap.MOD_REPLACE, 'sslVersionMax', 'TLS1.2'),
                                                 (ldap.MOD_REPLACE, 'nsSSL3', 'on')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_13' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    errmsg = os.popen('egrep "SSL alert:" %s | egrep "Found unsecure configuration: nsSSL3: on"' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False

    errmsg = os.popen('egrep "SSL alert:" %s | egrep "Respect the supported range."' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False

    errmsg = os.popen('egrep "SSL Initialization" %s | egrep "Configured SSL version range: min: TLS1.1, max: TLS1"' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False


def test_47928_run_3(topology):
    """
    nsSSL3: on; nsTLS1: off; sslVersionMin: TLS1.1; sslVersionMax: TLS1.2
    Conflict between nsSSL3/nsTLS1 and range; nsSSL3 is disabled; nsTLS1 is enabled.
    """
    _header(topology, 'Test Case 16 - nsSSL3: on; nsTLS1: off; sslVersionMin: TLS1.1; sslVersionMax: TLS1.2')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'sslVersionMin', 'TLS1.1'),
                                                 (ldap.MOD_REPLACE, 'sslVersionMax', 'TLS1.2'),
                                                 (ldap.MOD_REPLACE, 'nsSSL3', 'on'),
                                                 (ldap.MOD_REPLACE, 'nsTLS1', 'off')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_14' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    errmsg = os.popen('egrep "SSL alert:" %s | egrep "Found unsecure configuration: nsSSL3: on"' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False

    errmsg = os.popen('egrep "SSL alert:" %s | egrep "Respect the configured range."' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False

    errmsg = os.popen('egrep "SSL Initialization" %s | egrep "Configured SSL version range: min: TLS1.1, max: TLS1"' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected message was not found")
        assert False


def test_47838_run_last(topology):
    """
    Check nsSSL3Ciphers: all <== invalid value
    All ciphers are disabled.
    """
    _header(topology, 'Test Case 17 - Check nsSSL3Ciphers: all, which is invalid')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', None)])
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', 'all')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_15' % (topology.standalone.errlog, topology.standalone.errlog))
    os.system('touch %s' % (topology.standalone.errlog))
    topology.standalone.start(timeout=120)

    errmsg = os.popen('egrep "SSL alert:" %s | egrep "invalid ciphers"' % topology.standalone.errlog)
    if errmsg != "":
        log.info("Expected error message:")
        log.info("%s" % errmsg.readline())
    else:
        log.info("Expected error message was not found")
        assert False

    comp_nsSSLEnableCipherCount(topology, 0)

    topology.standalone.log.info("ticket47838, 47880, 47908, 47928 were successfully verified.")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
