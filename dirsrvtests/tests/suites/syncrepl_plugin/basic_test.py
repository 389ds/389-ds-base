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
import threading
from ldap.syncrepl import SyncreplConsumer
from ldap.ldapobject import ReconnectLDAPObject
import pytest
from lib389 import DirSrv
from lib389.idm.organizationalunit import OrganizationalUnits, OrganizationalUnit
from lib389.idm.user import nsUserAccounts, UserAccounts
from lib389.idm.group import Groups
from lib389.topologies import topology_st as topology
from lib389.paths import Paths
from lib389.utils import ds_is_older
from lib389.plugins import RetroChangelogPlugin, ContentSyncPlugin, AutoMembershipPlugin, MemberOfPlugin, MemberOfSharedConfig, AutoMembershipDefinitions, MEPTemplates, MEPConfigs, ManagedEntriesPlugin, MEPTemplate
from lib389._constants import *

from . import ISyncRepl, syncstate_assert

default_paths = Paths()
pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

@pytest.fixture(scope="function")
def init_sync_repl_plugins(topology, request):
    """Prepare test environment (retroCL/sync_repl/
    automember/memberof) and cleanup at the end of the test
      1.: enable retroCL
      2.: configure retroCL to log nsuniqueid as targetUniqueId
      3.: enable content_sync plugin
      4.: enable automember
      5.: create (2) groups. Few groups can help to reproduce the concurrent updates problem.
      6.: configure automember to provision those groups with 'member'
      7.: enable and configure memberof plugin
      8.: enable plugin log level
      9.: restart the server
      """
    inst = topology[0]
    inst.restart()

    # Enable/configure retroCL
    plugin = RetroChangelogPlugin(inst)
    plugin.disable()
    plugin.enable()
    plugin.set('nsslapd-attribute', 'nsuniqueid:targetuniqueid')

    # Enable sync plugin
    plugin = ContentSyncPlugin(inst)
    plugin.enable()

    # Enable automember
    plugin = AutoMembershipPlugin(inst)
    plugin.disable()
    plugin.enable()

    # Add the automember group
    groups = Groups(inst, DEFAULT_SUFFIX)
    group = []
    for i in range(1,5):
        group.append(groups.create(properties={'cn': 'group%d' % i}))

    # Add the automember config entry
    am_configs = AutoMembershipDefinitions(inst)
    am_configs_cleanup = []
    for g in group:
        am_config = am_configs.create(properties={'cn': 'config %s' % g.get_attr_val_utf8('cn'),
                                                  'autoMemberScope': DEFAULT_SUFFIX,
                                                  'autoMemberFilter': 'uid=*',
                                                  'autoMemberDefaultGroup': g.dn,
                                                  'autoMemberGroupingAttr': 'member:dn'})
        am_configs_cleanup.append(am_config)

    # Enable and configure memberof plugin
    plugin = MemberOfPlugin(inst)
    plugin.disable()
    plugin.enable()

    plugin.replace_groupattr('member')

    memberof_config = MemberOfSharedConfig(inst, 'cn=memberOf config,{}'.format(DEFAULT_SUFFIX))
    try:
        memberof_config.create(properties={'cn': 'memberOf config',
                                           'memberOfGroupAttr': 'member',
                                           'memberOfAttr': 'memberof'})
    except ldap.ALREADY_EXISTS:
        pass

    # Enable plugin log level (usefull for debug)
    inst.setLogLevel(65536)
    inst.restart()

    def fin():
        inst.restart()
        for am_config in am_configs_cleanup:
            am_config.delete()
        for g in group:
            try:
                g.delete()
            except:
                pass
    request.addfinalizer(fin)

@pytest.mark.skipif(ldap.__version__ < '3.3.1',
    reason="python ldap versions less that 3.3.1 have bugs in sync repl that will cause this to fail!")
def test_syncrepl_basic(topology):
    """ Test basic functionality of the SyncRepl interface

    :id: f9fea826-8ae2-412a-8e88-b8e0ba939b06

    :setup: Standalone instance

    :steps:
        1. Enable Retro Changelog
        2. Enable Syncrepl
        3. Run the syncstate test to check refresh, add, delete, mod.

    :expectedresults:
        1. Success
        1. Success
        1. Success
    """
    st = topology.standalone
    # Enable RetroChangelog.
    rcl = RetroChangelogPlugin(st)
    rcl.enable()
    # Set the default targetid
    rcl.replace('nsslapd-attribute', 'nsuniqueid:targetUniqueId')
    # Enable sync repl
    csp = ContentSyncPlugin(st)
    csp.enable()
    # Restart DS
    st.restart()
    # Setup the syncer
    sync = ISyncRepl(st)
    # Run the checks
    syncstate_assert(st, sync)

