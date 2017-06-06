# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import base64
import os
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2

pytestmark = pytest.mark.skipif(ds_is_older('1.3.5'), reason="Not implemented")

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
ENCRYPTION_DN = 'cn=encryption,%s' % CONFIG_DN
RSA = 'RSA'
RSA_DN = 'cn=%s,%s' % (RSA, ENCRYPTION_DN)
ISSUER = 'cn=CAcert'
CACERT = 'CAcertificate'
M1SERVERCERT = 'Server-Cert1'
M2SERVERCERT = 'Server-Cert2'
M1LDAPSPORT = '41636'
M2LDAPSPORT = '42636'
M1SUBJECT = 'CN=' + os.uname()[1] + ',OU=389 Directory Server'
M2SUBJECT = 'CN=' + os.uname()[1] + ',OU=390 Directory Server'


def add_entry(server, name, rdntmpl, start, num):
    log.info("\n######################### Adding %d entries to %s ######################\n" % (num, name))

    for i in range(num):
        ii = start + i
        dn = '%s%d,%s' % (rdntmpl, ii, DEFAULT_SUFFIX)
        server.add_s(Entry((dn, {'objectclass': 'top person extensibleObject'.split(),
                                 'uid': '%s%d' % (rdntmpl, ii),
                                 'cn': '%s user%d' % (name, ii),
                                 'sn': 'user%d' % (ii)})))


def enable_ssl(server, ldapsport, mycert):
    log.info("\n######################### Enabling SSL LDAPSPORT %s ######################\n" % ldapsport)
    server.simple_bind_s(DN_DM, PASSWORD)
    server.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3', 'off'),
                                    (ldap.MOD_REPLACE, 'nsTLS1', 'on'),
                                    (ldap.MOD_REPLACE, 'nsSSLClientAuth', 'allowed'),
                                    (ldap.MOD_REPLACE, 'nsSSL3Ciphers', '+all')])

    server.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-security', 'on'),
                                (ldap.MOD_REPLACE, 'nsslapd-ssl-check-hostname', 'off'),
                                (ldap.MOD_REPLACE, 'nsslapd-secureport', ldapsport)])

    server.add_s(Entry((RSA_DN, {'objectclass': "top nsEncryptionModule".split(),
                                 'cn': RSA,
                                 'nsSSLPersonalitySSL': mycert,
                                 'nsSSLToken': 'internal (software)',
                                 'nsSSLActivation': 'on'})))


def check_pems(confdir, mycacert, myservercert, myserverkey, notexist):
    log.info("\n######################### Check PEM files (%s, %s, %s)%s in %s ######################\n"
             % (mycacert, myservercert, myserverkey, notexist, confdir))
    global cacert
    cacert = '%s/%s.pem' % (confdir, mycacert)
    if os.path.isfile(cacert):
        if notexist == "":
            log.info('%s is successfully generated.' % cacert)
        else:
            log.info('%s is incorrecly generated.' % cacert)
            assert False
    else:
        if notexist == "":
            log.fatal('%s is not generated.' % cacert)
            assert False
        else:
            log.info('%s is correctly not generated.' % cacert)
    servercert = '%s/%s.pem' % (confdir, myservercert)
    if os.path.isfile(servercert):
        if notexist == "":
            log.info('%s is successfully generated.' % servercert)
        else:
            log.info('%s is incorrecly generated.' % servercert)
            assert False
    else:
        if notexist == "":
            log.fatal('%s was not generated.' % servercert)
            assert False
        else:
            log.info('%s is correctly not generated.' % servercert)
    serverkey = '%s/%s.pem' % (confdir, myserverkey)
    if os.path.isfile(serverkey):
        if notexist == "":
            log.info('%s is successfully generated.' % serverkey)
        else:
            log.info('%s is incorrectly generated.' % serverkey)
            assert False
    else:
        if notexist == "":
            log.fatal('%s was not generated.' % serverkey)
            assert False
        else:
            log.info('%s is correctly not generated.' % serverkey)


def doAndPrintIt(cmdline):
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    log.info("      OUT:")
    while True:
        l = proc.stdout.readline()
        if l == "":
            break
        log.info("      %s" % l)
    log.info("      ERR:")
    while True:
        l = proc.stderr.readline()
        if l == "" or l == "\n":
            break
        log.info("      <%s>" % l)
        assert False


