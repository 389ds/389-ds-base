# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest, time
import os
import glob
import ldap
from shutil import copyfile, rmtree
from itertools import permutations, product
from contextlib import contextmanager
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m3, topology_m2, set_timeout
from lib389.replica import *
from lib389._constants import *
from lib389.properties import TASK_WAIT
from lib389.index import *
from lib389.mappingTree import *
from lib389.backend import *
from lib389.conflicts import ConflictEntry
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.organization import Organization
from lib389.agreement import Agreements
from lib389.idm.organizationalunit import OrganizationalUnits


logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)
DEBUGGING = os.getenv("DEBUGGING", default=False)
SKIP_THE_TESTS = os.getenv('URP_VERY_LONG_TEST') is None

# Replication synchronization timeout in seconds
REPL_SYNC_TIMEOUT = 300

# test_urp_delete is a long test spending days
set_timeout(24*3600*10)


def normalize_values(v):
    # Return lower case sorted values.
    if isinstance(v, bytes):
        return v.decode('utf-8').lower()
    result = [ normalize_values(val) for val in v ]
    result.sort()
    return result


def get_entry_info(entry):
    norm_entry = { k.lower():normalize_values(v) for k,v in entry.items() }
    uuid = norm_entry['nsuniqueid'][0]
    if 'nstombstone' in norm_entry['objectclass']:
        return "tombstone:"+uuid
    elif 'nsds5replconflict' in norm_entry:
        return "conflict:"+uuid
    else:
        return "regular:"+uuid


def search_entries(inst, dnfilter):
    ldapurl, certdir = get_ldapurl_from_serverid(inst.serverid)
    assert 'ldapi://' in ldapurl
    conn = ldap.initialize(ldapurl)
    conn.sasl_interactive_bind_s("", ldap.sasl.external())
    filter = "(|(objectclass=*)(objectclass=ldapsubentry)(objectclass=nstombstone))"
    attrlist = ['nsuniqueid','objectclass', 'nsds5replconflict' ]
    result = conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filterstr=filter, attrlist=attrlist)
    entry_infos = [ get_entry_info(entry) for dn,entry in result if dnfilter.lower() in dn.lower() ]
    entry_infos.sort()
    return entry_infos


def list_all_order_combinations(m, n):
    # list all way to order pair of m per n elements
    # knowing that element (m, 0) should be the first for m
    # But m=3, n=4 generates 1247400 combinations 
    # and testing them would requires 3.5 years ...
    # So we are using list_order_combinations which is a subset
    actions = list(product(range(m), range(n)))
    actions.remove((0,0))
    for order in permutations(actions):
        order = ((0,0),) + order
        higher_instance_seen = 0
        ok = True
        for (instance,action) in order:
            if instance > higher_instance_seen+1:
                ok = False
                break
            if instance > higher_instance_seen:
                if action != 0:
                    ok = False
                    break
                higher_instance_seen = instance
        if ok is True:
            yield order


def list_order_combinations(m, n):
    # list all way to order pair of m per n elements
    # while keeping the same action sequence on a given instances
    # ( so action i on instance j is always done after action i-1 on instance j )
    # For m=3, n=4 There is 5774 orders The test requires 6 days
    class OrderedCombination:
        def __init__(self, m, n):
            self.m = m
            self.n = n

        def bypass(self, idx):
            # Keep the first action in order 
            # Changing the order of the first action is 
            # equivalent to swapping the instances so lets avoid to
            # do it to decrease the number of tests to perform.
            for i in range(self.n):
                for j in range(i+1, self.n):
                    if idx[j] > 0 and idx[i] == 0:
                        return True
            return False

        def run(self, s=None, idx=None):
            # Run self.action on every possible order for the n steps on m steps sets
            result = []
            if s is None:
                s = []
            if idx is None:
                idx = [ 0 ] * self.n
            stop = True
            if self.bypass(idx):
                return result
            for i in range(self.n):
                if idx[i] < self.m:
                    stop = False
                    idx2 = idx.copy()
                    s2 = s.copy()
                    idx2[i] += 1
                    s2.append((i+1, idx2[i]))
                    result.extend(self.run(s=s2, idx=idx2))
            if stop:
               result.append(s)
            return result

    if SKIP_THE_TESTS:
        # Lets avoid generating 5000 tests to skip 
        return [ 'skipped', ]
    return OrderedCombination(m,n).run()


