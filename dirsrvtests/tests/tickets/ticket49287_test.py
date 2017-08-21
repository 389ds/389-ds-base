# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.properties import RA_NAME, RA_BINDDN, RA_BINDPW, RA_METHOD, RA_TRANSPORT_PROT, BACKEND_NAME
from lib389.topologies import topology_m2
from lib389._constants import *

DEBUGGING = os.getenv('DEBUGGING', False)
GROUP_DN = ("cn=group," + DEFAULT_SUFFIX)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _add_repl_backend(s1, s2, be, rid):
    suffix = 'ou=%s,dc=test,dc=com' % be
    create_backend(s1, s2, suffix, be)
    add_ou(s1, suffix)
    replicate_backend(s1, s2, suffix, rid)


def _wait_for_sync(s1, s2, testbase, final_db):

    now = time.time()
    cn1 = 'sync-%s-%d' % (now, 1)
    cn2 = 'sync-%s-%d' % (now, 2)
    add_user(s1, cn1, testbase, 'add on m1', sleep=False)
    add_user(s2, cn2, testbase, 'add on m2', sleep=False)
    dn1 = 'cn=%s,%s' % (cn1, testbase)
    dn2 = 'cn=%s,%s' % (cn2, testbase)
    if final_db:
        final_db.append(dn1)
        final_db.append(dn2)
    _check_entry_exist(s2, dn1, 10, 5)
    _check_entry_exist(s1, dn2, 10, 5)


def _check_entry_exist(master, dn, loops=10, wait=1):
    attempt = 0
    while attempt <= loops:
        try:
            dn
            ent = master.getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            attempt = attempt + 1
            time.sleep(wait)
        except ldap.LDAPError as e:
            log.fatal('Failed to retrieve user (%s): error %s' % (dn, e.message['desc']))
            assert False
    assert attempt <= loops


def config_memberof(server):

    server.plugins.enable(name=PLUGIN_MEMBER_OF)
    MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
    server.modify_s(MEMBEROF_PLUGIN_DN, [(ldap.MOD_REPLACE,
                                          'memberOfAllBackends',
                                          'on')])
    # Configure fractional to prevent total init to send memberof
    ents = server.agreement.list(suffix=DEFAULT_SUFFIX)
    log.info('update %s to add nsDS5ReplicatedAttributeListTotal' % ents[0].dn)
    for ent in ents:
        server.modify_s(ent.dn,
                              [(ldap.MOD_REPLACE,
                                'nsDS5ReplicatedAttributeListTotal',
                                '(objectclass=*) $ EXCLUDE '),
                               (ldap.MOD_REPLACE,
                                'nsDS5ReplicatedAttributeList',
                                '(objectclass=*) $ EXCLUDE memberOf')])


def _disable_auto_oc_memberof(server):
    MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
    server.modify_s(MEMBEROF_PLUGIN_DN,
        [(ldap.MOD_REPLACE, 'memberOfAutoAddOC', 'nsContainer')])


def _enable_auto_oc_memberof(server):
    MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
    server.modify_s(MEMBEROF_PLUGIN_DN,
        [(ldap.MOD_REPLACE, 'memberOfAutoAddOC', 'nsMemberOf')])


def add_dc(server, dn):
    server.add_s(Entry((dn, {'objectclass': ['top', 'domain']})))


def add_ou(server, dn):
    server.add_s(Entry((dn, {'objectclass': ['top', 'organizationalunit']})))


def add_container(server, dn):
    server.add_s(Entry((dn, {'objectclass': ['top', 'nscontainer']})))


def add_user(server, cn, testbase, desc, sleep=True):
    dn = 'cn=%s,%s' % (cn, testbase)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'inetuser'],
                             'sn': 'user_%s' % cn,
                             'description': desc})))
    if sleep:
        time.sleep(2)


def add_person(server, cn, testbase, desc, sleep=True):
    dn = 'cn=%s,%s' % (cn, testbase)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person'],
                             'sn': 'user_%s' % cn,
                             'description': desc})))
    if sleep:
        time.sleep(2)


def add_multi_member(server, cn, mem_id, mem_usr, testbase, sleep=True):
    dn = 'cn=%s,ou=groups,%s' % (cn, testbase)
    members = []
    for usr in mem_usr:
        members.append('cn=a%d,ou=be_%d,%s' % (mem_id, usr, testbase))
    mod = [(ldap.MOD_ADD, 'member', members)]
    try:
        server.modify_s(dn, mod)
    except ldap.OBJECT_CLASS_VIOLATION:
        log.info('objectclass violation')

    if sleep:
        time.sleep(2)