def create_keys_certs(topology_m2):
    log.info("\n######################### Creating SSL Keys and Certs ######################\n")

    global m1confdir
    m1confdir = topology_m2.ms["master1"].confdir
    global m2confdir
    m2confdir = topology_m2.ms["master2"].confdir

    log.info("##### shutdown master1")
    topology_m2.ms["master1"].stop(timeout=10)

    log.info("##### Creating a password file")
    pwdfile = '%s/pwdfile.txt' % (m1confdir)
    os.system('rm -f %s' % pwdfile)
    opasswd = os.popen("(ps -ef ; w ) | sha1sum | awk '{print $1}'", "r")
    passwd = opasswd.readline()
    pwdfd = open(pwdfile, "w")
    pwdfd.write(passwd)
    pwdfd.close()

    log.info("##### create the pin file")
    m1pinfile = '%s/pin.txt' % (m1confdir)
    m2pinfile = '%s/pin.txt' % (m2confdir)
    os.system('rm -f %s' % m1pinfile)
    os.system('rm -f %s' % m2pinfile)
    pintxt = 'Internal (Software) Token:%s' % passwd
    pinfd = open(m1pinfile, "w")
    pinfd.write(pintxt)
    pinfd.close()
    os.system('chmod 400 %s' % m1pinfile)

    log.info("##### Creating a noise file")
    noisefile = '%s/noise.txt' % (m1confdir)
    noise = os.popen("(w ; ps -ef ; date ) | sha1sum | awk '{print $1}'", "r")
    noisewdfd = open(noisefile, "w")
    noisewdfd.write(noise.readline())
    noisewdfd.close()
    time.sleep(1)

    cmdline = ['certutil', '-N', '-d', m1confdir, '-f', pwdfile]
    log.info("##### Create key3.db and cert8.db database (master1): %s" % cmdline)
    doAndPrintIt(cmdline)

    cmdline = ['certutil', '-G', '-d', m1confdir, '-z', noisefile, '-f', pwdfile]
    log.info("##### Creating encryption key for CA (master1): %s" % cmdline)
    # os.system('certutil -G -d %s -z %s -f %s' % (m1confdir, noisefile, pwdfile))
    doAndPrintIt(cmdline)

    time.sleep(2)

    log.info("##### Creating self-signed CA certificate (master1) -- nickname %s" % CACERT)
    os.system(
        '( echo y ; echo ; echo y ) | certutil -S -n "%s" -s "%s" -x -t "CT,," -m 1000 -v 120 -d %s -z %s -f %s -2' % (
        CACERT, ISSUER, m1confdir, noisefile, pwdfile))

    global M1SUBJECT
    cmdline = ['certutil', '-S', '-n', M1SERVERCERT, '-s', M1SUBJECT, '-c', CACERT, '-t', ',,', '-m', '1001', '-v',
               '120', '-d', m1confdir, '-z', noisefile, '-f', pwdfile]
    log.info("##### Creating Server certificate -- nickname %s: %s" % (M1SERVERCERT, cmdline))
    doAndPrintIt(cmdline)

    time.sleep(2)

    global M2SUBJECT
    cmdline = ['certutil', '-S', '-n', M2SERVERCERT, '-s', M2SUBJECT, '-c', CACERT, '-t', ',,', '-m', '1002', '-v',
               '120', '-d', m1confdir, '-z', noisefile, '-f', pwdfile]
    log.info("##### Creating Server certificate -- nickname %s: %s" % (M2SERVERCERT, cmdline))
    doAndPrintIt(cmdline)

    time.sleep(2)

    log.info("##### start master1")
    topology_m2.ms["master1"].start(timeout=10)

    log.info("##### enable SSL in master1 with all ciphers")
    enable_ssl(topology_m2.ms["master1"], M1LDAPSPORT, M1SERVERCERT)

    cmdline = ['certutil', '-L', '-d', m1confdir]
    log.info("##### Check the cert db: %s" % cmdline)
    doAndPrintIt(cmdline)

    log.info("##### restart master1")
    topology_m2.ms["master1"].restart(timeout=10)

    log.info("##### Check PEM files of master1 (before setting nsslapd-extract-pemfiles")
    check_pems(m1confdir, CACERT, M1SERVERCERT, M1SERVERCERT + '-Key', " not")

    log.info("##### Set on to nsslapd-extract-pemfiles")
    topology_m2.ms["master1"].modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-extract-pemfiles', 'on')])

    log.info("##### restart master1")
    topology_m2.ms["master1"].restart(timeout=10)

    log.info("##### Check PEM files of master1 (after setting nsslapd-extract-pemfiles")
    check_pems(m1confdir, CACERT, M1SERVERCERT, M1SERVERCERT + '-Key', "")

    global mytmp
    mytmp = '/tmp'
    m2pk12file = '%s/%s.pk12' % (mytmp, M2SERVERCERT)
    cmd = 'pk12util -o %s -n "%s" -d %s -w %s -k %s' % (m2pk12file, M2SERVERCERT, m1confdir, pwdfile, pwdfile)
    log.info("##### Extract PK12 file for master2: %s" % cmd)
    os.system(cmd)

    log.info("##### Check PK12 files")
    if os.path.isfile(m2pk12file):
        log.info('%s is successfully extracted.' % m2pk12file)
    else:
        log.fatal('%s was not extracted.' % m2pk12file)
        assert False

    log.info("##### stop master2")
    topology_m2.ms["master2"].stop(timeout=10)

    log.info("##### Initialize Cert DB for master2")
    cmdline = ['certutil', '-N', '-d', m2confdir, '-f', pwdfile]
    log.info("##### Create key3.db and cert8.db database (master2): %s" % cmdline)
    doAndPrintIt(cmdline)

    log.info("##### Import certs to master2")
    log.info('Importing %s' % CACERT)
    global cacert
    os.system('certutil -A -n "%s" -t "CT,," -f %s -d %s -a -i %s' % (CACERT, pwdfile, m2confdir, cacert))
    cmd = 'pk12util -i %s -n "%s" -d %s -w %s -k %s' % (m2pk12file, M2SERVERCERT, m2confdir, pwdfile, pwdfile)
    log.info('##### Importing %s to master2: %s' % (M2SERVERCERT, cmd))
    os.system(cmd)
    log.info('copy %s to %s' % (m1pinfile, m2pinfile))
    os.system('cp %s %s' % (m1pinfile, m2pinfile))
    os.system('chmod 400 %s' % m2pinfile)

    log.info("##### start master2")
    topology_m2.ms["master2"].start(timeout=10)

    log.info("##### enable SSL in master2 with all ciphers")
    enable_ssl(topology_m2.ms["master2"], M2LDAPSPORT, M2SERVERCERT)

    log.info("##### restart master2")
    topology_m2.ms["master2"].restart(timeout=10)

    log.info("##### Check PEM files of master2 (before setting nsslapd-extract-pemfiles")
    check_pems(m2confdir, CACERT, M2SERVERCERT, M2SERVERCERT + '-Key', " not")

    log.info("##### Set on to nsslapd-extract-pemfiles")
    topology_m2.ms["master2"].modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-extract-pemfiles', 'on')])

    log.info("##### restart master2")
    topology_m2.ms["master2"].restart(timeout=10)

    log.info("##### Check PEM files of master2 (after setting nsslapd-extract-pemfiles")
    check_pems(m2confdir, CACERT, M2SERVERCERT, M2SERVERCERT + '-Key', "")

    log.info("##### restart master1")
    topology_m2.ms["master1"].restart(timeout=10)

    log.info("\n######################### Creating SSL Keys and Certs Done ######################\n")


