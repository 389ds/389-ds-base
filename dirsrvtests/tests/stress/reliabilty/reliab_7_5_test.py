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
    def __init__(self, master1, master2):
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating master 1...
    master1 = DirSrv(verbose=DEBUGGING)
    args_instance[SER_HOST] = HOST_MASTER_1
    args_instance[SER_PORT] = PORT_MASTER_1
    args_instance[SER_SECURE_PORT] = SECUREPORT_MASTER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_1
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master1.allocate(args_master)
    instance_master1 = master1.exists()
    if instance_master1:
        master1.delete()
    master1.create()
    master1.open()
    master1.replica.enableReplication(suffix=SUFFIX, role=ReplicaRole.MASTER,
                                      replicaId=REPLICAID_MASTER_1)

    # Creating master 2...
    master2 = DirSrv(verbose=DEBUGGING)
    args_instance[SER_HOST] = HOST_MASTER_2
    args_instance[SER_PORT] = PORT_MASTER_2
    args_instance[SER_SECURE_PORT] = SECUREPORT_MASTER_2
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_2
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master2.allocate(args_master)
    instance_master2 = master2.exists()
    if instance_master2:
        master2.delete()
    master2.create()
    master2.open()
    master2.replica.enableReplication(suffix=SUFFIX, role=ReplicaRole.MASTER,
                                      replicaId=REPLICAID_MASTER_2)

    #
    # Create all the agreements
    #
    # Creating agreement from master 1 to master 2
    properties = {RA_NAME: r'meTo_$host:$port',
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host,
                                          port=master2.port,
                                          properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME: r'meTo_$host:$port',
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host,
                                          port=master1.port,
                                          properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Import tests entries into master1 before we initialize master2
    #
    ldif_dir = master1.get_ldif_dir()

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
        ldif.write('dn: uid=master1_entry' + count + ',' +
                   DEFAULT_SUFFIX + '\n')
        ldif.write('objectclass: top\n')
        ldif.write('objectclass: person\n')
        ldif.write('objectclass: inetorgperson\n')
        ldif.write('objectclass: organizationalperson\n')
        ldif.write('uid: master1_entry' + count + '\n')
        ldif.write('cn: master1 entry' + count + '\n')
        ldif.write('givenname: master1 ' + count + '\n')
        ldif.write('sn: entry ' + count + '\n')
        ldif.write('userpassword: master1_entry' + count + '\n')
        ldif.write('description: ' + 'a' * random.randint(1, 1000) + '\n')
        ldif.write('\n')

        ldif.write('dn: uid=master2_entry' + count + ',' +
                   DEFAULT_SUFFIX + '\n')
        ldif.write('objectclass: top\n')
        ldif.write('objectclass: person\n')
        ldif.write('objectclass: inetorgperson\n')
        ldif.write('objectclass: organizationalperson\n')
        ldif.write('uid: master2_entry' + count + '\n')
        ldif.write('cn: master2 entry' + count + '\n')
        ldif.write('givenname: master2 ' + count + '\n')
        ldif.write('sn: entry ' + count + '\n')
        ldif.write('userpassword: master2_entry' + count + '\n')
        ldif.write('description: ' + 'a' * random.randint(1, 1000) + '\n')
        ldif.write('\n')
        idx += 1

    ldif.close()

    # Now import it
    try:
        master1.tasks.importLDIF(suffix=DEFAULT_SUFFIX, input_file=import_ldif,
                                 args={TASK_WAIT: True})
    except ValueError:
        log.fatal('test_reliab_7.5: Online import failed')
        assert False

    #
    # Initialize all the agreements
    #
    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    # Delete each instance in the end
    def fin():
        master1.delete()
        master2.delete()
        if ENABLE_VALGRIND:
            sbin_dir = get_sbin_dir(prefix=master1.prefix)
            valgrind_disable(sbin_dir)
    request.addfinalizer(fin)

    return TopologyReplication(master1, master2)


class AddDelUsers(threading.Thread):
    def __init__(self, inst, masterid):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.id = masterid

    def run(self):
        # Add 5000 entries
        idx = 0
        RDN = 'uid=add_del_master_' + self.id + '-'

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
                log.fatal('Add users to master ' + self.id + ' failed (' +
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
                log.fatal('Failed to delete (' + USER_DN + ') on master ' +
                          self.id + ': error ' + e.message['desc'])
            idx += 1
        conn.close()


class ModUsers(threading.Thread):
    # Do mods and modrdns
    def __init__(self, inst, masterid):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.id = masterid

    def run(self):
        # Mod existing entries
        conn = DirectoryManager(self.inst).bind()
        idx = 0
        while idx < NUM_USERS:
            USER_DN = ('uid=master' + self.id + '_entry' + str(idx) + ',' +
                       DEFAULT_SUFFIX)
            try:
                conn.modify(USER_DN, [(ldap.MOD_REPLACE,
                                       'givenname',
                                       'new givenname master1-' + str(idx))])
            except ldap.LDAPError as e:
                log.fatal('Failed to modify (' + USER_DN + ') on master ' +
                          self.id + ': error ' + e.message['desc'])
            idx += 1
        conn.close()

        # Modrdn existing entries
        conn = DirectoryManager(self.inst).bind()
        idx = 0
        while idx < NUM_USERS:
            USER_DN = ('uid=master' + self.id + '_entry' + str(idx) + ',' +
                       DEFAULT_SUFFIX)
            NEW_RDN = 'cn=master' + self.id + '_entry' + str(idx)
            try:
                conn.rename_s(USER_DN, NEW_RDN, delold=1)
            except ldap.LDAPError as e:
                log.error('Failed to modrdn (' + USER_DN + ') on master ' +
                          self.id + ': error ' + e.message['desc'])
            idx += 1
        conn.close()

        # Undo modrdn to we can rerun this test
        conn = DirectoryManager(self.inst).bind()
        idx = 0
        while idx < NUM_USERS:
            USER_DN = ('cn=master' + self.id + '_entry' + str(idx) + ',' +
                       DEFAULT_SUFFIX)
            NEW_RDN = 'uid=master' + self.id + '_entry' + str(idx)
            try:
                conn.rename_s(USER_DN, NEW_RDN, delold=1)
            except ldap.LDAPError as e:
                log.error('Failed to modrdn (' + USER_DN + ') on master ' +
                          self.id + ': error ' + e.message['desc'])
            idx += 1
        conn.close()


class DoSearches(threading.Thread):
    # Search a master
    def __init__(self, inst, masterid):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.id = masterid

    def run(self):
        # Equality
        conn = DirectoryManager(self.inst).bind()
        idx = 0
        while idx < NUM_USERS:
            search_filter = ('(|(uid=master' + self.id + '_entry' + str(idx) +
                             ')(cn=master' + self.id + '_entry' + str(idx) +
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
            search_filter = ('(|(uid=master' + self.id + '_entry' + str(idx) +
                             '*)(cn=master' + self.id + '_entry' + str(idx) +
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
    # Search a master
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

    # Update master 1
    try:
        topology.master1.modify_s(BACKEND_DN, [(ldap.MOD_REPLACE,
                                                'nsslapd-cachememsize',
                                                '512000'),
                                               (ldap.MOD_REPLACE,
                                                'nsslapd-cachesize',
                                                '500')])
    except ldap.LDAPError as e:
        log.fatal('Failed to set cache settings: error ' + e.message['desc'])
        assert False

    # Update master 2
    try:
        topology.master2.modify_s(BACKEND_DN, [(ldap.MOD_REPLACE,
                                                'nsslapd-cachememsize',
                                                '512000'),
                                               (ldap.MOD_REPLACE,
                                                'nsslapd-cachesize',
                                                '500')])
    except ldap.LDAPError as e:
        log.fatal('Failed to set cache settings: error ' + e.message['desc'])
        assert False

    # Restart the masters to pick up the new cache settings
    topology.master1.stop(timeout=10)
    topology.master2.stop(timeout=10)

    # This is the time to enable valgrind (if enabled)
    if ENABLE_VALGRIND:
        sbin_dir = get_sbin_dir(prefix=topology.master1.prefix)
        valgrind_enable(sbin_dir)

    topology.master1.start(timeout=30)
    topology.master2.start(timeout=30)


def test_reliab7_5_run(topology):
    '''
    Starting issuing adds, deletes, mods, modrdns, and searches
    '''
    global RUNNING
    count = 1
    RUNNING = True

    # Start some searches to run through the entire stress test
    fullSearch1 = DoFullSearches(topology.master1)
    fullSearch1.start()
    fullSearch2 = DoFullSearches(topology.master2)
    fullSearch2.start()

    while count <= MAX_PASSES:
        log.info('################## Reliabilty 7.5 Pass: %d' % count)

        # Master 1
        add_del_users1 = AddDelUsers(topology.master1, '1')
        add_del_users1.start()
        mod_users1 = ModUsers(topology.master1, '1')
        mod_users1.start()
        search1 = DoSearches(topology.master1, '1')
        search1.start()

        # Master 2
        add_del_users2 = AddDelUsers(topology.master2, '2')
        add_del_users2.start()
        mod_users2 = ModUsers(topology.master2, '2')
        mod_users2.start()
        search2 = DoSearches(topology.master2, '2')
        search2.start()

        # Search the masters
        search3 = DoSearches(topology.master1, '1')
        search3.start()
        search4 = DoSearches(topology.master2, '2')
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
        # Add an entry to each master, and wait for it to replicate
        MASTER1_DN = 'uid=rel7.5-master1,' + DEFAULT_SUFFIX
        MASTER2_DN = 'uid=rel7.5-master2,' + DEFAULT_SUFFIX

        # Master 1
        try:
            topology.master1.add_s(Entry((MASTER1_DN, {'objectclass':
                                                       ['top',
                                                        'extensibleObject'],
                                                       'sn': '1',
                                                       'cn': 'user 1',
                                                       'uid': 'rel7.5-master1',
                                                       'userpassword':
                                                       PASSWORD})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add replication test entry ' + MASTER1_DN +
                      ': error ' + e.message['desc'])
            assert False

        log.info('################## Waiting for master 2 to converge...')

        while True:
            entry = None
            try:
                entry = topology.master2.search_s(MASTER1_DN,
                                                  ldap.SCOPE_BASE,
                                                  'objectclass=*')
            except ldap.NO_SUCH_OBJECT:
                pass
            except ldap.LDAPError as e:
                log.fatal('Search Users: Search failed (%s): %s' %
                          (MASTER1_DN, e.message['desc']))
                assert False
            if entry:
                break
            time.sleep(5)

        log.info('################## Master 2 converged.')

        # Master 2
        try:
            topology.master2.add_s(
                Entry((MASTER2_DN, {'objectclass': ['top',
                                                    'extensibleObject'],
                                    'sn': '1',
                                    'cn': 'user 1',
                                    'uid': 'rel7.5-master2',
                                    'userpassword': PASSWORD})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add replication test entry ' + MASTER1_DN +
                      ': error ' + e.message['desc'])
            assert False

        log.info('################## Waiting for master 1 to converge...')
        while True:
            entry = None
            try:
                entry = topology.master1.search_s(MASTER2_DN,
                                                  ldap.SCOPE_BASE,
                                                  'objectclass=*')
            except ldap.NO_SUCH_OBJECT:
                pass
            except ldap.LDAPError as e:
                log.fatal('Search Users: Search failed (%s): %s' %
                          (MASTER2_DN, e.message['desc']))
                assert False
            if entry:
                break
            time.sleep(5)

        log.info('################## Master 1 converged.')

    # Stop the full searches
    RUNNING = False
    fullSearch1.join()
    fullSearch2.join()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
