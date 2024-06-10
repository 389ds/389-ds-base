# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import os
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2
from lib389._constants import *
from lib389.replica import ReplicationManager
from lib389.config import CertmapLegacy, Config
from lib389.idm.user import UserAccounts
from lib389.nss_ssl import NssSsl, USER_ISSUER

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.3.5'), reason="Not implemented")]

DEBUGGING = os.getenv("DEBUGGING", default=False)
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

test_user_uid = 1000

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
    log.info("######################### Relocate PEM files on supplier1 ######################")
    certdir_prefix = "/dev/shm"
    mycacert = os.path.join(certdir_prefix, "MyCA")
    topology_m2.ms["supplier1"].encryption.set('CACertExtractFile', mycacert)
    myservercert = os.path.join(certdir_prefix, "MyServerCert1")
    myserverkey = os.path.join(certdir_prefix, "MyServerKey1")
    topology_m2.ms["supplier1"].rsa.apply_mods([(ldap.MOD_REPLACE, 'ServerCertExtractFile', myservercert),
                                              (ldap.MOD_REPLACE, 'ServerKeyExtractFile', myserverkey)])
    log.info("##### restart supplier1")
    topology_m2.ms["supplier1"].restart()
    check_pems(certdir_prefix, mycacert, myservercert, myserverkey, "")

