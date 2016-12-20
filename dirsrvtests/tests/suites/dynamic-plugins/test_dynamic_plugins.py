# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
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
import pytest
import plugin_tests
import stress_tests
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *

log = logging.getLogger(__name__)

installation_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


def repl_fail(replica):
    # remove replica instance, and assert failure
    replica.delete()
    assert False


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
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

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def test_dynamic_plugins(topology):
    """
        Test Dynamic Plugins - exercise each plugin and its main features, while
        changing the configuration without restarting the server.

        Need to test: functionality, stability, and stress.  These tests need to run
                      with replication disabled, and with replication setup with a
                      second instance.  Then test if replication is working, and we have
                      same entries on each side.

        Functionality - Make sure that as configuration changes are made they take
                        effect immediately.  Cross plugin interaction (e.g. automember/memberOf)
                        needs to tested, as well as plugin tasks.  Need to test plugin
                        config validation(dependencies, etc).

        Memory Corruption - Restart the plugins many times, and in different orders and test
                            functionality, and stability.  This will excerise the internal
                            plugin linked lists, dse callbacks, and task handlers.

        Stress - Put the server under load that will trigger multiple plugins(MO, RI, DNA, etc)
                 Restart various plugins while these operations are going on.  Perform this test
                 5 times(stress_max_run).

    """

    REPLICA_PORT = 33334
    RUV_FILTER = '(&(nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff)(objectclass=nstombstone))'
    master_maxcsn = 0
    replica_maxcsn = 0
    msg = ' (no replication)'
    replication_run = False
    stress_max_runs = 5

    # First enable dynamic plugins
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError as e:
        ldap.fatal('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False

    # Test that critical plugins can be updated even though the change might not be applied
    try:
        topology.standalone.modify_s(DN_LDBM, [(ldap.MOD_REPLACE, 'description', 'test')])
    except ldap.LDAPError as e:
        ldap.fatal('Failed to apply change to critical plugin' + e.message['desc'])
        assert False

    while 1:
        #
        # First run the tests with replication disabled, then rerun them with replication set up
        #

        ############################################################################
        #  Test plugin functionality
        ############################################################################

        log.info('####################################################################')
        log.info('Testing Dynamic Plugins Functionality' + msg + '...')
        log.info('####################################################################\n')

        plugin_tests.test_all_plugins(topology.standalone)

        log.info('####################################################################')
        log.info('Successfully Tested Dynamic Plugins Functionality' + msg + '.')
        log.info('####################################################################\n')

        ############################################################################
        # Test the stability by exercising the internal lists, callabcks, and task handlers
        ############################################################################

        log.info('####################################################################')
        log.info('Testing Dynamic Plugins for Memory Corruption' + msg + '...')
        log.info('####################################################################\n')
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

        log.info('####################################################################')
        log.info('Successfully Tested Dynamic Plugins for Memory Corruption' + msg + '.')
        log.info('####################################################################\n')

        ############################################################################
        # Stress two plugins while restarting it, and while restarting other plugins.
        # The goal is to not crash, and have the plugins work after stressing them.
        ############################################################################

        log.info('####################################################################')
        log.info('Stressing Dynamic Plugins' + msg + '...')
        log.info('####################################################################\n')

        stress_tests.configureMO(topology.standalone)
        stress_tests.configureRI(topology.standalone)

        stress_count = 0
        while stress_count < stress_max_runs:
            log.info('####################################################################')
            log.info('Running stress test' + msg + '.  Run (%d/%d)...' % (stress_count + 1, stress_max_runs))
            log.info('####################################################################\n')

            try:
                # Launch three new threads to add a bunch of users
                add_users = stress_tests.AddUsers(topology.standalone, 'employee', True)
                add_users.start()
                add_users2 = stress_tests.AddUsers(topology.standalone, 'entry', True)
                add_users2.start()
                add_users3 = stress_tests.AddUsers(topology.standalone, 'person', True)
                add_users3.start()
                time.sleep(1)

                # While we are adding users restart the MO plugin and an idle plugin
                topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                topology.standalone.plugins.disable(name=PLUGIN_LINKED_ATTRS)
                topology.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)
                time.sleep(1)
                topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                time.sleep(2)
                topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                topology.standalone.plugins.disable(name=PLUGIN_LINKED_ATTRS)
                topology.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)
                topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)

                # Wait for the 'adding' threads to complete
                add_users.join()
                add_users2.join()
                add_users3.join()

                # Now launch three threads to delete the users
                del_users = stress_tests.DelUsers(topology.standalone, 'employee')
                del_users.start()
                del_users2 = stress_tests.DelUsers(topology.standalone, 'entry')
                del_users2.start()
                del_users3 = stress_tests.DelUsers(topology.standalone, 'person')
                del_users3.start()
                time.sleep(1)

                # Restart both the MO, RI plugins during these deletes, and an idle plugin
                topology.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
                topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                topology.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
                time.sleep(1)
                topology.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
                time.sleep(1)
                topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
                topology.standalone.plugins.disable(name=PLUGIN_LINKED_ATTRS)
                topology.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)
                topology.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
                topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                topology.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
                time.sleep(2)
                topology.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
                time.sleep(1)
                topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
                topology.standalone.plugins.disable(name=PLUGIN_LINKED_ATTRS)
                topology.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)

                # Wait for the 'deleting' threads to complete
                del_users.join()
                del_users2.join()
                del_users3.join()

                # Now make sure both the MO and RI plugins still work correctly
                plugin_tests.func_tests[8](topology.standalone)  # RI plugin
                plugin_tests.func_tests[5](topology.standalone)  # MO plugin

                # Cleanup the stress tests
                stress_tests.cleanup(topology.standalone)

            except:
                log.info('Stress test failed!')
                if replication_run:
                    repl_fail(replica_inst)

            stress_count += 1
            log.info('####################################################################')
            log.info('Successfully Stressed Dynamic Plugins' + msg +
                     '.  Completed (%d/%d)' % (stress_count, stress_max_runs))
            log.info('####################################################################\n')

        if replication_run:
            # We're done.
            break
        else:
            #
            # Enable replication and run everything one more time
            #
            log.info('Setting up replication, and rerunning the tests...\n')

            # Create replica instance
            replica_inst = DirSrv(verbose=False)
            args_instance[SER_HOST] = LOCALHOST
            args_instance[SER_PORT] = REPLICA_PORT
            args_instance[SER_SERVERID_PROP] = 'replica'
            args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX

            args_replica_inst = args_instance.copy()
            replica_inst.allocate(args_replica_inst)
            replica_inst.create()
            replica_inst.open()

            try:
                topology.standalone.replica.enableReplication(suffix=DEFAULT_SUFFIX,
                                                              role=REPLICAROLE_MASTER,
                                                              replicaId=1)
                replica_inst.replica.enableReplication(suffix=DEFAULT_SUFFIX,
                                                              role=REPLICAROLE_CONSUMER,
                                                              replicaId=65535)
                properties = {RA_NAME: r'to_replica',
                              RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                              RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                              RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                              RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}

                repl_agreement = topology.standalone.agreement.create(suffix=DEFAULT_SUFFIX,
                                                                      host=LOCALHOST,
                                                                      port=REPLICA_PORT,
                                                                      properties=properties)

                if not repl_agreement:
                    log.fatal("Fail to create a replica agreement")
                    repl_fail(replica_inst)

                topology.standalone.agreement.init(DEFAULT_SUFFIX, LOCALHOST, REPLICA_PORT)
                topology.standalone.waitForReplInit(repl_agreement)
            except:
                log.info('Failed to setup replication!')
                repl_fail(replica_inst)

            replication_run = True
            msg = ' (replication enabled)'
            time.sleep(1)

    ############################################################################
    # Check replication, and data are in sync, and remove the instance
    ############################################################################

    log.info('Checking if replication is in sync...')

    try:
        # Grab master's max CSN
        entry = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, RUV_FILTER)
        if not entry:
            log.error('Failed to find db tombstone entry from master')
            repl_fail(replica_inst)
        elements = entry[0].getValues('nsds50ruv')
        for ruv in elements:
            if 'replica 1' in ruv:
                parts = ruv.split()
                if len(parts) == 5:
                    master_maxcsn = parts[4]
                    break
                else:
                    log.error('RUV is incomplete')
                    repl_fail(replica_inst)
        if master_maxcsn == 0:
            log.error('Failed to find maxcsn on master')
            repl_fail(replica_inst)

    except ldap.LDAPError as e:
        log.fatal('Unable to search masterfor db tombstone: ' + e.message['desc'])
        repl_fail(replica_inst)

    # Loop on the consumer - waiting for it to catch up
    count = 0
    insync = False
    while count < 10:
        try:
            # Grab master's max CSN
            entry = replica_inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, RUV_FILTER)
            if not entry:
                log.error('Failed to find db tombstone entry on consumer')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 1' in ruv:
                    parts = ruv.split()
                    if len(parts) == 5:
                        replica_maxcsn = parts[4]
                        break
            if replica_maxcsn == 0:
                log.error('Failed to find maxcsn on consumer')
                repl_fail(replica_inst)
        except ldap.LDAPError as e:
            log.fatal('Unable to search for db tombstone on consumer: ' + e.message['desc'])
            repl_fail(replica_inst)

        if master_maxcsn == replica_maxcsn:
            insync = True
            log.info('Replication is in sync.\n')
            break
        count += 1
        time.sleep(1)

    # Report on replication status
    if not insync:
        log.error('Consumer not in sync with master!')
        repl_fail(replica_inst)

    #
    # Verify the databases are identical. There should not be any "user, entry, employee" entries
    #
    log.info('Checking if the data is the same between the replicas...')

    # Check the master
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX,
                                        ldap.SCOPE_SUBTREE,
                                        "(|(uid=person*)(uid=entry*)(uid=employee*))")
        if len(entries) > 0:
            log.error('Master database has incorrect data set!\n')
            repl_fail(replica_inst)
    except ldap.LDAPError as e:
        log.fatal('Unable to search db on master: ' + e.message['desc'])
        repl_fail(replica_inst)

    # Check the consumer
    try:
        entries = replica_inst.search_s(DEFAULT_SUFFIX,
                                        ldap.SCOPE_SUBTREE,
                                        "(|(uid=person*)(uid=entry*)(uid=employee*))")
        if len(entries) > 0:
            log.error('Consumer database in not consistent with master database')
            repl_fail(replica_inst)
    except ldap.LDAPError as e:
        log.fatal('Unable to search db on consumer: ' + e.message['desc'])
        repl_fail(replica_inst)

    log.info('Data is consistent across the replicas.\n')

    log.info('####################################################################')
    log.info('Replication consistency test passed')
    log.info('####################################################################\n')

    # Remove the replica instance
    replica_inst.delete()

    ############################################################################
    # We made it to the end!
    ############################################################################

    log.info('#####################################################')
    log.info('#####################################################')
    log.info("Dynamic Plugins Testsuite: Completed Successfully!")
    log.info('#####################################################')
    log.info('#####################################################\n')


def test_dynamic_plugins_final(topology):
    topology.standalone.delete()


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
    test_dynamic_plugins_final(topo)


if __name__ == '__main__':
    run_isolated()
