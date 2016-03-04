import os
import sys
import time
import ldap
import logging
import pytest
import threading
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

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


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


class modifySecondBackendThread(threading.Thread):
    def __init__(self, inst, timeout):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.timeout = timeout

    def run(self):
        conn = self.inst.openConnection()
        conn.set_option(ldap.OPT_TIMEOUT, self.timeout)
        log.info('Modify second suffix...')
        for x in range(0, 5000):
            try:
                conn.modify_s(SECOND_SUFFIX,
                              [(ldap.MOD_REPLACE,
                                'description',
                                'new description')])
            except ldap.LDAPError as e:
                log.fatal('Failed to modify second suffix - error: %s' %
                          (e.message['desc']))
                assert False

        conn.close()
        log.info('Finished modifying second suffix')


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Delete each instance in the end
    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_ticket47931(topology):
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
        topology.standalone.modify_s(DN_CONFIG,
                                     [(ldap.MOD_REPLACE,
                                       'nsslapd-dynamic-plugins',
                                       'on')])
    except ldap.LDAPError as e:
        ldap.error('Failed to enable dynamic plugins! ' + e.message['desc'])
        assert False

    # Enable the plugins
    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)

    # Create second backend
    topology.standalone.backend.create(SECOND_SUFFIX, {BACKEND_NAME: SECOND_BACKEND})
    topology.standalone.mappingtree.create(SECOND_SUFFIX, bename=SECOND_BACKEND)

    # Create the root node of the second backend
    try:
        topology.standalone.add_s(Entry((SECOND_SUFFIX,
                                  {'objectclass': 'top domain'.split(),
                                   'dc': 'deadlock'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to create suffix entry: error ' + e.message['desc'])
        assert False

    # Configure retrocl scope
    try:
        topology.standalone.modify_s(RETROCL_PLUGIN_DN,
                                     [(ldap.MOD_REPLACE,
                                       'nsslapd-include-suffix',
                                       DEFAULT_SUFFIX)])
    except ldap.LDAPError as e:
        ldap.error('Failed to configure retrocl plugin: ' + e.message['desc'])
        assert False

    # Configure memberOf group attribute
    try:
        topology.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                     [(ldap.MOD_REPLACE,
                                       'memberofgroupattr',
                                       'uniquemember')])
    except ldap.LDAPError as e:
        log.fatal('Failed to configure memberOf plugin: error ' + e.message['desc'])
        assert False

    # Create group
    try:
        topology.standalone.add_s(Entry((GROUP_DN,
                                         {'objectclass': 'top extensibleObject'.split(),
                                          'cn': 'group'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add grouo: error ' + e.message['desc'])
        assert False

    # Create 1500 entries (future members of the group)
    for idx in range(1, 1500):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology.standalone.add_s(Entry((USER_DN,
                                             {'objectclass': 'top extensibleObject'.split(),
                                              'uid': 'member%d' % (x)})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add user (%s): error %s' % (USER_DN, e.message['desc']))
            assert False

    # Modify second backend (separate thread)
    mod_backend_thrd = modifySecondBackendThread(topology.standalone, TIME_OUT)
    mod_backend_thrd.start()

    # Add members to the group - set timeout
    log.info('Adding members to the group...')
    topology.standalone.set_option(ldap.OPT_TIMEOUT, TIME_OUT)
    for idx in range(1, 1500):
        try:
            MEMBER_VAL = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology.standalone.modify_s(GROUP_DN,
                                         [(ldap.MOD_ADD,
                                           'uniquemember',
                                           MEMBER_VAL)])
        except ldap.TIMEOUT:
            log.fatal('Deadlock!  Bug verification failed.')
            assert False
        except ldap.LDAPError as e:
            log.fatal('Failed to update group(not a deadlock) member (%s) - error: %s' %
                      (MEMBER_VAL, e.message['desc']))
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