class TestSyncer(ReconnectLDAPObject, SyncreplConsumer):
    def __init__(self, *args, **kwargs):
        self.cookie = None
        self.cookies = []
        ldap.ldapobject.ReconnectLDAPObject.__init__(self, *args, **kwargs)

    def syncrepl_set_cookie(self, cookie):
        # extract the changenumber from the cookie
        self.cookie = cookie
        self.cookies.append(cookie.split('#')[2])
        log.info("XXXX Set cookie: %s" % cookie)

    def syncrepl_get_cookie(self):
        log.info("XXXX Get cookie: %s" % self.cookie)
        return self.cookie

    def syncrepl_present(self, uuids, refreshDeletes=False):
        log.info("XXXX syncrepl_present uuids %s %s" % ( uuids, refreshDeletes))

    def syncrepl_delete(self, uuids):
        log.info("XXXX syncrepl_delete uuids %s" % uuids)

    def syncrepl_entry(self, dn, attrs, uuid):
        log.info("XXXX syncrepl_entry dn %s" % dn)

    def syncrepl_refreshdone(self):
        log.info("XXXX syncrepl_refreshdone")

    def get_cookies(self):
        return self.cookies

class Sync_persist(threading.Thread, ReconnectLDAPObject, SyncreplConsumer):
    # This runs a sync_repl client in background
    # it registers a result that contain a list of the change numbers (from the cookie)
    # that are list as they are received
    def __init__(self, inst):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.cookie = None
        self.conn = inst.clone({SER_ROOT_DN: 'cn=directory manager', SER_ROOT_PW: 'password'})
        self.filterstr = '(|(objectClass=groupofnames)(objectClass=person))'
        self.attrs = [
            'objectclass',
            'cn',
            'displayname',
            'gidnumber',
            'givenname',
            'homedirectory',
            'mail',
            'member',
            'memberof',
            'sn',
            'uid',
            'uidnumber',
        ]
        self.conn.open()
        self.result = []

    def get_result(self):
        # used to return the cookies list to the requestor
        return self.result

    def run(self):
        """Start a sync repl client"""
        ldap_connection = TestSyncer(self.inst.toLDAPURL())
        ldap_connection.simple_bind_s('cn=directory manager', 'password')
        ldap_search = ldap_connection.syncrepl_search(
            "dc=example,dc=com",
            ldap.SCOPE_SUBTREE,
            mode='refreshAndPersist',
            attrlist=self.attrs,
            filterstr=self.filterstr,
            cookie=None
        )

        try:
            while ldap_connection.syncrepl_poll(all=1, msgid=ldap_search):
                pass
        except (ldap.SERVER_DOWN, ldap.CONNECT_ERROR) as e:
            print('syncrepl_poll: LDAP error (%s)', e)
        self.result = ldap_connection.get_cookies()
        log.info("ZZZ result = %s" % self.result)
        self.conn.unbind()