def add_member(server, cn, mem, testbase, sleep=True):
    dn = 'cn=%s,ou=groups,%s' % (cn, testbase)
    mem_dn = 'cn=%s,ou=people,%s' % (mem, testbase)
    mod = [(ldap.MOD_ADD, 'member', mem_dn)]
    server.modify_s(dn, mod)
    if sleep:
        time.sleep(2)


def add_group(server, testbase, nr, sleep=True):

    dn = 'cn=g%d,ou=groups,%s' % (nr, testbase)
    server.add_s(Entry((dn, {'objectclass': ['top', 'groupofnames'],
                             'member': [
                                   'cn=m1_%d,%s' % (nr, testbase),
                                   'cn=m2_%d,%s' % (nr, testbase),
                                   'cn=m3_%d,%s' % (nr, testbase)
                                   ],
                             'description': 'group %d' % nr})))
    if sleep:
        time.sleep(2)


def del_group(server, testbase, nr, sleep=True):

    dn = 'cn=g%d,%s' % (nr, testbase)
    server.delete_s(dn)
    if sleep:
        time.sleep(2)


def mod_entry(server, cn, testbase, desc):
    dn = 'cn=%s,%s' % (cn, testbase)
    mod = [(ldap.MOD_ADD, 'description', desc)]
    server.modify_s(dn, mod)
    time.sleep(2)


def del_entry(server, testbase, cn):
    dn = 'cn=%s,%s' % (cn, testbase)
    server.delete_s(dn)
    time.sleep(2)


def _disable_nunc_stans(server):
    server.modify_s(DN_CONFIG,
        [(ldap.MOD_REPLACE, 'nsslapd-enable-nunc-stans', 'off')])


def _enable_spec_logging(server):
    mods = [(ldap.MOD_REPLACE, 'nsslapd-accesslog-level', str(260)),  # Internal op
            (ldap.MOD_REPLACE, 'nsslapd-errorlog-level', str(8192 + 65536)),
            # (ldap.MOD_REPLACE, 'nsslapd-errorlog-level', str(8192+32768+524288)),
            # (ldap.MOD_REPLACE, 'nsslapd-errorlog-level', str(8192)),
            (ldap.MOD_REPLACE, 'nsslapd-plugin-logging', 'on')]
    server.modify_s(DN_CONFIG, mods)
    server.modify_s(DN_CONFIG,
        [(ldap.MOD_REPLACE, 'nsslapd-auditlog-logging-enabled', 'on')])


