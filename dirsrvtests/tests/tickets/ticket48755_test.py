# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def add_ou_entry(server, idx, myparent):
    name = 'OU%d' % idx
    dn = 'ou=%s,%s' % (name, myparent)
    server.add_s(Entry((dn, {'objectclass': ['top', 'organizationalunit'],
                             'ou': name})))
    time.sleep(1)


def add_user_entry(server, idx, myparent):
    name = 'tuser%d' % idx
    dn = 'uid=%s,%s' % (name, myparent)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'organizationalPerson', 'inetorgperson'],
                             'givenname': 'test',
                             'sn': 'user%d' % idx,
                             'cn': 'Test User%d' % idx,
                             'userpassword': 'password'})))
    time.sleep(1)


def del_user_entry(server, idx, myparent):
    name = 'tuser%d' % idx
    dn = 'uid=%s,%s' % (name, myparent)
    server.delete_s(dn)
    time.sleep(1)


def add_ldapsubentry(server, myparent):
    name = 'nsPwPolicyContainer'
    container = 'cn=%s,%s' % (name, myparent)
    server.add_s(Entry((container, {'objectclass': ['top', 'nsContainer'],
                                    'cn': '%s' % name})))

    name = 'nsPwPolicyEntry'
    pwpentry = 'cn=%s,%s' % (name, myparent)
    pwpdn = 'cn="%s",%s' % (pwpentry, container)
    server.add_s(Entry((pwpdn, {'objectclass': ['top', 'ldapsubentry', 'passwordpolicy'],
                                'passwordStorageScheme': 'ssha',
                                'passwordCheckSyntax': 'on',
                                'passwordInHistory': '6',
                                'passwordChange': 'on',
                                'passwordMinAge': '0',
                                'passwordExp': 'off',
                                'passwordMustChange': 'off',
                                'cn': '%s' % pwpentry})))

    name = 'nsPwTemplateEntry'
    tmplentry = 'cn=%s,%s' % (name, myparent)
    tmpldn = 'cn="%s",%s' % (tmplentry, container)
    server.add_s(Entry((tmpldn, {'objectclass': ['top', 'ldapsubentry', 'costemplate', 'extensibleObject'],
                                 'cosPriority': '1',
                                 'cn': '%s' % tmplentry})))

    name = 'nsPwPolicy_CoS'
    cos = 'cn=%s,%s' % (name, myparent)
    server.add_s(Entry((cos, {'objectclass': ['top', 'ldapsubentry', 'cosPointerDefinition', 'cosSuperDefinition'],
                              'costemplatedn': '%s' % tmpldn,
                              'cosAttribute': 'pwdpolicysubentry default operational-default',
                              'cn': '%s' % name})))
    time.sleep(1)


def test_ticket48755(topology_m2):
    log.info("Ticket 48755 - moving an entry could make the online init fail")

    M1 = topology_m2.ms["master1"]
    M2 = topology_m2.ms["master2"]

    log.info("Generating DIT_0")
    idx = 0
    add_ou_entry(M1, idx, DEFAULT_SUFFIX)

    ou0 = 'ou=OU%d' % idx
    parent0 = '%s,%s' % (ou0, DEFAULT_SUFFIX)
    add_ou_entry(M1, idx, parent0)

    add_ldapsubentry(M1, parent0)

    parent00 = 'ou=OU%d,%s' % (idx, parent0)
    for idx in range(0, 9):
        add_user_entry(M1, idx, parent00)
        if idx % 2 == 0:
            log.info("Turning tuser%d into a tombstone entry" % idx)
            del_user_entry(M1, idx, parent00)

    log.info('%s => %s => %s => 10 USERS' % (DEFAULT_SUFFIX, parent0, parent00))

    log.info("Generating DIT_1")
    idx = 1
    add_ou_entry(M1, idx, DEFAULT_SUFFIX)

    parent1 = 'ou=OU%d,%s' % (idx, DEFAULT_SUFFIX)
    add_ou_entry(M1, idx, parent1)

    add_ldapsubentry(M1, parent1)

    log.info("Moving %s to DIT_1" % parent00)
    M1.rename_s(parent00, ou0, newsuperior=parent1, delold=1)
    time.sleep(1)

    log.info("Moving %s to DIT_1" % parent0)
    parent01 = '%s,%s' % (ou0, parent1)
    M1.rename_s(parent0, ou0, newsuperior=parent01, delold=1)
    time.sleep(1)

    parent001 = '%s,%s' % (ou0, parent01)
    log.info("Moving USERS to %s" % parent0)
    for idx in range(0, 9):
        if idx % 2 == 1:
            name = 'tuser%d' % idx
            rdn = 'uid=%s' % name
            dn = 'uid=%s,%s' % (name, parent01)
            M1.rename_s(dn, rdn, newsuperior=parent001, delold=1)
            time.sleep(1)

    log.info('%s => %s => %s => %s => 10 USERS' % (DEFAULT_SUFFIX, parent1, parent01, parent001))

    log.info("Run Consumer Initialization.")
    m1_m2_agmt = topology_m2.ms["master1_agmts"]["m1_m2"]
    M1.startReplication_async(m1_m2_agmt)
    M1.waitForReplInit(m1_m2_agmt)
    time.sleep(2)

    m1entries = M1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                            '(|(objectclass=ldapsubentry)(objectclass=nstombstone)(nsuniqueid=*))')
    m2entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                            '(|(objectclass=ldapsubentry)(objectclass=nstombstone)(nsuniqueid=*))')

    log.info("m1entry count - %d", len(m1entries))
    log.info("m2entry count - %d", len(m2entries))

    assert len(m1entries) == len(m2entries)
    log.info('PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
