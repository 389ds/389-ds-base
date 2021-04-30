# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import pdb
from lib389.utils import ensure_bytes, ds_supports_new_changelog
from lib389.replica import ReplicationManager
from lib389.dseldif import DSEldif
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.topologies import topology_m2
from lib389._constants import *

pytestmark = pytest.mark.tier1

ATTRIBUTE = 'unhashed#user#password'

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def topology_with_tls(topology_m2):
    """Enable TLS on all suppliers"""

    [i.enable_tls() for i in topology_m2]

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication(topology_m2.ms['supplier1'], topology_m2.ms['supplier2'])

    return topology_m2


def _enable_changelog_encryption(inst, encrypt_algorithm):
    """Configure changelog encryption for supplier"""

    dse_ldif = DSEldif(inst)
    log.info('Configuring changelog encryption:{} for: {}'.format(inst.serverid, encrypt_algorithm))
    inst.stop()
    if ds_supports_new_changelog():
        changelog = 'cn=changelog,{}'.format(DN_USERROOT_LDBM)
    else:
        changelog = DN_CHANGELOG

    dse_ldif.replace(changelog, 'nsslapd-encryptionalgorithm', encrypt_algorithm)
    if dse_ldif.get(changelog, 'nsSymmetricKey'):
        dse_ldif.delete(changelog, 'nsSymmetricKey')
    inst.start()


def _check_unhashed_userpw_encrypted(inst, change_type, user_dn, user_pw, is_encrypted):
    """Check if unhashed#user#password attribute value is encrypted or not"""

    if ds_supports_new_changelog():
        log.info('Running dbscan -f to check {} attr'.format(ATTRIBUTE))
        dbscanOut = inst.dbscan(DEFAULT_BENAME, 'replication_changelog')
    else:
        changelog_dbdir = os.path.join(os.path.dirname(inst.dbdir), DEFAULT_CHANGELOG_DB)
        for changelog_dbfile in glob.glob(f'{changelog_dbdir}*/*.db*'):
            log.info('Changelog dbfile file exist: {}'.format(changelog_dbfile))
            log.info('Running dbscan -f to check {} attr'.format(ATTRIBUTE))
            dbscanOut = inst.dbscan(DEFAULT_CHANGELOG_DB, changelog_dbfile)

    count = 0
    for entry in dbscanOut.split(b'dbid: '):
        if ensure_bytes('operation: {}'.format(change_type)) in entry and\
           ensure_bytes(ATTRIBUTE) in entry and ensure_bytes(user_dn.lower()) in entry.lower():
            count += 1
            user_pw_attr = ensure_bytes('{}: {}'.format(ATTRIBUTE, user_pw))
            if is_encrypted:
                assert user_pw_attr not in entry, 'Changelog entry contains clear text password'
            else:
                assert user_pw_attr in entry, 'Changelog entry does not contain clear text password'
    assert count, 'Operation type and DN of the entry not matched in changelog'

#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_algorithm_unhashed(topology_with_tls):
    """Check encryption algorithm AES
    And check unhashed#user#password attribute for encryption.

    :id: b7a37bf8-4b2e-4dbd-9891-70117d67558c
    :parametrized: yes
    :setup: Replication with two suppliers and SSL configured.
    :steps: 1. Enable changelog encrytion on supplier1
            2. Add a user to supplier1/supplier2
            3. Run dbscan -f on m1 to check unhashed#user#password
               attribute is encrypted.
            4. Run dbscan -f on m2 to check unhashed#user#password
               attribute is in cleartext.
            5. Modify password in supplier2/supplier1
            6. Run dbscan -f on m1 to check unhashed#user#password
               attribute is encrypted.
            7. Run dbscan -f on m2 to check unhashed#user#password
               attribute is in cleartext.
    :expectedresults:
            1. It should pass
            2. It should pass
            3. It should pass
            4. It should pass
            5. It should pass
            6. It should pass
            7. It should pass
    """
    encryption = 'AES'
    m1 = topology_with_tls.ms['supplier1']
    m2 = topology_with_tls.ms['supplier2']
    m1.config.set('nsslapd-unhashed-pw-switch', 'on')
    m2.config.set('nsslapd-unhashed-pw-switch', 'on')
    test_passw = 'm2Test199'

    _enable_changelog_encryption(m1, encryption)

    for inst1, inst2 in ((m1, m2), (m2, m1)):
        # need to create a user specific to the encryption
        # else the two runs will hit the same user
        user_props={
                    'uid': 'testuser_%s' % encryption,
                    'cn' : 'testuser_%s' % encryption,
                    'sn' : 'user',
                    'uidNumber' : '1000',
                    'gidNumber' : '1000',
                    'homeDirectory' : '/home/testuser_%s' % encryption
                }
        user_props["userPassword"] = PASSWORD
        users = UserAccounts(inst1, DEFAULT_SUFFIX)
        tuser = users.create(properties=user_props)

        _check_unhashed_userpw_encrypted(m1, 'add', tuser.dn, PASSWORD, True)
        _check_unhashed_userpw_encrypted(m2, 'add', tuser.dn, PASSWORD, False)

        users = UserAccounts(inst2, DEFAULT_SUFFIX)
        tuser = users.get(tuser.rdn)
        tuser.set('userPassword', test_passw)
        _check_unhashed_userpw_encrypted(m1, 'modify', tuser.dn, test_passw, True)
        _check_unhashed_userpw_encrypted(m2, 'modify', tuser.dn, test_passw, False)
        tuser.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
