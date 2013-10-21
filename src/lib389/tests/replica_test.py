"""Brooker classes to organize ldap methods.
   Stuff is split in classes, like:
   * Replica
   * Backend
   * Suffix

   You will access this from:
   DSAdmin.backend.methodName()
"""

from nose import *
from nose.tools import *

import config
from config import log
from config import *

import ldap
import time
import sys
import dsadmin
from dsadmin import DSAdmin, Entry
from dsadmin import NoSuchEntryError
from dsadmin import utils
from dsadmin.tools import DSAdminTools
from subprocess import Popen
from random import randint
from dsadmin.brooker import Replica
from dsadmin import MASTER_TYPE, DN_MAPPING_TREE, DN_CHANGELOG
# Test harnesses
from dsadmin_test import drop_backend, addbackend_harn
from dsadmin_test import drop_added_entries

conn = None
added_entries = None
added_backends = None

MOCK_REPLICA_ID = '12'


def setup():
    # uses an existing 389 instance
    # add a suffix
    # add an agreement
    # This setup is quite verbose but to test dsadmin method we should
    # do things manually. A better solution would be to use an LDIF.
    global conn
    conn = DSAdmin(**config.auth)
    conn.verbose = True
    conn.added_entries = []
    conn.added_backends = set(['o=mockbe1'])
    conn.added_replicas = []

    # add a backend for testing ruv and agreements
    addbackend_harn(conn, 'testReplica')

    # add another backend for testing replica.add()
    addbackend_harn(conn, 'testReplicaCreation')

    # replication needs changelog
    conn.replica.changelog()
    # add rmanager entry
    try:
        conn.add_s(Entry((DN_RMANAGER, {
            'objectclass': "top person inetOrgPerson".split(),
            'sn': ["bind dn pseudo user"],
            'cn': 'replication manager',
            'uid': 'rmanager'
        }))
        )
        conn.added_entries.append(DN_RMANAGER)
    except ldap.ALREADY_EXISTS:
        pass

    # add a master replica entry
    # to test ruv and agreements
    replica_dn = ','.join(
        ['cn=replica', 'cn="o=testReplica"', DN_MAPPING_TREE])
    replica_e = Entry(replica_dn)
    replica_e.update({
                     'objectclass': ["top", "nsds5replica", "extensibleobject"],
                     'cn': "replica",
                     'nsds5replicaroot': 'o=testReplica',
                     'nsds5replicaid': MOCK_REPLICA_ID,
                     'nsds5replicatype': '3',
                     'nsds5flags': '1',
                     'nsds5replicabinddn': DN_RMANAGER
                     })
    try:
        conn.add_s(replica_e)
    except ldap.ALREADY_EXISTS:
        pass
    conn.added_entries.append(replica_dn)

    agreement_dn = ','.join(('cn=testAgreement', replica_dn))
    agreement_e = Entry(agreement_dn)
    agreement_e.update({
                       'objectclass': ["top", "nsds5replicationagreement"],
                       'cn': 'testAgreement',
                       'nsds5replicahost': 'localhost',
                       'nsds5replicaport': '22389',
                       'nsds5replicatimeout': '120',
                       'nsds5replicabinddn': DN_RMANAGER,
                       'nsds5replicacredentials': 'password',
                       'nsds5replicabindmethod': 'simple',
                       'nsds5replicaroot': 'o=testReplica',
                       'nsds5replicaupdateschedule': '0000-2359 0123456',
                       'description': 'testAgreement'
                       })
    try:
        conn.add_s(agreement_e)
    except ldap.ALREADY_EXISTS:
        pass
    conn.added_entries.append(agreement_dn)
    conn.agreement_dn = agreement_dn


def teardown():
    global conn
    drop_added_entries(conn)
    conn.delete_s(','.join(['cn="o=testreplica"', DN_MAPPING_TREE]))
    drop_backend(conn, 'o=testreplica')
    conn.delete_s('o=testreplica')


def changelog():
    changelog_e = conn.replica.changelog(dbname='foo')
    assert changelog_e.data['nsslapd-changelogdir'].endswith('foo')


def changelog_default_test():
    e = conn.replica.changelog()
    conn.added_entries.append(e.dn)
    assert e.dn, "Bad changelog entry: %r " % e
    assert e.getValue('nsslapd-changelogdir').endswith("changelogdb"), "Mismatching entry %r " % e.data.get('nsslapd-changelogdir')
    conn.delete_s("cn=changelog5,cn=config")


def changelog_customdb_test():
    e = conn.replica.changelog(dbname="mockChangelogDb")
    conn.added_entries.append(e.dn)
    assert e.dn, "Bad changelog entry: %r " % e
    assert e.getValue('nsslapd-changelogdir').endswith("mockChangelogDb"), "Mismatching entry %r " % e.data.get('nsslapd-changelogdir')
    conn.delete_s("cn=changelog5,cn=config")


