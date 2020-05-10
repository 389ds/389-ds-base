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
import subprocess
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2
from lib389._constants import *
from lib389.replica import ReplicationManager

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.3.5'), reason="Not implemented")]

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

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


def check_pems(confdir, mycacert, myservercert, myserverkey, notexist):
    log.info("\n######################### Check PEM files (%s, %s, %s)%s in %s ######################\n"
             % (mycacert, myservercert, myserverkey, notexist, confdir))
    global cacert
    cacert = f"{mycacert}.pem"
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
    servercert = f"{myservercert}.pem"
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
    serverkey = f"{myserverkey}.pem"
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


def relocate_pem_files(topology_m2):
    log.info("######################### Relocate PEM files on master1 ######################")
    certdir_prefix = "/dev/shm"
    mycacert = os.path.join(certdir_prefix, "MyCA")
    topology_m2.ms["master1"].encryption.set('CACertExtractFile', mycacert)
    myservercert = os.path.join(certdir_prefix, "MyServerCert1")
    myserverkey = os.path.join(certdir_prefix, "MyServerKey1")
    topology_m2.ms["master1"].rsa.apply_mods([(ldap.MOD_REPLACE, 'ServerCertExtractFile', myservercert),
                                              (ldap.MOD_REPLACE, 'ServerKeyExtractFile', myserverkey)])
    log.info("##### restart master1")
    topology_m2.ms["master1"].restart()
    check_pems(certdir_prefix, mycacert, myservercert, myserverkey, "")

@pytest.mark.ds47536
def test_openldap_no_nss_crypto(topology_m2):
    """Check that we allow usage of OpenLDAP libraries
    that don't use NSS for crypto

    :id: 0a622f3d-8ba5-4df2-a1de-1fb2237da40a
    :setup: Replication with two masters:
        master_1 ----- startTLS -----> master_2;
        master_1 <-- TLS_clientAuth -- master_2;
        nsslapd-extract-pemfiles set to 'on' on both masters
        without specifying cert names
    :steps:
        1. Add 5 users to master 1 and 2
        2. Check that the users were successfully replicated
        3. Relocate PEM files on master 1
        4. Check PEM files in master 1 config directory
        5. Add 5 users more to master 1 and 2
        6. Check that the users were successfully replicated
        7. Export userRoot on master 1
    :expectedresults:
        1. Users should be successfully added
        2. Users should be successfully replicated
        3. Operation should be successful
        4. PEM files should be found
        5. Users should be successfully added
        6. Users should be successfully replicated
        7. Operation should be successful
    """

    log.info("Ticket 47536 - Allow usage of OpenLDAP libraries that don't use NSS for crypto")

    m1 = topology_m2.ms["master1"]
    m2 = topology_m2.ms["master2"]
    [i.enable_tls() for i in topology_m2]
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication(m1, m2)

    add_entry(m1, 'master1', 'uid=m1user', 0, 5)
    add_entry(m2, 'master2', 'uid=m2user', 0, 5)
    repl.wait_for_replication(m1, m2)
    repl.wait_for_replication(m2, m1)

    log.info('##### Searching for entries on master1...')
    entries = m1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 10 == len(entries)

    log.info('##### Searching for entries on master2...')
    entries = m2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 10 == len(entries)

    relocate_pem_files(topology_m2)

    add_entry(m1, 'master1', 'uid=m1user', 10, 5)
    add_entry(m2, 'master2', 'uid=m2user', 10, 5)

    repl.wait_for_replication(m1, m2)
    repl.wait_for_replication(m2, m1)

    log.info('##### Searching for entries on master1...')
    entries = m1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 20 == len(entries)

    log.info('##### Searching for entries on master2...')
    entries = m2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 20 == len(entries)

    output_file = os.path.join(m1.get_ldif_dir(), "master1.ldif")
    m1.tasks.exportLDIF(benamebase='userRoot', output_file=output_file, args={'wait': True})

    log.info("Ticket 47536 - PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode

    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
