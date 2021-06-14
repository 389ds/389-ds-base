# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
"""
   :Requirement: 389-ds-base: Sync Replication Plugin
"""
import logging
import ldap
import time
from ldap.syncrepl import SyncreplConsumer
import pytest
from lib389 import DirSrv
from lib389.idm.user import nsUserAccounts, UserAccounts
from lib389.topologies import topology_st as topology
from lib389.paths import Paths
from lib389.utils import ds_is_older
from lib389.plugins import RetroChangelogPlugin, ContentSyncPlugin
from lib389._constants import *

log = logging.getLogger(__name__)

OU_PEOPLE = "ou=people,%s" % DEFAULT_SUFFIX

class ISyncRepl(DirSrv, SyncreplConsumer):
    """
    This implements a test harness for checking syncrepl, and allowing us to check various actions or
    behaviours. During a "run" it stores the results in it's instance, so that they can be inspected
    later to ensure that syncrepl worked as expected.
    """
    def __init__(self, inst, openldap=False):
        ### 🚧 WARNING 🚧
        # There are bugs with python ldap sync repl in ALL VERSIONS below 3.3.1.
        # These tests WILL FAIL unless you have version 3.3.1 or higher!
        assert ldap.__version__ >= '3.3.1'

        self.inst = inst
        self.msgid = None

        self.last_cookie = None
        self.next_cookie = None
        self.cookie = None
        self.openldap = openldap
        if self.openldap:
            # In openldap mode, our initial cookie needs to be a rid.
            self.cookie = "rid=123"
        self.delete = []
        self.present = []
        self.entries = {}

        super().__init__()

    def result4(self, *args, **kwargs):
        return self.inst.result4(*args, **kwargs, escapehatch='i am sure')

    def search_ext(self, *args, **kwargs):
        return self.inst.search_ext(*args, **kwargs, escapehatch='i am sure')

    def syncrepl_search(self, base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE, mode='refreshOnly', cookie=None, **search_args):
        # Wipe the last result set.
        self.delete = []
        self.present = []
        self.entries = {}
        self.refdel = False
        self.next_cookie = None
        # Start the sync
        # If cookie is none, will call "get_cookie" we have.
        self.msgid = super().syncrepl_search(base, scope, mode, cookie, **search_args)
        log.debug(f'syncrepl_search -> {self.msgid}')
        assert self.msgid is not None

    def syncrepl_complete(self):
        log.debug(f'syncrepl_complete -> {self.msgid}')
        assert self.msgid is not None
        # Loop until the operation is complete.
        while super().syncrepl_poll(msgid=self.msgid) is True:
            pass
        assert self.next_cookie is not None
        self.last_cookie = self.cookie
        self.cookie = self.next_cookie

    def check_cookie(self):
        assert self.last_cookie != self.cookie

    def syncrepl_set_cookie(self, cookie):
        log.debug(f'set_cookie -> {cookie}')
        if self.openldap:
            assert self.cookie.startswith("rid=123")
        self.next_cookie = cookie

    def syncrepl_get_cookie(self):
        log.debug('get_cookie -> %s' % self.cookie)
        if self.openldap:
            assert self.cookie.startswith("rid=123")
        return self.cookie

    def syncrepl_present(self, uuids, refreshDeletes=False):
        log.debug(f'=====> refdel -> {refreshDeletes} uuids -> {uuids}')
        if refreshDeletes:
            # Indicate we recieved a refdel in the process.
            self.refdel = True
        if uuids is not None:
            self.present = self.present + uuids

    def syncrepl_delete(self, uuids):
        log.debug(f'delete -> {uuids}')
        self.delete = uuids

    def syncrepl_entry(self, dn, attrs, uuid):
        log.debug(f'entry -> {dn}')
        self.entries[dn] = (uuid, attrs)

    def syncrepl_refreshdone(self):
        log.debug('refreshdone')