def create_agmt(s1, s2, replSuffix):
    properties = {RA_NAME: 'meTo_{}:{}'.format(s1.host, str(s2.port)),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    new_agmt = s1.agreement.create(suffix=replSuffix,
                                   host=s2.host,
                                   port=s2.port,
                                   properties=properties)
    return new_agmt


def create_backend(s1, s2, beSuffix, beName):
    s1.mappingtree.create(beSuffix, beName)
    s1.backend.create(beSuffix, {BACKEND_NAME: beName})
    s2.mappingtree.create(beSuffix, beName)
    s2.backend.create(beSuffix, {BACKEND_NAME: beName})


def replicate_backend(s1, s2, beSuffix, rid):
    s1.replica.enableReplication(suffix=beSuffix, role=ReplicaRole.MASTER, replicaId=rid)
    s2.replica.enableReplication(suffix=beSuffix, role=ReplicaRole.MASTER, replicaId=rid + 1)

    # agreement m2_m1_agmt is not needed... :p

    s1s2_agmt = create_agmt(s1, s2, beSuffix)
    s2s1_agmt = create_agmt(s2, s1, beSuffix)
    s1.agreement.init(beSuffix, s2.host, s2.port)
    s1.waitForReplInit(s1s2_agmt)


def check_group_mods(server1, server2, group, testbase):
    # add members to group
    add_multi_member(server1, group, 1, [1,2,3,4,5], testbase, sleep=False)
    add_multi_member(server1, group, 2, [3,4,5], testbase, sleep=False)
    add_multi_member(server1, group, 3, [0], testbase, sleep=False)
    add_multi_member(server1, group, 4, [1,3,5], testbase, sleep=False)
    add_multi_member(server1, group, 5, [2,0], testbase, sleep=False)
    add_multi_member(server1, group, 6, [2,3,4], testbase, sleep=False)
    # check that replication is working
    # for main backend and some member backends
    _wait_for_sync(server1, server2, testbase, None)
    for i in range(6):
        be = "be_%d" % i
        _wait_for_sync(server1, server2, 'ou=%s,dc=test,dc=com' % be, None)


def check_multi_group_mods(server1, server2, group1, group2, testbase):
    # add members to group
    add_multi_member(server2, group1, 1, [1,2,3,4,5], testbase, sleep=False)
    add_multi_member(server1, group2, 1, [1,2,3,4,5], testbase, sleep=False)
    add_multi_member(server2, group1, 2, [3,4,5], testbase, sleep=False)
    add_multi_member(server1, group2, 2, [3,4,5], testbase, sleep=False)
    add_multi_member(server2, group1, 3, [0], testbase, sleep=False)
    add_multi_member(server1, group2, 3, [0], testbase, sleep=False)
    add_multi_member(server2, group1, 4, [1,3,5], testbase, sleep=False)
    add_multi_member(server1, group2, 4, [1,3,5], testbase, sleep=False)
    add_multi_member(server2, group1, 5, [2,0], testbase, sleep=False)
    add_multi_member(server1, group2, 5, [2,0], testbase, sleep=False)
    add_multi_member(server2, group1, 6, [2,3,4], testbase, sleep=False)
    add_multi_member(server1, group2, 6, [2,3,4], testbase, sleep=False)
    # check that replication is working
    # for main backend and some member backends
    _wait_for_sync(server1, server2, testbase, None)
    for i in range(6):
        be = "be_%d" % i
        _wait_for_sync(server1, server2, 'ou=%s,dc=test,dc=com' % be, None)


def test_ticket49287(topology_m2):
    """
        test case for memberof and conflict entries

    """

    # return

    M1 = topology_m2.ms["master1"]
    M2 = topology_m2.ms["master2"]

    config_memberof(M1)
    config_memberof(M2)

    _enable_spec_logging(M1)
    _enable_spec_logging(M2)

    _disable_nunc_stans(M1)
    _disable_nunc_stans(M2)

    M1.restart(timeout=10)
    M2.restart(timeout=10)

    testbase = 'dc=test,dc=com'
    bename = 'test'
    create_backend(M1, M2, testbase, bename)
    add_dc(M1, testbase)
    add_ou(M1, 'ou=groups,%s' % testbase)
    replicate_backend(M1, M2, testbase, 100)

    peoplebase = 'ou=people,dc=test,dc=com'
    peoplebe = 'people'
    create_backend(M1, M2, peoplebase, peoplebe)
    add_ou(M1, peoplebase)
    replicate_backend(M1, M2, peoplebase, 100)

    for i in range(10):
        cn = 'a%d' % i
        add_user(M1, cn, peoplebase, 'add on m1', sleep=False)
    time.sleep(2)
    add_group(M1, testbase, 1)
    for i in range(10):
        cn = 'a%d' % i
        add_member(M1, 'g1', cn, testbase, sleep=False)
        cn = 'b%d' % i
        add_user(M1, cn, peoplebase, 'add on m1', sleep=False)
    time.sleep(2)

    _wait_for_sync(M1, M2, testbase, None)
    _wait_for_sync(M1, M2, peoplebase, None)

    # test group with members in multiple backends
    for i in range(7):
        be = "be_%d" % i
        _add_repl_backend(M1, M2, be, 300 + i)

    # add entries akllowing meberof
    for i in range(1, 7):
        be = "be_%d" % i
        for i in range(10):
            cn = 'a%d' % i
            add_user(M1, cn, 'ou=%s,dc=test,dc=com' % be, 'add on m1', sleep=False)
    # add entries not allowing memberof
    be = 'be_0'
    for i in range(10):
        cn = 'a%d' % i
        add_person(M1, cn, 'ou=%s,dc=test,dc=com' % be, 'add on m1', sleep=False)

    _disable_auto_oc_memberof(M1)
    _disable_auto_oc_memberof(M2)
    add_group(M1, testbase, 2)
    check_group_mods(M1, M2, 'g2', testbase)

    _enable_auto_oc_memberof(M1)
    add_group(M1, testbase, 3)
    check_group_mods(M1, M2, 'g3', testbase)

    _enable_auto_oc_memberof(M2)
    add_group(M1, testbase, 4)
    check_group_mods(M1, M2, 'g4', testbase)

    add_group(M1, testbase, 5)
    add_group(M1, testbase, 6)
    check_multi_group_mods(M1, M2, 'g5', 'g6', testbase)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
