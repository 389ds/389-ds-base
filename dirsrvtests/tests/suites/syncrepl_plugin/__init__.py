# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

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

class ISyncRepl(DirSrv, SyncreplConsumer):
    """
    This implements a test harness for checking syncrepl, and allowing us to check various actions or
    behaviours. During a "run" it stores the results in it's instance, so that they can be inspected
    later to ensure that syncrepl worked as expected.
    """
    def __init__(self, inst, openldap=False):
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
    r = st.search_ext_s(
        base=DEFAULT_SUFFIX,
        scope=ldap.SCOPE_SUBTREE,
        filterstr='(objectClass=*)',
        attrsonly=1,
        escapehatch='i am sure'
    )

    # Initial sync
    log.debug("*test* initial")
    sync.syncrepl_search()
    sync.syncrepl_complete()
    # check we caught them all
    assert len(r) == len(sync.entries.keys())
    assert len(r) == len(sync.present)
    assert 0 == len(sync.delete)

    # Add a new entry

    account = nsUserAccounts(st, DEFAULT_SUFFIX).create_test_user()
    # Check
    log.debug("*test* add")
    sync.syncrepl_search()
    sync.syncrepl_complete()
    sync.check_cookie()
    assert 1 == len(sync.entries.keys())
    assert 1 == len(sync.present)
    assert 0 == len(sync.delete)

    # Mod
    account.replace('description', 'change')
    # Check
    log.debug("*test* mod")
    sync.syncrepl_search()
    sync.syncrepl_complete()
    sync.check_cookie()
    assert 1 == len(sync.entries.keys())
    assert 1 == len(sync.present)
    assert 0 == len(sync.delete)

    ## Delete
    account.delete()

    # Check
    log.debug("*test* del")
    sync.syncrepl_search()
    sync.syncrepl_complete()
    # In a delete, the cookie isn't updated (?)
    sync.check_cookie()
    log.debug(f'{sync.entries.keys()}')
    log.debug(f'{sync.present}')
    log.debug(f'{sync.delete}')
    assert 0 == len(sync.entries.keys())
    assert 0 == len(sync.present)
    assert 1 == len(sync.delete)