def syncstate_assert(st, sync):
    # How many entries do we have?
    # We setup sync under ou=people so we can modrdn out of the scope.
    r = st.search_ext_s(
        base=OU_PEOPLE,
        scope=ldap.SCOPE_SUBTREE,
        filterstr='(objectClass=*)',
        attrsonly=1,
        escapehatch='i am sure'
    )

    # Initial sync
    log.debug("*test* initial")
    sync.syncrepl_search(base=OU_PEOPLE)
    sync.syncrepl_complete()
    # check we caught them all
    assert len(r) == len(sync.entries.keys())
    assert len(r) == len(sync.present)
    assert 0 == len(sync.delete)
    if sync.openldap:
        assert True == sync.refdel
    else:
        assert False == sync.refdel

    # Add a new entry
    account = nsUserAccounts(st, DEFAULT_SUFFIX).create_test_user()

    # Find the primary uuid we expect to see in syncrepl.
    # This will be None if not present.
    acc_uuid = account.get_attr_val_utf8('entryuuid')
    if not sync.openldap:
        nsid = account.get_attr_val_utf8('nsuniqueid')
        # nsunique has a diff format, so we change it up.
        # 431cf081-b44311ea-83fdb082-f24d490e
        # Add a hyphen V
        # 431cf081-b443-11ea-83fdb082-f24d490e
        nsid_a = nsid[:13] + '-' + nsid[13:]
        #           Add a hyphen V
        # 431cf081-b443-11ea-83fd-b082-f24d490e
        nsid_b = nsid_a[:23] + '-' + nsid_a[23:]
        #             Remove a hyphen V
        # 431cf081-b443-11ea-83fd-b082-f24d490e
        acc_uuid = nsid_b[:28] + nsid_b[29:]
        # Tada!
        # 431cf081-b443-11ea-83fd-b082f24d490e
        log.debug(f"--> expected sync uuid (from nsuniqueid): {acc_uuid}")
    else:
        log.debug(f"--> expected sync uuid (from entryuuid): {acc_uuid}")

    # Check
    log.debug("*test* add")
    sync.syncrepl_search(base=OU_PEOPLE)
    sync.syncrepl_complete()
    sync.check_cookie()
    log.debug(f"sd: {sync.delete}, sp: {sync.present} sek: {sync.entries.keys()}")

    assert 1 == len(sync.entries.keys())
    assert 1 == len(sync.present)
    ####################################
    assert sync.present == [acc_uuid]
    assert 0 == len(sync.delete)
    if sync.openldap:
        assert True == sync.refdel
    else:
        assert False == sync.refdel

    # Mod
    account.replace('description', 'change')
    # Check
    log.debug("*test* mod")
    sync.syncrepl_search(base=OU_PEOPLE)
    sync.syncrepl_complete()
    sync.check_cookie()
    log.debug(f"sd: {sync.delete}, sp: {sync.present} sek: {sync.entries.keys()}")
    assert 1 == len(sync.entries.keys())
    assert 1 == len(sync.present)
    ####################################
    assert sync.present == [acc_uuid]
    assert 0 == len(sync.delete)
    if sync.openldap:
        assert True == sync.refdel
    else:
        assert False == sync.refdel

    ## ModRdn (remain in scope)
    account.rename('uid=test1_modrdn')
    # newsuperior=None
    # Check
    log.debug("*test* modrdn (in scope)")
    sync.syncrepl_search(base=OU_PEOPLE)
    sync.syncrepl_complete()
    sync.check_cookie()
    log.debug(f"sd: {sync.delete}, sp: {sync.present} sek: {sync.entries.keys()}")
    assert 1 == len(sync.entries.keys())
    assert 1 == len(sync.present)
    ####################################
    assert sync.present == [acc_uuid]
    assert 0 == len(sync.delete)
    if sync.openldap:
        assert True == sync.refdel
    else:
        assert False == sync.refdel

    # import time
    # print("attach now ....")
    # time.sleep(45)

    ## Modrdn (out of scope, then back into scope)
    account.rename('uid=test1_modrdn', newsuperior=DEFAULT_SUFFIX)

    # Check it's gone.
    log.debug("*test* modrdn (move out of scope)")
    sync.syncrepl_search(base=OU_PEOPLE)
    sync.syncrepl_complete()
    sync.check_cookie()
    log.debug(f"sd: {sync.delete}, sp: {sync.present} sek: {sync.entries.keys()}")
    assert 0 == len(sync.entries.keys())
    assert 0 == len(sync.present)
    ## WARNING: This test MAY FAIL here if you do not have a new enough python-ldap
    # due to an ASN.1 parsing bug. You require at least python-ldap 3.3.1
    assert 1 == len(sync.delete)
    assert sync.delete == [acc_uuid]
    if sync.openldap:
        assert True == sync.refdel
    else:
        assert False == sync.refdel

    # Put it back
    account.rename('uid=test1_modrdn', newsuperior=OU_PEOPLE)
    log.debug("*test* modrdn (move in to scope)")
    sync.syncrepl_search(base=OU_PEOPLE)
    sync.syncrepl_complete()
    sync.check_cookie()
    log.debug(f"sd: {sync.delete}, sp: {sync.present} sek: {sync.entries.keys()}")
    assert 1 == len(sync.entries.keys())
    assert 1 == len(sync.present)
    ####################################
    assert sync.present == [acc_uuid]
    assert 0 == len(sync.delete)
    if sync.openldap:
        assert True == sync.refdel
    else:
        assert False == sync.refdel

    ## Delete
    account.delete()

    # Check
    log.debug("*test* del")
    sync.syncrepl_search(base=OU_PEOPLE)
    sync.syncrepl_complete()
    # In a delete, the cookie isn't updated (?)
    sync.check_cookie()
    log.debug(f'{sync.entries.keys()}')
    log.debug(f'{sync.present}')
    log.debug(f'{sync.delete}')
    log.debug(f"sd: {sync.delete}, sp: {sync.present} sek: {sync.entries.keys()}")
    assert 0 == len(sync.entries.keys())
    assert 0 == len(sync.present)
    assert 1 == len(sync.delete)
    assert sync.delete == [acc_uuid]
    ####################################
    if sync.openldap:
        assert True == sync.refdel
    else:
        assert False == sync.refdel

