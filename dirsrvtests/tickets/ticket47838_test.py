import os
import sys
import time
import ldap
import logging
import socket
import pytest
import shutil
from lib389 import DirSrv, Entry, tools
from lib389 import DirSrvTools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from constants import *

log = logging.getLogger(__name__)

installation_prefix = None

CONFIG_DN = 'cn=config'
ENCRYPTION_DN = 'cn=encryption,%s' % CONFIG_DN
RSA = 'RSA'
RSA_DN = 'cn=%s,%s' % (RSA, ENCRYPTION_DN)
LDAPSPORT = '10636'
SERVERCERT = 'Server-Cert'
plus_all_ecount = 0
plus_all_dcount = 0
plus_all_ecount_noweak = 0
plus_all_dcount_noweak = 0

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone

@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
        At the beginning, It may exists a standalone instance.
        It may also exists a backup for the standalone instance.

        Principle:
            If standalone instance exists:
                restart it
            If backup of standalone exists:
                create/rebind to standalone

                restore standalone instance from backup
            else:
                Cleanup everything
                    remove instance
                    remove backup
                Create instance
                Create backup
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

    # Get the status of the backups
    backup_standalone = standalone.checkBackupFS()

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()
    if instance_standalone:
        # assuming the instance is already stopped, just wait 5 sec max
        standalone.stop(timeout=5)
        try:
            standalone.start(timeout=10)
        except ldap.SERVER_DOWN:
            pass

    if backup_standalone:
        # The backup exist, assuming it is correct
        # we just re-init the instance with it
        if not instance_standalone:
            standalone.create()
            # Used to retrieve configuration information (dbdir, confdir...)
            standalone.open()

        # restore standalone instance from backup
        standalone.stop(timeout=10)
        standalone.restoreFS(backup_standalone)
        standalone.start(timeout=10)

    else:
        # We should be here only in two conditions
        #      - This is the first time a test involve standalone instance
        #      - Something weird happened (instance/backup destroyed)
        #        so we discard everything and recreate all

        # Remove the backup. So even if we have a specific backup file
        # (e.g backup_standalone) we clear backup that an instance may have created
        if backup_standalone:
            standalone.clearBackupFS()

        # Remove the instance
        if instance_standalone:
            standalone.delete()

        # Create the instance
        standalone.create()

        # Used to retrieve configuration information (dbdir, confdir...)
        standalone.open()

        # Time to create the backups
        standalone.stop(timeout=10)
        standalone.backupfile = standalone.backupFS()
        standalone.start(timeout=10)

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    #
    # Here we have standalone instance up and running
    # Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyStandalone(standalone)

def _header(topology, label):
    topology.standalone.log.info("\n\n###############################################")
    topology.standalone.log.info("#######")
    topology.standalone.log.info("####### %s" % label)
    topology.standalone.log.info("#######")
    topology.standalone.log.info("###############################################")

def test_ticket47838_init(topology):
    """
    Generate self signed cert and import it to the DS cert db.
    Enable SSL
    """
    _header(topology, 'Testing Ticket 47838 - harden the list of ciphers available by default')

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
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3', 'on'),
                                                 (ldap.MOD_REPLACE, 'nsSSLClientAuth', 'allowed'),
                                                 (ldap.MOD_REPLACE, 'allowWeakCipher', 'on'),
                                                 (ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+all')])

    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-security', 'on'),
                                             (ldap.MOD_REPLACE, 'nsslapd-ssl-check-hostname', 'off'),
                                             (ldap.MOD_REPLACE, 'nsslapd-secureport', LDAPSPORT)])

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
        if attrs.has_key('nsSSLEnabledCiphers'):
            enabledciphercnt = len(attrs['nsSSLEnabledCiphers'])
    topology.standalone.log.info("enabledCipherCount: %d" % enabledciphercnt)
    assert ecount == enabledciphercnt

def test_ticket47838_run_0(topology):
    """
    Check nsSSL3Ciphers: +all
    All ciphers are enabled except null.
    Note: allowWeakCipher: on
    """
    _header(topology, 'Test Case 1 - Check the ciphers availability for "+all"; allowWeakCipher: on')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '64')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.restart(timeout=120)

    enabled = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | wc -l' % topology.standalone.errlog)
    disabled = os.popen('egrep "SSL alert:" %s | egrep \": disabled\" | wc -l' % topology.standalone.errlog)
    ecount = int(enabled.readline().rstrip())
    dcount = int(disabled.readline().rstrip())

    log.info("Enabled ciphers: %d" % ecount)
    log.info("Disabled ciphers: %d" % dcount)
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

def test_ticket47838_run_1(topology):
    """
    Check nsSSL3Ciphers: +all
    All ciphers are enabled except null.
    Note: default allowWeakCipher (i.e., off) for +all
    """
    _header(topology, 'Test Case 2 - Check the ciphers availability for "+all" with default allowWeakCiphers')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '64')])
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

def test_ticket47838_run_2(topology):
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

def test_ticket47838_run_3(topology):
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

