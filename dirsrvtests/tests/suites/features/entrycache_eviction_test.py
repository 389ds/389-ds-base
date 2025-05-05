# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import pytest
import ldap
import ldif
import random
from contextlib import suppress
from lib389.backend import Backends
from lib389.cli_base import FakeArgs
from lib389.cli_ctl.dbgen import dbgen_create_groups
from lib389._constants import DEFAULT_SUFFIX
from lib389.dirsrv_log import DirsrvErrorLog
from lib389.tasks import ImportTask
from lib389.topologies import topology_st


pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

os.environ['DS389_MDB_MAX_SIZE'] = str(200 * 1024 * 1024)

SEED = 195707

# Ensure that the dn, attributes and values are not sorted
class LdifScrambler(ldif.LDIFParser):

    def __init__(self, ldifin, ldifout):
        self.fdin = open(ldifin, 'rb')
        self.fdout = open(ldifout, 'wt')
        super().__init__(self.fdin)
        self.writer = ldif.LDIFWriter(self.fdout)
        self.others = []
        self.users = []
        self.user5 = []
        self.groups = []
        self.group5 = []

    def close_fds(self):
        self.fdin.close()
        self.fdout.close()

    def handle(self,dn,entry):
        if 'cn=myGroup-5,ou=groups,dc=entrycache_test,dc=example,dc=com' in dn:
            self.group5.append((dn, entry))
        elif ',ou=groups,dc=entrycache_test,dc=example,dc=com' in dn:
            self.groups.append((dn, entry))
        elif 'uid=group_entry5-' in dn:
            self.user5.append((dn, entry))
        elif 'uid=group_entry' in dn:
            self.users.append((dn, entry))
        else:
            self.others.append((dn, entry))

    def scramble(self):
        self.parse()
        log.debug(f'{len(self.others)} entries in self.others')
        log.debug(f'{len(self.users)} entries in self.users')
        log.debug(f'{len(self.user5)} entries in self.user5')
        log.debug(f'{len(self.groups)} entries in self.groups')
        log.debug(f'{len(self.group5)} entries in self.group5')
        for dn, entry in self.others:
            self.writer.unparse(dn, entry)
        self.scramble_entry(self.users)
        self.scramble_entry(self.groups)
        self.scramble_entry(self.user5)
        self.scramble_entry(self.group5)

    def scramble_entry(self, group):
        random.shuffle(group)
        for dn, entry in group:
            for vals in entry.values():
                random.shuffle(vals)
            self.writer.unparse(dn, entry)


@pytest.fixture(scope="function")
def prepare_be(topology_st, request):

    inst = topology_st.standalone
    ldif_file = inst.get_ldif_dir() + '/30ku.ldif'
    ldif_file_scrambled = inst.get_ldif_dir() + '/30ku-scrambled.ldif'
    bename = 'entrycache_test'
    suffix = 'dc=entrycache_test,dc=example,dc=com'
    people_base = f'ou=people,{suffix}'
    groups_base = f'ou=groups,{suffix}'

    # Remove the backend if it exists
    bes = Backends(inst)
    with suppress(ldap.NO_SUCH_OBJECT):
        be1 = bes.get(bename)
        be1.delete()

    # Creates the backend.
    be1 = bes.create(properties={ 'cn': bename, 'nsslapd-suffix': suffix, })

    # Prepare finalizer
    def fin():
        be1.delete()
        if os.path.exists(ldif_file):
            os.remove(ldif_file)

    if not DEBUGGING:
        request.addfinalizer(fin)

    # Generates ldif file with a few clarge groups
    # And preset random seed to have deterministic test
    random.seed(SEED)
    args = FakeArgs()
    args.NAME = 'myGroup'
    args.parent = groups_base
    args.suffix = suffix
    args.number = 5
    args.num_members = 6000
    args.create_members = True
    args.member_attr = 'uniquemember'
    args.member_parent = people_base
    args.ldif_file = ldif_file
    dbgen_create_groups(inst, log, args)
    assert os.path.exists(ldif_file)

    # Set entry cache large enough to hold all the large groups
    # and with a limited number of entries to trigger eviction
    be1.replace('nsslapd-cachememsize',  '8000000' )
    be1.replace('nsslapd-cachesize',  '100' )
    # Set debugging trace specific for this test
    be1.replace('nsslapd-cache-debug-pattern',  f'cn=.*,ou=groups,{suffix}' )

    # import the ldif
    inst.stop()
    LdifScrambler(ldif_file, ldif_file_scrambled).scramble()
    if not inst.ldif2db(bename, None, None, None, ldif_file):
        log.fatal('Failed to import {ldif_file}')
        assert False
    inst.start()

    return (bename, suffix, be1, people_base, groups_base)