def test_sync_repl_mep(topology, request):
    """Test sync repl with MEP plugin that triggers several
    updates on the same entry

    :id: d9515930-293e-42da-9835-9f255fa6111b
    :setup: Standalone Instance
    :steps:
        1. enable retro/sync_repl/mep
        2. Add mep Template and definition entry
        3. start sync_repl client
        4. Add users with PosixAccount ObjectClass (mep will update it several times)
        5. Check that the received cookie are progressing
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    inst = topology[0]

    # Enable/configure retroCL
    plugin = RetroChangelogPlugin(inst)
    plugin.disable()
    plugin.enable()
    plugin.set('nsslapd-attribute', 'nsuniqueid:targetuniqueid')

    # Enable sync plugin
    plugin = ContentSyncPlugin(inst)
    plugin.enable()

    # Check the plug-in status
    mana = ManagedEntriesPlugin(inst)
    plugin.enable()

    # Add Template and definition entry
    org1 = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(properties={'ou': 'Users'})
    org2 = OrganizationalUnit(inst, f'ou=Groups,{DEFAULT_SUFFIX}')
    meps = MEPTemplates(inst, DEFAULT_SUFFIX)
    mep_template1 = meps.create(properties={
        'cn': 'UPG Template1',
        'mepRDNAttr': 'cn',
        'mepStaticAttr': 'objectclass: posixGroup',
        'mepMappedAttr': 'cn: $uid|gidNumber: $gidNumber|description: User private group for $uid'.split('|')})
    conf_mep = MEPConfigs(inst)
    mep_config = conf_mep.create(properties={
        'cn': 'UPG Definition2',
        'originScope': org1.dn,
        'originFilter': 'objectclass=posixaccount',
        'managedBase': org2.dn,
        'managedTemplate': mep_template1.dn})

    # Enable plugin log level (usefull for debug)
    inst.setLogLevel(65536)
    inst.restart()

    # create a sync repl client and wait 5 seconds to be sure it is running
    sync_repl = Sync_persist(inst)
    sync_repl.start()
    time.sleep(5)

    # Add users with PosixAccount ObjectClass and verify creation of User Private Group
    user = UserAccounts(inst, f'ou=Users,{DEFAULT_SUFFIX}', rdn=None).create_test_user()
    assert user.get_attr_val_utf8('mepManagedEntry') == f'cn=test_user_1000,ou=Groups,{DEFAULT_SUFFIX}'

    # stop the server to get the sync_repl result set (exit from while loop).
    # Only way I found to acheive that.
    # and wait a bit to let sync_repl thread time to set its result before fetching it.
    inst.stop()
    time.sleep(10)
    cookies = sync_repl.get_result()

    # checking that the cookie are in increasing and in an acceptable range (0..1000)
    assert len(cookies) > 0
    prev = -1
    for cookie in cookies:
        log.info('Check cookie %s' % cookie)

        assert int(cookie) >= 0
        assert int(cookie) < 1000
        assert int(cookie) > prev
        prev = int(cookie)
    sync_repl.join()
    log.info('test_sync_repl_map: PASS\n')

def test_sync_repl_cookie(topology, init_sync_repl_plugins, request):
    """Test sync_repl cookie are progressing is an increasing order
       when there are nested updates

    :id: d7fbde25-5702-46ac-b38e-169d7a68e97c
    :setup: Standalone Instance
    :steps:
      1.: initialization/cleanup done by init_sync_repl_plugins fixture
      2.: create a thread dedicated to run a sync repl client
      3.: Create (9) users that will generate nested updates (automember/memberof)
      4.: stop sync repl client and collect the list of cookie.change_no
      5.: check that cookies.change_no are in increasing order
    :expectedresults:
      1.: succeeds
      2.: succeeds
      3.: succeeds
      4.: succeeds
      5.: succeeds
    """
    inst = topology[0]

    # create a sync repl client and wait 5 seconds to be sure it is running
    sync_repl = Sync_persist(inst)
    sync_repl.start()
    time.sleep(5)

    # create users, that automember/memberof will generate nested updates
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    users_set = []
    for i in range(10001, 10010):
        users_set.append(users.create_test_user(uid=i))

    # stop the server to get the sync_repl result set (exit from while loop).
    # Only way I found to acheive that.
    # and wait a bit to let sync_repl thread time to set its result before fetching it.
    inst.stop()
    time.sleep(10)
    cookies = sync_repl.get_result()

    # checking that the cookie are in increasing and in an acceptable range (0..1000)
    assert len(cookies) > 0
    prev = -1
    for cookie in cookies:
        log.info('Check cookie %s' % cookie)

        assert int(cookie) >= 0
        assert int(cookie) < 1000
        assert int(cookie) > prev
        prev = int(cookie)
    sync_repl.join()
    log.info('test_sync_repl_cookie: PASS\n')

    def fin():
        inst.restart()
        for user in users_set:
            try:
                user.delete()
            except:
                pass

    request.addfinalizer(fin)

    return

def test_sync_repl_cookie_add_del(topology, init_sync_repl_plugins, request):
    """Test sync_repl cookie are progressing is an increasing order
       when there add and del

    :id: 83e11038-6ed0-4a5b-ac77-e44887ab11e3
    :setup: Standalone Instance
    :steps:
      1.: initialization/cleanup done by init_sync_repl_plugins fixture
      2.: create a thread dedicated to run a sync repl client
      3.: Create (3) users that will generate nested updates (automember/memberof)
      4.: Delete (3) users
      5.: stop sync repl client and collect the list of cookie.change_no
      6.: check that cookies.change_no are in increasing order
    :expectedresults:
      1.: succeeds
      2.: succeeds
      3.: succeeds
      4.: succeeds
      5.: succeeds
      6.: succeeds
    """
    inst = topology[0]

    # create a sync repl client and wait 5 seconds to be sure it is running
    sync_repl = Sync_persist(inst)
    sync_repl.start()
    time.sleep(5)

    # create users, that automember/memberof will generate nested updates
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    users_set = []
    for i in range(10001, 10004):
        users_set.append(users.create_test_user(uid=i))

    time.sleep(10)
    # delete users, that automember/memberof will generate nested updates
    for user in users_set:
        user.delete()
    # stop the server to get the sync_repl result set (exit from while loop).
    # Only way I found to acheive that.
    # and wait a bit to let sync_repl thread time to set its result before fetching it.
    inst.stop()
    cookies = sync_repl.get_result()

    # checking that the cookie are in increasing and in an acceptable range (0..1000)
    assert len(cookies) > 0
    prev = -1
    for cookie in cookies:
        log.info('Check cookie %s' % cookie)

        assert int(cookie) >= 0
        assert int(cookie) < 1000
        assert int(cookie) > prev
        prev = int(cookie)
    sync_repl.join()
    log.info('test_sync_repl_cookie_add_del: PASS\n')

    def fin():
        pass

    request.addfinalizer(fin)

    return

def test_sync_repl_cookie_with_failure(topology, init_sync_repl_plugins, request):
    """Test sync_repl cookie are progressing is the right order
       when there is a failure in nested updates

    :id: e0103448-170e-4080-8f22-c34606447ce2
    :setup: Standalone Instance
    :steps:
      1.: initialization/cleanup done by init_sync_repl_plugins fixture
      2.: update group2 so that it will not accept 'member' attribute (set by memberof)
      3.: create a thread dedicated to run a sync repl client
      4.: Create a group that will be the only update received by sync repl client
      5.: Create (9) users that will generate nested updates (automember/memberof).
          creation will fail because 'member' attribute is not allowed in group2
      6.: stop sync repl client and collect the list of cookie.change_no
      7.: check that the list of cookie.change_no contains only the group 'step 11'
    :expectedresults:
      1.: succeeds
      2.: succeeds
      3.: succeeds
      4.: succeeds
      5.: Fails (expected)
      6.: succeeds
      7.: succeeds
    """
    inst = topology[0]

    # Set group2 as a groupOfUniqueNames so that automember will fail to update that group
    # This will trigger a failure in internal MOD and a failure to add member
    group2 = Groups(inst, DEFAULT_SUFFIX).get('group2')
    group2.replace('objectclass', 'groupOfUniqueNames')


    # create a sync repl client and wait 5 seconds to be sure it is running
    sync_repl = Sync_persist(inst)
    sync_repl.start()
    time.sleep(5)

    # Add a test group just to check that sync_repl receives that SyncControlInfo cookie
    groups = Groups(inst, DEFAULT_SUFFIX)
    testgroup = groups.create(properties={'cn': 'group%d' % 10})

    # create users, that automember/memberof will generate nested updates
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    users_set = []
    for i in range(1000,1010):
        try:
            users_set.append(users.create_test_user(uid=i))
            # Automember should fail to add uid=1000 in group2
            assert(False)
        except ldap.UNWILLING_TO_PERFORM:
            pass

    # stop the server to get the sync_repl result set (exit from while loop).
    # Only way I found to acheive that.
    # and wait a bit to let sync_repl thread time to set its result before fetching it.
    inst.stop()
    time.sleep(10)
    cookies = sync_repl.get_result()

    # checking that the cookie list contains only two entries
    # the one from the SyncInfo/RefreshDelete that indicates the end of the refresh
    # the the one from SyncStateControl related to the only updated entry (group10)
    assert len(cookies) == 2
    prev = -1
    for cookie in cookies:
        log.info('Check cookie %s' % cookie)

        assert int(cookie) >= 0
        assert int(cookie) < 1000
        assert int(cookie) > prev
        prev = int(cookie)
    sync_repl.join()
    log.info('test_sync_repl_cookie_with_failure: PASS\n')

    def fin():
        inst.restart()
        for user in users_set:
            try:
                user.delete()
            except:
                pass
        testgroup.delete()

    request.addfinalizer(fin)