def test_openldap_no_nss_crypto(topology_m2):
    """Check that we allow usage of OpenLDAP libraries
    that don't use NSS for crypto

    :id: 0a622f3d-8ba5-4df2-a1de-1fb2237da40a
    :setup: Replication with two suppliers:
        supplier_1 ----- startTLS -----> supplier_2;
        supplier_1 <-- TLS_clientAuth -- supplier_2;
        nsslapd-extract-pemfiles set to 'on' on both suppliers
        without specifying cert names
    :steps:
        1. Add 5 users to supplier 1 and 2
        2. Check that the users were successfully replicated
        3. Relocate PEM files on supplier 1
        4. Check PEM files in supplier 1 config directory
        5. Add 5 users more to supplier 1 and 2
        6. Check that the users were successfully replicated
        7. Export userRoot on supplier 1
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

    m1 = topology_m2.ms["supplier1"]
    m2 = topology_m2.ms["supplier2"]
    [i.enable_tls() for i in topology_m2]
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication(m1, m2)

    add_entry(m1, 'supplier1', 'uid=m1user', 0, 5)
    add_entry(m2, 'supplier2', 'uid=m2user', 0, 5)
    repl.wait_for_replication(m1, m2)
    repl.wait_for_replication(m2, m1)

    log.info('##### Searching for entries on supplier1...')
    entries = m1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 11 == len(entries)

    log.info('##### Searching for entries on supplier2...')
    entries = m2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 11 == len(entries)

    relocate_pem_files(topology_m2)

    add_entry(m1, 'supplier1', 'uid=m1user', 10, 5)
    add_entry(m2, 'supplier2', 'uid=m2user', 10, 5)

    repl.wait_for_replication(m1, m2)
    repl.wait_for_replication(m2, m1)

    log.info('##### Searching for entries on supplier1...')
    entries = m1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 21 == len(entries)

    log.info('##### Searching for entries on supplier2...')
    entries = m2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 21 == len(entries)

    output_file = os.path.join(m1.get_ldif_dir(), "supplier1.ldif")
    m1.tasks.exportLDIF(benamebase='userRoot', output_file=output_file, args={'wait': True})

    log.info("Ticket 47536 - PASSED")


def create_sasl_user(inst, users, ssca, cn, pw=PW_DM):
    # Create a dict with user data needed for certificate authentication as an user

    global test_user_uid

    assert cn.startswith('test_')
    subjectDN = USER_ISSUER.format(HOSTNAME=cn)
    properties = {
        'uid': cn,
        'cn': cn,
        'sn': cn,
        'uidNumber': str(test_user_uid),
        'gidNumber': '2000',
        'homeDirectory': '/home/' + cn,
        'userPassword': pw,
        'nsCertSubjectDN': subjectDN,
    }
    user = users.create(properties=properties)
    test_user_uid += 1

    # Create user's certificate
    ssca.create_rsa_user(cn)

    # Get the details of where the key and crt are.
    tls_locs = ssca.get_rsa_user(cn)
    user.enroll_certificate(tls_locs['crt_der_path'])

    return { 'user': user,
             'subjectDN': subjectDN,
             'ssca_dir': inst.get_ssca_dir(),
             'tls_locs': tls_locs,
             'key': tls_locs['key'],
             'crt': tls_locs['crt'],
             'cn': cn,
           }


def remove_test_users(inst):
    if inst.state == DIRSRV_STATE_ONLINE:
        users = UserAccounts(inst, DEFAULT_SUFFIX)
        for user in users.list():
            if user.rdn.startswith('test_'):
                user.delete()


def rebind(inst):
    inst.simple_bind_s(DN_DM, PW_DM, escapehatch='i am sure')


def sasl_bind_as_user(inst, user):
    # Bind with user then rebind as Directory Manager
    dn = user["subjectDN"]
    log.info(f'Trying to bind with {dn} certificate')
    try:
        inst.open(saslmethod='EXTERNAL', connOnly=True, certdir=user['ssca_dir'],
                  userkey=user['key'], usercert=user['crt'])
        log.info(f'Bind with {dn} certificate is successful.')
        rebind(inst)
    except ldap.LDAPError as e:
        log.error(f'Bind with {dn} certificate failed. Error is: {e}.')
        rebind(inst)
        raise e


def test_bind_certifcate_with_unescaped_subject(topology_m2, request):
    """Test close connection on failed bind with a failed cert mapping

    :id: 7545ad30-21dc-11ef-a27e-482ae39447e5
    :setup: Replication with two suppliers
            (Only one instance is used but it is better to reuse the
            same topology than the other tests in this module)
    :steps:
        1. perform cleanup and initialize security framework
        2. create user1 and its associated certificate
        3. create user2 and its associated certificate. user2 bane include a wildchar
        4. Check that subtring searches are working as expected
        5. configure certmap and set nsslapd-certmap-basedn
        6. check that EXTERNAL is listed in supported mechns.
        7. bind using user1 certificate
        8. bind using user2 certificate
        9. delete user2 but keep its certificates
        10. bind using user2 certificate
    :expectedresults:
        1. success
        2. success
        3. success
        4. success
        5. success
        6. success
        7. success
        8. success
        9. success
        10. should get INVALID_CREDENTIALS exception
    """

    # Perform some cleanup and initialize the security framework
    inst = topology_m2.ms["supplier1"]
    rebind(inst)
    inst.enable_tls()
    ssca = NssSsl(dbpath=inst.get_ssca_dir())
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    remove_test_users(inst)

    # Prepare to update certmap.conf file
    cm = CertmapLegacy(inst)
    certmaps = cm.list()
    previous_certmaps_default = certmaps['default']
    log.info(f'previous_certmaps_default = {previous_certmaps_default}')

    # register cleanup finalizer before actually changing the certmap.conf file
    def fin():
        if inst.status():
            rebind(inst)
        # Restore certmaps
        if not DEBUGGING:
            remove_test_users(inst)
            certmaps['default'] = previous_certmaps_default
            cm.set(certmaps)

    request.addfinalizer(fin)

    # update the certmap.conf file
    certmaps['default'] = {
        **{ key: None for key in previous_certmaps_default.keys() },
        'VerifyCert': 'off',
        'DNComps': '',
        'CmapLdapAttr': 'nsCertSubjectDN',
        'issuer': 'default'
    }
    log.info(f"certmaps_default = {certmaps['default']}")
    cm.set(certmaps)

    # And set nsslapd-certmap-basedn properly
    config = Config(inst)
    config.replace('nsslapd-certmap-basedn', DEFAULT_SUFFIX)

    # Check that EXTERNAL is listed in supported mechns.
    assert(inst.rootdse.supports_sasl_external())

    # Restart to allow certmaps to be re-read
    inst.restart()

    # create users
    user1 = create_sasl_user(inst, users, ssca, 'test_user_1000')
    user2 = create_sasl_user(inst, users, ssca, 'test_user_1*')

    # Check that subtring searches are working as expected
    # ( This step because 'test_u*_1000' hits
    #   https://github.com/389ds/389-ds-base/issues/6203 )
    for cn,result_len in ( (user1['cn'], 1), (user2['cn'], 2), ('*', 2), ):
        filterstr = f'(nsCertSubjectDN={USER_ISSUER.format(HOSTNAME=cn)})'
        ents = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filterstr, escapehatch='i am sure')
        log.info(f'Search with filter {filterstr} returned {len(ents)} entries. {result_len} are expected.')
        assert len(ents) == result_len

    # bind with user1 cert (Should be successful)
    sasl_bind_as_user(inst, user1)

    # bind with user2 cert (Should be successful)
    sasl_bind_as_user(inst, user1)

    # Remove user2 entry (but keep its certificate)
    user2['user'].delete()

    # bind with user2 certificate (Should fail)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        sasl_bind_as_user(inst, user2)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode

    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