@pytest.fixture(scope="module")
def urp_tester(topology_m3):
    class UrpTesterInstances:
        # Contains the data and the methods about a specific instance 
        def __init__(self, tester, inst):
            self.tester = tester
            self.inst = inst
            self.users = UserAccounts(inst, DEFAULT_SUFFIX)
            ldapurl, certdir = get_ldapurl_from_serverid(inst.serverid)
            assert 'ldapi://' in ldapurl
            self.conn = ldap.initialize(ldapurl)
            self.conn.sasl_interactive_bind_s("", ldap.sasl.external())
            self.entriesinfo = []
            self.replicas = [ inst0 for inst0 in tester.topo if inst0 != inst ]

        def add_user(self):
            user_properties = {
                'uid': 'testuser',
                'cn': 'testuser',
                'sn': 'testuser',
                'uidNumber': '1',
                'gidNumber': '1',
                'homeDirectory': '/home/testuser'
            }
            # Wait 1 second to ensure that csn time are differents
            time.sleep(1)
            self.user = self.users.create(properties=user_properties)
            log.info(f"Adding entry {self.user.dn} on {self.inst.serverid}")
            self.uuid = self.user.get_attr_val_utf8_l('nsuniqueid')

        def sync1(self):
            self.tester.resync_agmt(self.replicas[0], self.inst)

        def sync2(self):
            self.tester.resync_agmt(self.replicas[1], self.inst)

        def remove_user(self):
            filter = f'(&(nsuniqueid={self.uuid})(|(objectclass=nsTombstone)(objectclass=ldapsubentry)(objectclass=*)))'
            res = self.conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filter, ['dn',])
            assert len(res) == 1
            dn,entry = res[0]
            log.info(f'Removing entry: {dn}')
            self.conn.delete_s(dn)

        def get_entries(self):
            self.entry_info = search_entries(self.inst, 'uid=testuser')

        def run_action(self, action):
            actions = ( ( None, None ),
                        ( "ADD ENTRY", self.add_user ),
                        ( "SYNC ENTRY FROM REPLICA ASSOCIATED WITH FIRST AGMT", self.sync1 ),
                        ( "SYNC ENTRY FROM REPLICA ASSOCIATED WITH SECOND AGMT", self.sync2 ),
                        ( "REMOVE ENTRY", self.remove_user ), )
            log.info (f'***** {actions[action][0]} on {self.inst.serverid}')
            actions[action][1]()


    class UrpTester:
        # Contains the data and the methods for all instances
        def __init__(self, topo):
            self.topo = topo
            self.repl = ReplicationManager(DEFAULT_SUFFIX)
            self.insts = [ UrpTesterInstances(self, inst) for inst in topo ]
            inst = topo[0]
            self.ldif = f'{inst.get_ldif_dir()}/userroot.ldif'
            tasks = Tasks(inst)
            tasks.exportLDIF(DEFAULT_SUFFIX, output_file=self.ldif, args={EXPORT_REPL_INFO: True, TASK_WAIT: True})

        def resync_agmt(self, instfrom, instto):
            log.info(f"Enabling replication agreement from {instfrom.serverid} to {instto.serverid}")
            self.repl.enable_to_supplier(instto, [instfrom,])
            self.repl.wait_for_replication(instfrom, instto, timeout=REPL_SYNC_TIMEOUT)

        def disable_all_agmts(self):
            log.info(f"Disabling replication all replication agreements")
            for inst in self.topo:
                for agmt in Agreements(inst).list():
                    agmt.pause()

        def resync_all_agmts(self):
            ilist = [ inst for inst in self.topo ]
            for inst in ilist:
                for inst2 in ilist:
                    if inst != inst2:
                        self.resync_agmt(inst, inst2)

        def get_entries(self):
            for inst in self.insts:
                inst.get_entries()

        def reset(self):
            # Reinitilize all the replicas to original data and disable all agmts
            self.disable_all_agmts()
            # In theory importing on all replicas should work but sometime the replication
            # fails to restart (probably worth to investigate) - so let use total reinit instead.
            # for inst in self.topo:
            #     tasks = Tasks(inst)
            #     tasks.importLDIF(DEFAULT_SUFFIX, input_file=self.ldif, args={TASK_WAIT: True})
            # self.resync_all_agmts()
            inst = self.topo[0]
            agmts = Agreements(inst).list()
            for agmt in agmts:
                agmt.resume()
            tasks = Tasks(inst)
            tasks.importLDIF(DEFAULT_SUFFIX, input_file=self.ldif, args={TASK_WAIT: True})
            for agmt in Agreements(inst).list():
                agmt.begin_reinit()
                (done, error) = agmt.wait_reinit()
                assert done is True 
                assert error is False
            for agmt in agmts:
                agmt.pause()

    return UrpTester(topology_m3)

