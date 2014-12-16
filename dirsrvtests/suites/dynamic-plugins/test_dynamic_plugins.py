'''
Created on Dec 09, 2014

@author: mreynolds
'''
import os
import sys
import time
import ldap
import ldap.sasl
import logging
import socket
import pytest
import plugin_tests
import stress_tests
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from constants import *

log = logging.getLogger(__name__)

installation_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
        At the beginning, It may exists a standalone instance.
        It may also exists a backup for the standalone instance.

        Principle:
            If standalone instance exists:
                restart it
            If backup of standalone exists:
                create/rebind to standalone

                restore standalone instance from backup
            else:
                Cleanup everything
                    remove instance
                    remove backup
                Create instance
                Create backup
    '''
    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix

    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the backups
    backup_standalone = standalone.checkBackupFS()

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()
    if instance_standalone:
        # assuming the instance is already stopped, just wait 5 sec max
        standalone.stop(timeout=5)
        standalone.start(timeout=10)

    if backup_standalone:
        # The backup exist, assuming it is correct
        # we just re-init the instance with it
        if not instance_standalone:
            standalone.create()
            # Used to retrieve configuration information (dbdir, confdir...)
            standalone.open()

        # restore standalone instance from backup
        standalone.stop(timeout=10)
        standalone.restoreFS(backup_standalone)
        standalone.start(timeout=10)

    else:
        # We should be here only in two conditions
        #      - This is the first time a test involve standalone instance
        #      - Something weird happened (instance/backup destroyed)
        #        so we discard everything and recreate all

        # Remove the backup. So even if we have a specific backup file
        # (e.g backup_standalone) we clear backup that an instance may have created
        if backup_standalone:
            standalone.clearBackupFS()

        # Remove the instance
        if instance_standalone:
            standalone.delete()

        # Create the instance
        standalone.create()

        # Used to retrieve configuration information (dbdir, confdir...)
        standalone.open()

        # Time to create the backups
        standalone.stop(timeout=10)
        standalone.backupfile = standalone.backupFS()
        standalone.start(timeout=10)

    #
    # Here we have standalone instance up and running
    # Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyStandalone(standalone)


def test_dynamic_plugins(topology):
    """
        Test Dynamic Plugins - exercise each plugin and its main features, while
        changing the configuration without restarting the server.

        Need to test: functionality, stability, and stress.

        Functionality - Make sure that as configuration changes are made they take
                        effect immediately.  Cross plugin interaction (e.g. automember/memberOf)
                        needs to tested, as well as plugin tasks.  Need to test plugin
                        config validation(dependencies, etc).

        Memory Corruption - Restart the plugins many times, and in different orders and test
                            functionality, and stability.  This will excerise the internal
                            plugin linked lists, dse callabcks, and task handlers.

        Stress - Put the server under some type of load that is using a particular
                 plugin for each operation, and then make changes to that plugin.
                 The new changes should take effect, and the server should not crash.

    """

    ############################################################################
    #  Test plugin functionality
    ############################################################################

    # First enable dynamic plugins
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError, e:
        ldap.error('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False

    log.info('#####################################################')
    log.info('Testing Dynamic Plugins Functionality...')
    log.info('#####################################################\n')

    plugin_tests.test_all_plugins(topology.standalone)

    log.info('#####################################################')
    log.info('Successfully Tested Dynamic Plugins Functionality.')
    log.info('#####################################################\n')

    ############################################################################
    # Test the stability by exercising the internal lists, callabcks, and task handlers
    ############################################################################

    log.info('#####################################################')
    log.info('Testing Dynamic Plugins for Memory Corruption...')
    log.info('#####################################################\n')
    prev_plugin_test = None
    prev_prev_plugin_test = None
    for plugin_test in plugin_tests.func_tests:
        #
        # Restart the plugin several times (and prev plugins) - work that linked list
        #
        plugin_test(topology.standalone, "restart")

        if prev_prev_plugin_test:
            prev_prev_plugin_test(topology.standalone, "restart")

        plugin_test(topology.standalone, "restart")

        if prev_plugin_test:
            prev_plugin_test(topology.standalone, "restart")

        plugin_test(topology.standalone, "restart")

        # Now run the functional test
        plugin_test(topology.standalone)

        # Set the previous tests
        if prev_plugin_test:
            prev_prev_plugin_test = prev_plugin_test
        prev_plugin_test = plugin_test

    log.info('#####################################################')
    log.info('Successfully Tested Dynamic Plugins for Memory Corruption.')
    log.info('#####################################################\n')

    ############################################################################
    # Stress two plugins while restarting it, and while restarting other plugins.
    # The goal is to not crash, and have the plugins work after stressing it.
    ############################################################################

    log.info('#####################################################')
    log.info('Stressing Dynamic Plugins...')
    log.info('#####################################################\n')

    # Configure the plugins
    stress_tests.configureMO(topology.standalone)
    stress_tests.configureRI(topology.standalone)

    # Launch three new threads to add a bunch of users
    add_users = stress_tests.AddUsers(topology.standalone, 'user', True)
    add_users.start()
    add_users2 = stress_tests.AddUsers(topology.standalone, 'entry', True)
    add_users2.start()
    add_users3 = stress_tests.AddUsers(topology.standalone, 'person', True)
    add_users3.start()
    time.sleep(1)

    # While we are adding users restart the MO plugin
    topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    time.sleep(3)
    topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
    time.sleep(1)
    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)

    # Restart idle plugin
    topology.standalone.plugins.disable(name=PLUGIN_LINKED_ATTRS)
    topology.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)

    # Wait for the 'adding' threads to complete
    add_users.join()
    add_users2.join()
    add_users3.join()

    # Now launch three threads to delete the users, and restart both the MO and RI plugins
    del_users = stress_tests.DelUsers(topology.standalone, 'user')
    del_users.start()
    del_users2 = stress_tests.DelUsers(topology.standalone, 'entry')
    del_users2.start()
    del_users3 = stress_tests.DelUsers(topology.standalone, 'person')
    del_users3.start()
    time.sleep(1)

    # Restart the both the MO and RI plugins during these deletes

    topology.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
    topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
    time.sleep(3)
    topology.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
    time.sleep(1)
    topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
    time.sleep(1)
    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    time.sleep(1)
    topology.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)

    # Restart idle plugin
    topology.standalone.plugins.disable(name=PLUGIN_LINKED_ATTRS)
    topology.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)

    # Wait for the 'deleting' threads to complete
    del_users.join()
    del_users2.join()
    del_users3.join()

    # Now make sure both the MO and RI plugins still work
    plugin_tests.func_tests[8](topology.standalone)  # RI plugin
    plugin_tests.func_tests[5](topology.standalone)  # MO plugin

    log.info('#####################################################')
    log.info('Successfully Stressed Dynamic Plugins.')
    log.info('#####################################################\n')

    ############################################################################
    # We made it to the end!
    ############################################################################

    log.info('#####################################################')
    log.info('#####################################################')
    log.info("Dynamic Plugins Testsuite: Completed Successfully!")
    log.info('#####################################################')
    log.info('#####################################################')

def test_dynamic_plugins_final(topology):
    topology.standalone.stop(timeout=10)


def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix = None

    topo = topology(True)
    test_dynamic_plugins(topo)

if __name__ == '__main__':
    run_isolated()
