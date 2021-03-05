# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import sys
import time
import ldap
import logging
import pytest
import threading
import random
from lib389 import DirSrv, Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

from lib389.idm.directorymanager import DirectoryManager

pytestmark = pytest.mark.tier3

logging.getLogger(__name__).setLevel(logging.DEBUG)
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s' +
                              ' - %(message)s')
handler = logging.StreamHandler()
handler.setFormatter(formatter)
log = logging.getLogger(__name__)
log.addHandler(handler)

installation1_prefix = None
NUM_USERS = 5000
MAX_PASSES = 1000
CHECK_CONVERGENCE = True
ENABLE_VALGRIND = False
RUNNING = True

DEBUGGING = os.getenv('DEBUGGING', default=False)

class TopologyReplication(object):
    def __init__(self, supplier1, supplier2):
        supplier1.open()
        self.supplier1 = supplier1
        supplier2.open()
        self.supplier2 = supplier2


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating supplier 1...
    supplier1 = DirSrv(verbose=DEBUGGING)
    args_instance[SER_HOST] = HOST_SUPPLIER_1
    args_instance[SER_PORT] = PORT_SUPPLIER_1
    args_instance[SER_SECURE_PORT] = SECUREPORT_SUPPLIER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_SUPPLIER_1
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_supplier = args_instance.copy()
    supplier1.allocate(args_supplier)
    instance_supplier1 = supplier1.exists()
    if instance_supplier1:
        supplier1.delete()
    supplier1.create()
    supplier1.open()
    supplier1.replica.enableReplication(suffix=SUFFIX, role=ReplicaRole.SUPPLIER,
                                      replicaId=REPLICAID_SUPPLIER_1)

    # Creating supplier 2...
    supplier2 = DirSrv(verbose=DEBUGGING)
    args_instance[SER_HOST] = HOST_SUPPLIER_2
    args_instance[SER_PORT] = PORT_SUPPLIER_2
    args_instance[SER_SECURE_PORT] = SECUREPORT_SUPPLIER_2
    args_instance[SER_SERVERID_PROP] = SERVERID_SUPPLIER_2
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_supplier = args_instance.copy()
    supplier2.allocate(args_supplier)
    instance_supplier2 = supplier2.exists()
    if instance_supplier2:
        supplier2.delete()
    supplier2.create()
    supplier2.open()
    supplier2.replica.enableReplication(suffix=SUFFIX, role=ReplicaRole.SUPPLIER,
                                      replicaId=REPLICAID_SUPPLIER_2)

    #
    # Create all the agreements
    #
    # Creating agreement from supplier 1 to supplier 2
    properties = {RA_NAME: r'meTo_$host:$port',
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = supplier1.agreement.create(suffix=SUFFIX, host=supplier2.host,
                                          port=supplier2.port,
                                          properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a supplier -> supplier replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from supplier 2 to supplier 1
    properties = {RA_NAME: r'meTo_$host:$port',
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = supplier2.agreement.create(suffix=SUFFIX, host=supplier1.host,
                                          port=supplier1.port,
                                          properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a supplier -> supplier replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Import tests entries into supplier1 before we initialize supplier2
    #
    ldif_dir = supplier1.get_ldif_dir()

    import_ldif = ldif_dir + '/rel7.5-entries.ldif'

    # First generate an ldif
    try:
        ldif = open(import_ldif, 'w')
    except IOError as e:
        log.fatal('Failed to create test ldif, error: %s - %s' %
                  (e.errno, e.strerror))
        assert False

    # Create the root node
    ldif.write('dn: ' + DEFAULT_SUFFIX + '\n')
    ldif.write('objectclass: top\n')
    ldif.write('objectclass: domain\n')
    ldif.write('dc: example\n')
    ldif.write('\n')

    # Create the entries
    idx = 0
    while idx < NUM_USERS:
        count = str(idx)
        ldif.write('dn: uid=supplier1_entry' + count + ',' +
                   DEFAULT_SUFFIX + '\n')
        ldif.write('objectclass: top\n')
        ldif.write('objectclass: person\n')
        ldif.write('objectclass: inetorgperson\n')
        ldif.write('objectclass: organizationalperson\n')
        ldif.write('uid: supplier1_entry' + count + '\n')
        ldif.write('cn: supplier1 entry' + count + '\n')
        ldif.write('givenname: supplier1 ' + count + '\n')
        ldif.write('sn: entry ' + count + '\n')
        ldif.write('userpassword: supplier1_entry' + count + '\n')
        ldif.write('description: ' + 'a' * random.randint(1, 1000) + '\n')
        ldif.write('\n')

        ldif.write('dn: uid=supplier2_entry' + count + ',' +
                   DEFAULT_SUFFIX + '\n')
        ldif.write('objectclass: top\n')
        ldif.write('objectclass: person\n')
        ldif.write('objectclass: inetorgperson\n')
        ldif.write('objectclass: organizationalperson\n')
        ldif.write('uid: supplier2_entry' + count + '\n')
        ldif.write('cn: supplier2 entry' + count + '\n')
        ldif.write('givenname: supplier2 ' + count + '\n')
        ldif.write('sn: entry ' + count + '\n')
        ldif.write('userpassword: supplier2_entry' + count + '\n')
        ldif.write('description: ' + 'a' * random.randint(1, 1000) + '\n')
        ldif.write('\n')
        idx += 1

    ldif.close()

    # Now import it
    try:
        supplier1.tasks.importLDIF(suffix=DEFAULT_SUFFIX, input_file=import_ldif,
                                 args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_reliab_7.5: Online import failed')
        assert False

    #
    # Initialize all the agreements
    #
    supplier1.agreement.init(SUFFIX, HOST_SUPPLIER_2, PORT_SUPPLIER_2)
    supplier1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if supplier1.testReplication(DEFAULT_SUFFIX, supplier2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Clear out the tmp dir
    supplier1.clearTmpDir(__file__)

    # Delete each instance in the end
    def fin():
        supplier1.delete()
        supplier2.delete()
        if ENABLE_VALGRIND:
            sbin_dir = get_sbin_dir(prefix=supplier1.prefix)
            valgrind_disable(sbin_dir)
    request.addfinalizer(fin)

    return TopologyReplication(supplier1, supplier2)


class AddDelUsers(threading.Thread):
    def __init__(self, inst, supplierid):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.id = supplierid

    def run(self):
        # Add 5000 entries
        idx = 0
        RDN = 'uid=add_del_supplier_' + self.id + '-'

        conn = DirectoryManager(self.inst).bind()

        while idx < NUM_USERS:
            USER_DN = RDN + str(idx) + ',' + DEFAULT_SUFFIX
            try:
                conn.add_s(Entry((USER_DN, {'objectclass':
                                            'top extensibleObject'.split(),
                                            'uid': 'user' + str(idx),
                                            'cn': 'g' * random.randint(1, 500)
                                            })))
            except ldap.LDAPError as e:
                log.fatal('Add users to supplier ' + self.id + ' failed (' +
                          USER_DN + ') error: ' + e.message['desc'])
            idx += 1
        conn.close()

        # Delete 5000 entries
        conn = DirectoryManager(self.inst).bind()
        idx = 0
        while idx < NUM_USERS:
            USER_DN = RDN + str(idx) + ',' + DEFAULT_SUFFIX
            try:
                conn.delete_s(USER_DN)
            except ldap.LDAPError as e:
                log.fatal('Failed to delete (' + USER_DN + ') on supplier ' +
                          self.id + ': error ' + e.message['desc'])
            idx += 1
        conn.close()


class ModUsers(threading.Thread):
    # Do mods and modrdns
    def __init__(self, inst, supplierid):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.id = supplierid

    def run(self):
        # Mod existing entries
        conn = DirectoryManager(self.inst).bind()
        idx = 0
        while idx < NUM_USERS:
            USER_DN = ('uid=supplier' + self.id + '_entry' + str(idx) + ',' +
                       DEFAULT_SUFFIX)
            try:
                conn.modify(USER_DN, [(ldap.MOD_REPLACE,
                                       'givenname',
                                       'new givenname supplier1-' + str(idx))])
            except ldap.LDAPError as e:
                log.fatal('Failed to modify (' + USER_DN + ') on supplier ' +
                          self.id + ': error ' + e.message['desc'])
            idx += 1
        conn.close()

        # Modrdn existing entries
        conn = DirectoryManager(self.inst).bind()
        idx = 0
        while idx < NUM_USERS:
            USER_DN = ('uid=supplier' + self.id + '_entry' + str(idx) + ',' +
                       DEFAULT_SUFFIX)
            NEW_RDN = 'cn=supplier' + self.id + '_entry' + str(idx)
            try:
                conn.rename_s(USER_DN, NEW_RDN, delold=1)
            except ldap.LDAPError as e:
                log.error('Failed to modrdn (' + USER_DN + ') on supplier ' +
                          self.id + ': error ' + e.message['desc'])
            idx += 1
        conn.close()

        # Undo modrdn to we can rerun this test
        conn = DirectoryManager(self.inst).bind()
        idx = 0
        while idx < NUM_USERS:
            USER_DN = ('cn=supplier' + self.id + '_entry' + str(idx) + ',' +
                       DEFAULT_SUFFIX)
            NEW_RDN = 'uid=supplier' + self.id + '_entry' + str(idx)
            try:
                conn.rename_s(USER_DN, NEW_RDN, delold=1)
            except ldap.LDAPError as e:
                log.error('Failed to modrdn (' + USER_DN + ') on supplier ' +
                          self.id + ': error ' + e.message['desc'])
            idx += 1
        conn.close()


class DoSearches(threading.Thread):
    # Search a supplier
    def __init__(self, inst, supplierid):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.id = supplierid

    def run(self):
        # Equality
        conn = DirectoryManager(self.inst).bind()
        idx = 0
        while idx < NUM_USERS:
            search_filter = ('(|(uid=supplier' + self.id + '_entry' + str(idx) +
                             ')(cn=supplier' + self.id + '_entry' + str(idx) +
                             '))')
            try:
                conn.search(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, search_filter)
            except ldap.LDAPError as e:
                log.fatal('Search Users: Search failed (%s): %s' %
                          (search_filter, e.message['desc']))
                conn.close()
                return

            idx += 1
        conn.close()

        # Substring
        conn = DirectoryManager(self.inst).bind()
        idx = 0
        while idx < NUM_USERS:
            search_filter = ('(|(uid=supplier' + self.id + '_entry' + str(idx) +
                             '*)(cn=supplier' + self.id + '_entry' + str(idx) +
                             '*))')
            try:
                conn.search(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, search_filter)
            except ldap.LDAPError as e:
                log.fatal('Search Users: Search failed (%s): %s' %
                          (search_filter, e.message['desc']))
                conn.close()
                return

            idx += 1
        conn.close()


class DoFullSearches(threading.Thread):
    # Search a supplier
    def __init__(self, inst):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst

    def run(self):
        global RUNNING
        conn = DirectoryManager(self.inst).bind()
        while RUNNING:
            time.sleep(2)
            try:
                conn.search_s(DEFAULT_SUFFIX,
                              ldap.SCOPE_SUBTREE,
                              'objectclass=top')
            except ldap.LDAPError as e:
                log.fatal('Full Search Users: Search failed (%s): %s' %
                          ('objectclass=*', e.message['desc']))
                conn.close()
                assert False

        conn.close()


def test_reliab7_5_init(topology):
    '''
    Reduce entry cache - to increase the cache churn

    Then process "reliability 15" type tests
    '''

    BACKEND_DN = 'cn=userroot,cn=ldbm database,cn=plugins,cn=config'

    # Update supplier 1
    try:
        topology.supplier1.modify_s(BACKEND_DN, [(ldap.MOD_REPLACE,
                                                'nsslapd-cachememsize',
                                                '512000'),
                                               (ldap.MOD_REPLACE,
                                                'nsslapd-cachesize',
                                                '500')])
    except ldap.LDAPError as e:
        log.fatal('Failed to set cache settings: error ' + e.message['desc'])
        assert False

    # Update supplier 2
    try:
        topology.supplier2.modify_s(BACKEND_DN, [(ldap.MOD_REPLACE,
                                                'nsslapd-cachememsize',
                                                '512000'),
                                               (ldap.MOD_REPLACE,
                                                'nsslapd-cachesize',
                                                '500')])
    except ldap.LDAPError as e:
        log.fatal('Failed to set cache settings: error ' + e.message['desc'])
        assert False

    # Restart the suppliers to pick up the new cache settings
    topology.supplier1.stop(timeout=10)
    topology.supplier2.stop(timeout=10)

    # This is the time to enable valgrind (if enabled)
    if ENABLE_VALGRIND:
        sbin_dir = get_sbin_dir(prefix=topology.supplier1.prefix)
        valgrind_enable(sbin_dir)

    topology.supplier1.start(timeout=30)
    topology.supplier2.start(timeout=30)


def test_reliab7_5_run(topology):
    '''
    Starting issuing adds, deletes, mods, modrdns, and searches
    '''
    global RUNNING
    count = 1
    RUNNING = True

    # Start some searches to run through the entire stress test
    fullSearch1 = DoFullSearches(topology.supplier1)
    fullSearch1.start()
    fullSearch2 = DoFullSearches(topology.supplier2)
    fullSearch2.start()

    while count <= MAX_PASSES:
        log.info('################## Reliabilty 7.5 Pass: %d' % count)

        # Supplier 1
        add_del_users1 = AddDelUsers(topology.supplier1, '1')
        add_del_users1.start()
        mod_users1 = ModUsers(topology.supplier1, '1')
        mod_users1.start()
        search1 = DoSearches(topology.supplier1, '1')
        search1.start()

        # Supplier 2
        add_del_users2 = AddDelUsers(topology.supplier2, '2')
        add_del_users2.start()
        mod_users2 = ModUsers(topology.supplier2, '2')
        mod_users2.start()
        search2 = DoSearches(topology.supplier2, '2')
        search2.start()

        # Search the suppliers
        search3 = DoSearches(topology.supplier1, '1')
        search3.start()
        search4 = DoSearches(topology.supplier2, '2')
        search4.start()

        # Wait for threads to finish
        log.info('################## Waiting for threads to finish...')
        add_del_users1.join()
        mod_users1.join()
        add_del_users2.join()
        mod_users2.join()
        log.info('################## Update threads finished.')
        search1.join()
        search2.join()
        search3.join()
        search4.join()
        log.info('################## All threads finished.')

        # Allow some time for replication to catch up before firing
        # off the next round of updates
        time.sleep(5)
        count += 1

    #
    # Wait for replication to converge
    #
    if CHECK_CONVERGENCE:
        # Add an entry to each supplier, and wait for it to replicate
        SUPPLIER1_DN = 'uid=rel7.5-supplier1,' + DEFAULT_SUFFIX
        SUPPLIER2_DN = 'uid=rel7.5-supplier2,' + DEFAULT_SUFFIX

        # Supplier 1
        try:
            topology.supplier1.add_s(Entry((SUPPLIER1_DN, {'objectclass':
                                                       ['top',
                                                        'extensibleObject'],
                                                       'sn': '1',
                                                       'cn': 'user 1',
                                                       'uid': 'rel7.5-supplier1',
                                                       'userpassword':
                                                       PASSWORD})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add replication test entry ' + SUPPLIER1_DN +
                      ': error ' + e.message['desc'])
            assert False

        log.info('################## Waiting for supplier 2 to converge...')

        while True:
            entry = None
            try:
                entry = topology.supplier2.search_s(SUPPLIER1_DN,
                                                  ldap.SCOPE_BASE,
                                                  'objectclass=*')
            except ldap.NO_SUCH_OBJECT:
                pass
            except ldap.LDAPError as e:
                log.fatal('Search Users: Search failed (%s): %s' %
                          (SUPPLIER1_DN, e.message['desc']))
                assert False
            if entry:
                break
            time.sleep(5)

        log.info('################## Supplier 2 converged.')

        # Supplier 2
        try:
            topology.supplier2.add_s(
                Entry((SUPPLIER2_DN, {'objectclass': ['top',
                                                    'extensibleObject'],
                                    'sn': '1',
                                    'cn': 'user 1',
                                    'uid': 'rel7.5-supplier2',
                                    'userpassword': PASSWORD})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add replication test entry ' + SUPPLIER1_DN +
                      ': error ' + e.message['desc'])
            assert False

        log.info('################## Waiting for supplier 1 to converge...')
        while True:
            entry = None
            try:
                entry = topology.supplier1.search_s(SUPPLIER2_DN,
                                                  ldap.SCOPE_BASE,
                                                  'objectclass=*')
            except ldap.NO_SUCH_OBJECT:
                pass
            except ldap.LDAPError as e:
                log.fatal('Search Users: Search failed (%s): %s' %
                          (SUPPLIER2_DN, e.message['desc']))
                assert False
            if entry:
                break
            time.sleep(5)

        log.info('################## Supplier 1 converged.')

    # Stop the full searches
    RUNNING = False
    fullSearch1.join()
    fullSearch2.join()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