@pytest.mark.skipif(SKIP_THE_TESTS, reason="This test is meant to execute in specific test environment")
@pytest.mark.parametrize("actionorder", list_order_combinations(4,3))
def test_urp_delete(urp_tester, actionorder):
    """Test urp conflict handling for add and delete operations

    :id: 7b08b6ac-f362-11ee-bf7c-482ae39447e5
    :setup: Three suppliers
    :parametrized: yes
    :steps:
        On every possible combinations (i.e 5775) for the actions on all instances
          1. Reinitialise the instances
          2. Run the action on specified order and specified instances
             The actions are:
                  - Add the entry on current replica
                  - Sync first other replica changes with current replica
                  - Sync second other replica changes with current replica
                  - Find the entry added on this replica and remove it
          3. Wait until instances are in sync
          4. Check that entry type,nsuniqueids pair are the same on all replica
                  (entry type is either tombstone, conflct, or regular)
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
    """
    log.info(f'actions order is: {actionorder}')
    urp_tester.reset()
    # Run the actions on specified server with specified order
    for inst_idx,action in actionorder:
        urp_tester.insts[inst_idx-1].run_action(action)
    # Wait until replication is in sync
    urp_tester.resync_all_agmts()
    # Check that all replica have the sames entries
    urp_tester.get_entries()
    assert urp_tester.insts[0].entry_info == urp_tester.insts[1].entry_info
    assert urp_tester.insts[0].entry_info == urp_tester.insts[2].entry_info


def gen_ldif_file(first, second, result):
    with open(result, 'w') as output:
        for file in [ first, second ]:
            with open(file, 'r') as input:
                for line in input:
                    output.write(line)



@pytest.mark.skipif(SKIP_THE_TESTS, reason="This test is meant to execute in specific test environment")
def test_urp_with_crossed_entries(topology_m2):
    """Test urp behaviour if entry conflict entries are crossed

    :id: 6b4adfc2-fd9b-11ee-a6f0-482ae39447e5
    :setup: Two suppliers
    :steps:
        1. Generate ldif files with crossed conflict entries
        2. Import the ldif files
        3. Remove a conflict entry
        4. Wait until le replication is in sync
        5. Check that entries have the same type (tombstone/conflict/regular) on both replica.
        6. Import the ldif files
        7. Remove a regular entry
        8. Wait until le replication is in sync
        9. Check that entries have the same type (tombstone/conflict/regular) on both replica.
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
    """
    # Note in theory crossed entries should not happen
    # But this test shows that we are in trouble if that is the case
    s1 = topology_m2.ms["supplier1"]
    s2 = topology_m2.ms["supplier2"]
    datadir = os.path.join(os.path.dirname(__file__), '../../data/urp')
    # 1. Generate ldif files with crossed conflict entries
    # Export the ldif to be sure that the replication credentials are ok
    export_ldif = f'{s1.get_ldif_dir()}/db.ldif'
    import_ldif1 = f'{s1.get_ldif_dir()}/db1.ldif'
    import_ldif2 = f'{s1.get_ldif_dir()}/db2.ldif'
    Tasks(s1).exportLDIF(DEFAULT_SUFFIX, output_file=export_ldif, args={EXPORT_REPL_INFO: True, TASK_WAIT: True})
    gen_ldif_file(export_ldif, f'{datadir}/db1.ldif', import_ldif1)
    gen_ldif_file(export_ldif, f'{datadir}/db2.ldif', import_ldif2)
    # 2. import the ldif files
    Tasks(s1).importLDIF(DEFAULT_SUFFIX, input_file=import_ldif1, args={TASK_WAIT: True})
    Tasks(s2).importLDIF(DEFAULT_SUFFIX, input_file=import_ldif2, args={TASK_WAIT: True})
    # 3. Remove a conflict entry
    ConflictEntry(s1, 'cn=u22449+nsUniqueId=c6654281-f11b11ee-ad93a02d-7ba2db25,dc=example,dc=com').delete()
    # 4. Wait until le replication is in sync
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(s1, s2)
    # 5. Check that entries have the same type (tombstone/conflict/regular) on both replica.
    u1 = search_entries(s1, 'cn=u22449')
    u2 = search_entries(s2, 'cn=u22449')
    assert u1 == u2
    # 6. import the ldif files
    Tasks(s1).importLDIF(DEFAULT_SUFFIX, input_file=import_ldif1, args={TASK_WAIT: True})
    Tasks(s2).importLDIF(DEFAULT_SUFFIX, input_file=import_ldif2, args={TASK_WAIT: True})
    # 7. Remove a regular entry
    UserAccount(s2, 'cn=u22449,dc=example,dc=com').delete()
    # 8. Wait until le replication is in sync
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(s1, s2)
    # 9. Check that entries have the same type (tombstone/conflict/regular) on both replica.
    u1 = search_entries(s1, 'cn=u22449')
    u2 = search_entries(s2, 'cn=u22449')
    assert u1 == u2


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
