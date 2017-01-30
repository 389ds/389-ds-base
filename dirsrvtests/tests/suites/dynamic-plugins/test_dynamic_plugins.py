# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
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
import logging

import ldap.sasl
import pytest
from lib389.tasks import *

import plugin_tests
import stress_tests
from lib389.topologies import topology_st

log = logging.getLogger(__name__)


def repl_fail(replica):
    """Remove replica instance, and assert failure"""

    replica.delete()
    assert False


def test_dynamic_plugins(topology_st):
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
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError as e:
        log.fatal('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False

    # Test that critical plugins can be updated even though the change might not be applied
    try:
        topology_st.standalone.modify_s(DN_LDBM, [(ldap.MOD_REPLACE, 'description', 'test')])
    except ldap.LDAPError as e:
        log.fatal('Failed to apply change to critical plugin' + e.message['desc'])
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

        plugin_tests.test_all_plugins(topology_st.standalone)

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
            plugin_test(topology_st.standalone, "restart")

            if prev_prev_plugin_test:
                prev_prev_plugin_test(topology_st.standalone, "restart")

            plugin_test(topology_st.standalone, "restart")

            if prev_plugin_test:
                prev_plugin_test(topology_st.standalone, "restart")

            plugin_test(topology_st.standalone, "restart")

            # Now run the functional test
            plugin_test(topology_st.standalone)

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

        stress_tests.configureMO(topology_st.standalone)
        stress_tests.configureRI(topology_st.standalone)

        stress_count = 0
        while stress_count < stress_max_runs:
            log.info('####################################################################')
            log.info('Running stress test' + msg + '.  Run (%d/%d)...' % (stress_count + 1, stress_max_runs))
            log.info('####################################################################\n')

            try:
                # Launch three new threads to add a bunch of users
                add_users = stress_tests.AddUsers(topology_st.standalone, 'employee', True)
                add_users.start()
                add_users2 = stress_tests.AddUsers(topology_st.standalone, 'entry', True)
                add_users2.start()
                add_users3 = stress_tests.AddUsers(topology_st.standalone, 'person', True)
                add_users3.start()
                time.sleep(1)

                # While we are adding users restart the MO plugin and an idle plugin
                topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                topology_st.standalone.plugins.disable(name=PLUGIN_LINKED_ATTRS)
                topology_st.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)
                time.sleep(1)
                topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                time.sleep(2)
                topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                topology_st.standalone.plugins.disable(name=PLUGIN_LINKED_ATTRS)
                topology_st.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)
                topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)

                # Wait for the 'adding' threads to complete
                add_users.join()
                add_users2.join()
                add_users3.join()

                # Now launch three threads to delete the users
                del_users = stress_tests.DelUsers(topology_st.standalone, 'employee')
                del_users.start()
                del_users2 = stress_tests.DelUsers(topology_st.standalone, 'entry')
                del_users2.start()
                del_users3 = stress_tests.DelUsers(topology_st.standalone, 'person')
                del_users3.start()
                time.sleep(1)

                # Restart both the MO, RI plugins during these deletes, and an idle plugin
                topology_st.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
                topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                topology_st.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
                time.sleep(1)
                topology_st.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
                time.sleep(1)
                topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology_st.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
                topology_st.standalone.plugins.disable(name=PLUGIN_LINKED_ATTRS)
                topology_st.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)
                topology_st.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
                topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                topology_st.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
                time.sleep(2)
                topology_st.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
                time.sleep(1)
                topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
                time.sleep(1)
                topology_st.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
                topology_st.standalone.plugins.disable(name=PLUGIN_LINKED_ATTRS)
                topology_st.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)

                # Wait for the 'deleting' threads to complete
                del_users.join()
                del_users2.join()
                del_users3.join()

                # Now make sure both the MO and RI plugins still work correctly
                plugin_tests.func_tests[8](topology_st.standalone)  # RI plugin
                plugin_tests.func_tests[5](topology_st.standalone)  # MO plugin

                # Cleanup the stress tests
                stress_tests.cleanup(topology_st.standalone)

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
                topology_st.standalone.replica.enableReplication(suffix=DEFAULT_SUFFIX,
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

                repl_agreement = topology_st.standalone.agreement.create(suffix=DEFAULT_SUFFIX,
                                                                         host=LOCALHOST,
                                                                         port=REPLICA_PORT,
                                                                         properties=properties)

                if not repl_agreement:
                    log.fatal("Fail to create a replica agreement")
                    repl_fail(replica_inst)

                topology_st.standalone.agreement.init(DEFAULT_SUFFIX, LOCALHOST, REPLICA_PORT)
                topology_st.standalone.waitForReplInit(repl_agreement)
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
        entry = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, RUV_FILTER)
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
    while count < 60:
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
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX,
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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