def changelog_full_path_test():
    e = conn.replica.changelog(dbname="/tmp/mockChangelogDb")
    conn.added_entries.append(e.dn)

    assert e.dn, "Bad changelog entry: %r " % e
    expect(e, 'nsslapd-changelogdir', "/tmp/mockChangelogDb")
    conn.delete_s("cn=changelog5,cn=config")


def check_init_test():
    raise NotImplementedError()


def disable_logging_test():
    raise NotImplementedError()


def enable_logging_test():
    raise NotImplementedError()


def status_test():
    status = conn.replica.status(conn.agreement_dn)
    log.info(status)
    assert status


def list_test():
    # was get_entries_test():
    replicas = conn.replica.list()
    assert any(['testreplica' in x.dn.lower() for x in replicas])


def ruv_test():
    ruv = conn.replica.ruv(suffix='o=testReplica')
    assert ruv, "Missing RUV"
    assert len(ruv.rid), "Missing RID"
    assert int(MOCK_REPLICA_ID) in ruv.rid.keys()


@raises(ldap.NO_SUCH_OBJECT)
def ruv_missing_test():
    ruv = conn.replica.ruv(suffix='o=MISSING')
    assert ruv, "Missing RUV"
    assert len(ruv.rid), "Missing RID"
    assert int(MOCK_REPLICA_ID) in ruv.rid.keys()


def start_test():
    raise NotImplementedError()


def stop_test():
    raise NotImplementedError()


def restart_test():
    raise NotImplementedError()


def start_async_test():
    raise NotImplementedError()


def wait_for_init_test():
    raise NotImplementedError()


def setup_agreement_default_test():
    user = {
        'binddn': DN_RMANAGER,
        'bindpw': "password"
    }
    params = {'consumer': MockDSAdmin(), 'suffix': "o=testReplica"}
    params.update(user)

    agreement_dn = conn.replica.agreement_add(**params)
    conn.added_entries.append(agreement_dn)

@raises(ldap.ALREADY_EXISTS)
def setup_agreement_duplicate_test():
    user = {
        'binddn': DN_RMANAGER,
        'bindpw': "password"
    }
    params = {
        'consumer': MockDSAdmin(),
        'suffix': "o=testReplica",
        'cn_format': 'testAgreement',
        'description_format': 'testAgreement'
    }
    params.update(user)
    conn.replica.agreement_add(**params)


def setup_agreement_test():
    user = {
        'binddn': DN_RMANAGER,
        'bindpw': "password"
    }
    params = {'consumer': MockDSAdmin(), 'suffix': "o=testReplica"}
    params.update(user)

    conn.replica.agreement_add(**params)
    # timeout=120, auto_init=False, bindmethod='simple', starttls=False, args=None):
    raise NotImplementedError()

def setup_agreement_fractional_test():
    # TODO: fractiona replicates only a subset of attributes 
    # 
    user = {
        'binddn': DN_RMANAGER,
        'bindpw': "password"
    }
    params = {'consumer': MockDSAdmin(), 'suffix': "o=testReplica"}
    params.update(user)

    #conn.replica.agreement_add(**params)
    #cn_format=r'meTo_%s:%s', description_format=r'me to %s:%s', timeout=120, auto_init=False, bindmethod='simple', starttls=False, args=None):
    raise NotImplementedError()


def find_agreements_test():
    agreements = conn.replica.agreements(dn=False)
    assert any(['testagreement' in x.dn.lower(
    ) for x in agreements]), "Missing agreement"


def find_agreements_dn_test():
    agreements_dn = conn.replica.agreements()
    assert any(['testagreement' in x.lower(
    ) for x in agreements_dn]), "Missing agreement"


def setup_replica_test():
    args = {
        'suffix': "o=testReplicaCreation",
        'binddn': DN_RMANAGER,
        'bindpw': "password",
        'rtype': dsadmin.MASTER_TYPE,
        'rid': MOCK_REPLICA_ID
    }
    # create a replica entry
    replica_e = conn.replica.add(**args)
    assert 'dn' in replica_e, "Missing dn in replica"
    conn.added_entries.append(replica_e['dn'])


def setup_replica_hub_test():
    args = {
        'suffix': "o=testReplicaCreation",
        'binddn': DN_RMANAGER,
        'bindpw': "password",
        'rtype': dsadmin.HUB_TYPE,
        'rid': MOCK_REPLICA_ID
    }
    # create a replica entry
    replica_e = conn.replica.add(**args)
    assert 'dn' in replica_e, "Missing dn in replica"
    conn.added_entries.append(replica_e['dn'])


def setup_replica_referrals_test():
    #tombstone_purgedelay=None, purgedelay=None, referrals=None, legacy=False
    raise NotImplementedError()


def setup_all_replica_test():
    raise NotImplementedError()

def replica_keep_in_sync_test():
    raise NotImplementedError()
