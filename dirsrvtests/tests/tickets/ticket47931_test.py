# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import threading
import time
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, PLUGIN_RETRO_CHANGELOG, PLUGIN_MEMBER_OF, BACKEND_NAME

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.3'), reason="Not implemented")]

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None
SECOND_SUFFIX = "dc=deadlock"
SECOND_BACKEND = "deadlock"
RETROCL_PLUGIN_DN = ('cn=' + PLUGIN_RETRO_CHANGELOG + ',cn=plugins,cn=config')
MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
GROUP_DN = ("cn=group," + DEFAULT_SUFFIX)
MEMBER_DN_COMP = "uid=member"
TIME_OUT = 5


class modifySecondBackendThread(threading.Thread):
    def __init__(self, inst, timeout):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.timeout = timeout

    def run(self):
        conn = self.inst.clone()
        conn.set_option(ldap.OPT_TIMEOUT, self.timeout)
        log.info('Modify second suffix...')
        for x in range(0, 5000):
            try:
                conn.modify_s(SECOND_SUFFIX,
                              [(ldap.MOD_REPLACE,
                                'description',
                                b'new description')])
            except ldap.LDAPError as e:
                log.fatal('Failed to modify second suffix - error: %s' %
                          (e.args[0]['desc']))
                assert False

        conn.close()
        log.info('Finished modifying second suffix')


def test_ticket47931(topology_st):
    """Test Retro Changelog and MemberOf deadlock fix.
       Verification steps:
           - Enable retro cl and memberOf.
           - Create two backends: A & B.
           - Configure retrocl scoping for backend A.
           - Configure memberOf plugin for uniquemember
           - Create group in backend A.
           - In parallel, add members to the group on A, and make modifications
             to entries in backend B.
           - Make sure the server does not hang during the updates to both
             backends.

    """

    # Enable dynamic plugins to make plugin configuration easier
    try:
        topology_st.standalone.modify_s(DN_CONFIG,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-dynamic-plugins',
                                          b'on')])
    except ldap.LDAPError as e:
        log.error('Failed to enable dynamic plugins! ' + e.args[0]['desc'])
        assert False

    # Enable the plugins
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology_st.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)

    # Create second backend
    topology_st.standalone.backend.create(SECOND_SUFFIX, {BACKEND_NAME: SECOND_BACKEND})
    topology_st.standalone.mappingtree.create(SECOND_SUFFIX, bename=SECOND_BACKEND)

    # Create the root node of the second backend
    try:
        topology_st.standalone.add_s(Entry((SECOND_SUFFIX,
                                            {'objectclass': 'top domain'.split(),
                                             'dc': 'deadlock'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to create suffix entry: error ' + e.args[0]['desc'])
        assert False

    # Configure retrocl scope
    try:
        topology_st.standalone.modify_s(RETROCL_PLUGIN_DN,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-include-suffix',
                                          ensure_bytes(DEFAULT_SUFFIX))])
    except ldap.LDAPError as e:
        log.error('Failed to configure retrocl plugin: ' + e.args[0]['desc'])
        assert False

    # Configure memberOf group attribute
    try:
        topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                        [(ldap.MOD_REPLACE,
                                          'memberofgroupattr',
                                          b'uniquemember')])
    except ldap.LDAPError as e:
        log.fatal('Failed to configure memberOf plugin: error ' + e.args[0]['desc'])
        assert False
    time.sleep(1)

    # Create group
    try:
        topology_st.standalone.add_s(Entry((GROUP_DN,
                                            {'objectclass': 'top extensibleObject'.split(),
                                             'cn': 'group'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add grouo: error ' + e.args[0]['desc'])
        assert False

    # Create 1500 entries (future members of the group)
    for idx in range(1, 1500):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology_st.standalone.add_s(Entry((USER_DN,
                                                {'objectclass': 'top extensibleObject'.split(),
                                                 'uid': 'member%d' % (idx)})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add user (%s): error %s' % (USER_DN, e.args[0]['desc']))
            assert False

    # Modify second backend (separate thread)
    mod_backend_thrd = modifySecondBackendThread(topology_st.standalone, TIME_OUT)
    mod_backend_thrd.start()
    time.sleep(1)

    # Add members to the group - set timeout
    log.info('Adding members to the group...')
    topology_st.standalone.set_option(ldap.OPT_TIMEOUT, TIME_OUT)
    for idx in range(1, 1500):
        try:
            MEMBER_VAL = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology_st.standalone.modify_s(GROUP_DN,
                                            [(ldap.MOD_ADD,
                                              'uniquemember',
                                              ensure_bytes(MEMBER_VAL))])
        except ldap.TIMEOUT:
            log.fatal('Deadlock!  Bug verification failed.')
            assert False
        except ldap.LDAPError as e:
            log.fatal('Failed to update group(not a deadlock) member (%s) - error: %s' %
                      (MEMBER_VAL, e.args[0]['desc']))
            assert False
    log.info('Finished adding members to the group.')

    # Wait for the thread to finish
    mod_backend_thrd.join()

    # No timeout, test passed!
    log.info('Test complete\n')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