def grab_debug_logs(inst):
    errlog = DirsrvErrorLog(inst)
    msgs = errlog.match('.*entrycache_.*_int')
    adds = [ line for line in msgs if 'entrycache_add_int' in line ]
    dels= [ line for line in msgs if 'entrycache_remove_int' in line ]
    return (msgs, adds, dels)


def test_entry_cache_eviction(topology_st, prepare_be):
    """Test that large groups are not evicted

            :id: 550b995e-1c76-11f0-93ed-482ae39447e5
            :setup: Standalone instance
            :steps:
                 1. Create DS instance and prepare a test backend
                 2. Search all entries
                 3. Check error log that the group are added in cache but not removed
                 4. Change the number of entries protected from eviction threshold
                    so that no group is preserved.
                 5. Search all entries
                 6. Check error log that the group are removed except the last one
                 7. Change the number of entries protected from eviction threshold
                    so that one group is not preserved.
                 8. Search all entries
                 9. Check error log that the group are preserved except one
            :expectedresults:
                 1. Success
                 2. Success
                 3. Success
                 4. Success
                 5. Success
                 6. Success
                 7. Success
                 8. Success
                 9. Success
            """

    inst = topology_st.standalone
    bename, suffix, be1, people_base, groups_base = prepare_be

    # Search all groups then all people to try to evict the groups from entrycache
    inst.search_s(groups_base, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'], escapehatch='i am sure')
    inst.search_s(people_base, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'], escapehatch='i am sure')

    # Check error logs
    msgs, adds, dels = grab_debug_logs(inst)
    # Should have entrycache_add_int for each group and no entrycache_delete_int
    assert len(adds) == 5
    assert len(dels) == 0


    be1.replace('nsslapd-cache-preserved-entries', '0')
    # Search all people to evict groups from entrycache
    inst.search_s(people_base, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'], escapehatch='i am sure')
    # Check error logs
    msgs, adds, dels = grab_debug_logs(inst)
    assert len(adds) == 5
    assert len(dels) == 5

    # Search all groups then all people to try to evict the groups from entrycache
    inst.search_s(groups_base, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'], escapehatch='i am sure')
    inst.search_s(people_base, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'], escapehatch='i am sure')
    # Check error logs
    msgs, adds, dels = grab_debug_logs(inst)
    #  Should have the 5 add messages from previous searches + 5 new add
    #  Should have the 5 del messages from previous searches + 5 new del
    assert len(adds) == 5+5
    assert len(dels) == 5+5

    be1.replace('nsslapd-cache-preserved-entries', '4')
    # Search all groups then all people to try to evict the groups from entrycache
    inst.search_s(groups_base, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'], escapehatch='i am sure')
    inst.search_s(people_base, ldap.SCOPE_SUBTREE, '(objectclass=top)', ['dn'], escapehatch='i am sure')
    # Check error logs
    msgs, adds, dels = grab_debug_logs(inst)
    # Should have entrycache_add_int for each group and one group is removed
    # because only 4 of them are preserved.
    assert len(adds) == 10+5
    assert len(dels) == 10+1

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

