""" Testing basic functionalities of DSAdmin


"""
import dsadmin
from dsadmin import DSAdmin, Entry
from dsadmin import NoSuchEntryError
import ldap
from ldap import *

from nose import SkipTest
from nose.tools import *

import config
from config import *

conn = None
added_entries = None


def setup():
    global conn
    try:
        conn = DSAdmin(**config.auth)
        conn.verbose = True
        conn.added_entries = []
    except SERVER_DOWN, e:
        log.error("To run tests you need a working 389 instance %s" % config.auth)
        raise e


def tearDown():
    global conn

    # reduce log level
    conn.config.loglevel(0)
    conn.config.loglevel(0, level='access')

    for e in conn.added_entries:
        try:
            conn.delete_s(e)
        except ldap.NO_SUCH_OBJECT:
            log.warn("entry not found %r" % e)


def bind_test():
    print "conn: %s" % conn


def setupBindDN_UID_test():
    user = {
        'binddn': 'uid=rmanager1,cn=config',
        'bindpw': 'password'
    }
    e = conn.setupBindDN(**user)
    conn.added_entries.append(e.dn)

    assert e.dn == user['binddn'], "Bad entry: %r " % e
    expected = conn.getEntry(user['binddn'], ldap.SCOPE_BASE)
    assert entry_equals(
        e, expected), "Mismatching entry %r vs %r" % (e, expected)


def setupBindDN_CN_test():
    user = {
        'binddn': 'cn=rmanager1,cn=config',
        'bindpw': 'password'
    }
    e = conn.setupBindDN(**user)
    conn.added_entries.append(e.dn)
    assert e.dn == user['binddn'], "Bad entry: %r " % e
    expected = conn.getEntry(user['binddn'], ldap.SCOPE_BASE)
    assert entry_equals(
        e, expected), "Mismatching entry %r vs %r" % (e, expected)


def setupChangelog_default_test():
    e = conn.replica.changelog()
    conn.added_entries.append(e.dn)
    assert e.dn, "Bad changelog entry: %r " % e
    assert e.getValue('nsslapd-changelogdir').endswith("changelogdb"), "Mismatching entry %r " % e.data.get('nsslapd-changelogdir')
    conn.delete_s("cn=changelog5,cn=config")


def setupChangelog_test():
    e = conn.replica.changelog(dbname="mockChangelogDb")
    conn.added_entries.append(e.dn)
    assert e.dn, "Bad changelog entry: %r " % e
    assert e.getValue('nsslapd-changelogdir').endswith("mockChangelogDb"), "Mismatching entry %r " % e.data.get('nsslapd-changelogdir')
    conn.delete_s("cn=changelog5,cn=config")


def setupChangelog_full_test():
    e = conn.replica.changelog(dbname="/tmp/mockChangelogDb")
    conn.added_entries.append(e.dn)

    assert e.dn, "Bad changelog entry: %r " % e
    expect(e, 'nsslapd-changelogdir', "/tmp/mockChangelogDb")
    conn.delete_s("cn=changelog5,cn=config")


@raises(NoSuchEntryError)
def getMTEntry_missing_test():
    e = conn.getMTEntry('o=MISSING')


def getMTEntry_present_test():
    suffix = 'o=addressbook16'
    e = conn.getMTEntry(suffix)
    assert e, "Entry should be present %s" % suffix