def config_tls_agreements(topology_m2):
    log.info("######################### Configure SSL/TLS agreements ######################")
    log.info("######################## master1 -- startTLS -> master2 #####################")
    log.info("##################### master1 <- tls_clientAuth -- master2 ##################")

    log.info("##### Update the agreement of master1")
    m1_m2_agmt = topology_m2.ms["master1_agmts"]["m1_m2"]
    topology_m2.ms["master1"].modify_s(m1_m2_agmt, [(ldap.MOD_REPLACE, 'nsDS5ReplicaTransportInfo', 'TLS')])

    log.info("##### Add the cert to the repl manager on master1")
    global mytmp
    global m2confdir
    m2servercert = '%s/%s.pem' % (m2confdir, M2SERVERCERT)
    m2sc = open(m2servercert, "r")
    m2servercertstr = ''
    for l in m2sc.readlines():
        if ((l == "") or l.startswith('This file is auto-generated') or
                l.startswith('Do not edit') or l.startswith('Issuer:') or
                l.startswith('Subject:') or l.startswith('-----')):
            continue
        m2servercertstr = "%s%s" % (m2servercertstr, l.rstrip())
    m2sc.close()

    log.info('##### master2 Server Cert in base64 format: %s' % m2servercertstr)

    replmgr = defaultProperties[REPLICATION_BIND_DN]
    rentry = topology_m2.ms["master1"].search_s(replmgr, ldap.SCOPE_BASE, 'objectclass=*')
    log.info('##### Replication manager on master1: %s' % replmgr)
    oc = 'ObjectClass'
    log.info('      %s:' % oc)
    if rentry:
        for val in rentry[0].getValues(oc):
            log.info('                 : %s' % val)
    topology_m2.ms["master1"].modify_s(replmgr, [(ldap.MOD_ADD, oc, 'extensibleObject')])

    global M2SUBJECT
    topology_m2.ms["master1"].modify_s(replmgr,
                                       [(ldap.MOD_ADD, 'userCertificate;binary', base64.b64decode(m2servercertstr)),
                                        (ldap.MOD_ADD, 'description', M2SUBJECT)])

    log.info("##### Modify the certmap.conf on master1")
    m1certmap = '%s/certmap.conf' % (m1confdir)
    os.system('chmod 660 %s' % m1certmap)
    m1cm = open(m1certmap, "w")
    m1cm.write('certmap Example	%s\n' % ISSUER)
    m1cm.write('Example:DNComps	cn\n')
    m1cm.write('Example:FilterComps\n')
    m1cm.write('Example:verifycert	on\n')
    m1cm.write('Example:CmapLdapAttr	description')
    m1cm.close()
    os.system('chmod 440 %s' % m1certmap)

    log.info("##### Update the agreement of master2")
    m2_m1_agmt = topology_m2.ms["master2_agmts"]["m2_m1"]
    topology_m2.ms["master2"].modify_s(m2_m1_agmt, [(ldap.MOD_REPLACE, 'nsDS5ReplicaTransportInfo', 'TLS'),
                                                    (ldap.MOD_REPLACE, 'nsDS5ReplicaBindMethod', 'SSLCLIENTAUTH')])

    topology_m2.ms["master1"].stop(10)
    topology_m2.ms["master2"].stop(10)
    topology_m2.ms["master1"].start(10)
    topology_m2.ms["master2"].start(10)

    log.info("\n######################### Configure SSL/TLS agreements Done ######################\n")