def test_ticket47838_run_4(topology):
    """
    Check no nsSSL3Ciphers
    Default ciphers are enabled.
    default allowWeakCipher
    """
    _header(topology, 'Test Case 5 - Check no nssSSL3Chiphers (default setting) with default allowWeakCipher')

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
    assert ecount == 12
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)
    weak = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    assert wcount == 0

    comp_nsSSLEnableCipherCount(topology, ecount)

def test_ticket47838_run_5(topology):
    """
    Check nsSSL3Ciphers: default
    Default ciphers are enabled.
    default allowWeakCipher
    """
    _header(topology, 'Test Case 6 - Check default nssSSL3Chiphers (default setting) with default allowWeakCipher')

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
    assert ecount == 12
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)
    weak = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    assert wcount == 0

    comp_nsSSLEnableCipherCount(topology, ecount)

def test_ticket47838_run_6(topology):
    """
    Check nssSSL3Chiphers: +all,-rsa_rc4_128_md5
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology, 'Test Case 7 - Check nssSSL3Chiphers: +all,-tls_dhe_rsa_aes_128_gcm_sha with default allowWeakCipher')

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

def test_ticket47838_run_7(topology):
    """
    Check nssSSL3Chiphers: -all,+rsa_rc4_128_md5
    All ciphers are disabled.
    default allowWeakCipher
    """
    _header(topology, 'Test Case 8 - Check nssSSL3Chiphers: -all,+rsa_rc4_128_md5 with default allowWeakCipher')

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

def test_ticket47838_run_8(topology):
    """
    Check nsSSL3Ciphers: default + allowWeakCipher: off
    Strong Default ciphers are enabled.
    """
    _header(topology, 'Test Case 9 - Check default nssSSL3Chiphers (default setting + allowWeakCipher: off)')

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
    assert ecount == 12
    assert dcount == (plus_all_ecount + plus_all_dcount - ecount)
    weak = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    assert wcount == 0

    comp_nsSSLEnableCipherCount(topology, ecount)

def test_ticket47838_run_9(topology):
    """
    Check no nsSSL3Ciphers
    Default ciphers are enabled.
    allowWeakCipher: on
    nsslapd-errorlog-level: 0
    """
    _header(topology, 'Test Case 10 - Check no nssSSL3Chiphers (default setting) with no errorlog-level & allowWeakCipher on')

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
    assert ecount == 23
    assert dcount == 0
    weak = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)
    assert wcount == 11

    comp_nsSSLEnableCipherCount(topology, ecount)

def test_ticket47838_run_10(topology):
    """
    Check nssSSL3Chiphers: -TLS_RSA_WITH_NULL_MD5,+TLS_RSA_WITH_RC4_128_MD5,
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
    _header(topology, 'Test Case 11 - Check nssSSL3Chiphers: long list using the NSS Cipher Suite name with allowWeakCipher on')

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
    assert ecount == 9
    assert dcount == 0
    weak = os.popen('egrep "SSL alert:" %s | egrep \": enabled\" | egrep "WEAK CIPHER" | wc -l' % topology.standalone.errlog)
    wcount = int(weak.readline().rstrip())
    log.info("Weak ciphers in the default setting: %d" % wcount)

    topology.standalone.log.info("ticket47838 was successfully verified.");

    comp_nsSSLEnableCipherCount(topology, ecount)

def test_ticket47838_run_11(topology):
    """
    Check nssSSL3Chiphers: +fortezza
    SSL_GetImplementedCiphers does not return this as a secuire cipher suite
    """
    _header(topology, 'Test Case 12 - Check nssSSL3Chiphers: +fortezza, which is not supported')

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

def test_ticket47838_run_last(topology):
    """
    Check nssSSL3Chiphers: all <== invalid value
    All ciphers are disabled.
    """
    _header(topology, 'Test Case 13 - Check nssSSL3Chiphers: all, which is invalid')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', 'all')])

    log.info("\n######################### Restarting the server ######################\n")
    topology.standalone.stop(timeout=10)
    os.system('mv %s %s.47838_10' % (topology.standalone.errlog, topology.standalone.errlog))
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

    topology.standalone.log.info("ticket47838, 47880, 47908 were successfully verified.");

def test_ticket47838_final(topology):
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', None)])
    topology.standalone.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3Ciphers', 'default'),
                                                 (ldap.MOD_REPLACE, 'allowWeakCipher', 'on')])
    topology.standalone.stop(timeout=10)

def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix = None

    topo = topology(True)
    test_ticket47838_init(topo)
    
    test_ticket47838_run_0(topo)
    test_ticket47838_run_1(topo)
    test_ticket47838_run_2(topo)
    test_ticket47838_run_3(topo)
    test_ticket47838_run_4(topo)
    test_ticket47838_run_5(topo)
    test_ticket47838_run_6(topo)
    test_ticket47838_run_7(topo)
    test_ticket47838_run_8(topo)
    test_ticket47838_run_9(topo)
    test_ticket47838_run_10(topo)
    test_ticket47838_run_11(topo)

    test_ticket47838_run_last(topo)
    
    test_ticket47838_final(topo)

if __name__ == '__main__':
    run_isolated()