def relocate_pem_files(topology_m2):
    log.info("######################### Relocate PEM files on master1 ######################")
    mycacert = 'MyCA'
    topology_m2.ms["master1"].modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'CACertExtractFile', mycacert)])
    myservercert = 'MyServerCert1'
    myserverkey = 'MyServerKey1'
    topology_m2.ms["master1"].modify_s(RSA_DN, [(ldap.MOD_REPLACE, 'ServerCertExtractFile', myservercert),
                                                (ldap.MOD_REPLACE, 'ServerKeyExtractFile', myserverkey)])
    log.info("##### restart master1")
    topology_m2.ms["master1"].restart(timeout=10)
    check_pems(m1confdir, mycacert, myservercert, myserverkey, "")


def test_ticket47536(topology_m2):
    """
    Set up 2way MMR:
        master_1 ----- startTLS -----> master_2
        master_1 <-- TLS_clientAuth -- master_2

    Check CA cert, Server-Cert and Key are retrieved as PEM from cert db
    when the server is started.  First, the file names are not specified
    and the default names derived from the cert nicknames.  Next, the
    file names are specified in the encryption config entries.

    Each time add 5 entries to master 1 and 2 and check they are replicated.
    """
    log.info("Ticket 47536 - Allow usage of OpenLDAP libraries that don't use NSS for crypto")

    create_keys_certs(topology_m2)
    config_tls_agreements(topology_m2)

    add_entry(topology_m2.ms["master1"], 'master1', 'uid=m1user', 0, 5)
    add_entry(topology_m2.ms["master2"], 'master2', 'uid=m2user', 0, 5)

    time.sleep(1)

    log.info('##### Searching for entries on master1...')
    entries = topology_m2.ms["master1"].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 10 == len(entries)

    log.info('##### Searching for entries on master2...')
    entries = topology_m2.ms["master2"].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 10 == len(entries)

    relocate_pem_files(topology_m2)

    add_entry(topology_m2.ms["master1"], 'master1', 'uid=m1user', 10, 5)
    add_entry(topology_m2.ms["master2"], 'master2', 'uid=m2user', 10, 5)

    time.sleep(10)

    log.info('##### Searching for entries on master1...')
    entries = topology_m2.ms["master1"].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 20 == len(entries)

    log.info('##### Searching for entries on master2...')
    entries = topology_m2.ms["master2"].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 20 == len(entries)

    output_file = os.path.join(topology_m2.ms["master1"].get_ldif_dir(), "master1.ldif")
    topology_m2.ms["master1"].tasks.exportLDIF(benamebase='userRoot', output_file=output_file, args={'wait': True})

    log.info("Ticket 47536 - PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode

    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
