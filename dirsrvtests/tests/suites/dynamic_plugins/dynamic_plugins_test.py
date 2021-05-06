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
from lib389.replica import ReplicationManager
from lib389.config import LDBMConfig
from lib389._constants import *
from lib389.topologies import topology_m2
from ..plugins import acceptance_test
from . import stress_tests

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


def check_replicas(topology_m2):
    """Check that replication is in sync and working"""

    m1 = topology_m2.ms["supplier1"]
    m2 = topology_m2.ms["supplier2"]

    log.info('Checking if replication is in sync...')
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication_topology(topology_m2)
    #
    # Verify the databases are identical. There should not be any "user, entry, employee" entries
    #
    log.info('Checking if the data is the same between the replicas...')

    # Check the supplier
    try:
        entries = m1.search_s(DEFAULT_SUFFIX,
                              ldap.SCOPE_SUBTREE,
                              "(|(uid=person*)(uid=entry*)(uid=employee*))")
        if len(entries) > 0:
            log.error('Supplier database has incorrect data set!\n')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search db on supplier: ' + e.message['desc'])
        assert False

    # Check the consumer
    try:
        entries = m2.search_s(DEFAULT_SUFFIX,
                              ldap.SCOPE_SUBTREE,
                              "(|(uid=person*)(uid=entry*)(uid=employee*))")
        if len(entries) > 0:
            log.error('Consumer database in not consistent with supplier database')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search db on consumer: ' + e.message['desc'])
        assert False

    log.info('Data is consistent across the replicas.\n')

#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_acceptance(topology_m2):
    """Exercise each plugin and its main features, while
    changing the configuration without restarting the server.

    :id: 96136538-0151-4b09-9933-0e0cbf2c786c
    :setup: 2 Supplier Instances
    :steps:
        1. Pause all replication
        2. Set nsslapd-dynamic-plugins to on
        3. Try to update LDBM config entry
        4. Go through all plugin basic functionality
        5. Resume replication
        6. Go through all plugin basic functionality again
        7. Check that data in sync and replication is working
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    m1 = topology_m2.ms["supplier1"]
    msg = ' (no replication)'
    replication_run = False

    # First part of the test should be without replication
    topology_m2.pause_all_replicas()

    # First enable dynamic plugins
    m1.config.replace('nsslapd-dynamic-plugins', 'on')

    # Test that critical plugins can be updated even though the change might not be applied
    ldbm_config = LDBMConfig(m1)
    ldbm_config.replace('description', 'test')

    while True:
        # First run the tests with replication disabled, then rerun them with replication set up

        ############################################################################
        #  Test plugin functionality
        ############################################################################

        log.info('####################################################################')
        log.info('Testing Dynamic Plugins Functionality' + msg + '...')
        log.info('####################################################################\n')

        acceptance_test.check_all_plugins(topology_m2)

        log.info('####################################################################')
        log.info('Successfully Tested Dynamic Plugins Functionality' + msg + '.')
        log.info('####################################################################\n')

        if replication_run:
            # We're done.
            break
        else:
            log.info('Resume replication and run everything one more time')
            topology_m2.resume_all_replicas()

            replication_run = True
            msg = ' (replication enabled)'
            time.sleep(1)

    ############################################################################
    # Check replication, and data are in sync
    ############################################################################
    check_replicas(topology_m2)

#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_memory_corruption(topology_m2):
    """Check the plugins for memory corruption issues while
    dynamic plugins option is enabled

    :id: 96136538-0151-4b09-9933-0e0cbf2c7862
    :setup: 2 Supplier Instances
    :steps:
        1. Pause all replication
        2. Set nsslapd-dynamic-plugins to on
        3. Try to update LDBM config entry
        4. Restart the plugin many times in a linked list fashion
           restarting previous and preprevious plugins in the list of all plugins
        5. Run the functional test
        6. Repeat 4 and 5 steps for all plugins
        7. Resume replication
        8. Go through 4-6 steps once more
        9. Check that data in sync and replication is working
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


    m1 = topology_m2.ms["supplier1"]
    msg = ' (no replication)'
    replication_run = False

    # First part of the test should be without replication
    topology_m2.pause_all_replicas()

    # First enable dynamic plugins
    m1.config.replace('nsslapd-dynamic-plugins', 'on')

    # Test that critical plugins can be updated even though the change might not be applied
    ldbm_config = LDBMConfig(m1)
    ldbm_config.replace('description', 'test')

    while True:
        # First run the tests with replication disabled, then rerun them with replication set up

        ############################################################################
        # Test the stability by exercising the internal lists, callabcks, and task handlers
        ############################################################################

        log.info('####################################################################')
        log.info('Testing Dynamic Plugins for Memory Corruption' + msg + '...')
        log.info('####################################################################\n')
        prev_plugin_test = None
        prev_prev_plugin_test = None

        for plugin_test in acceptance_test.func_tests:
            #
            # Restart the plugin several times (and prev plugins) - work that linked list
            #
            plugin_test(topology_m2, "restart")

            if prev_prev_plugin_test:
                prev_prev_plugin_test(topology_m2, "restart")

            plugin_test(topology_m2, "restart")

            if prev_plugin_test:
                prev_plugin_test(topology_m2, "restart")

            plugin_test(topology_m2, "restart")

            # Now run the functional test
            plugin_test(topology_m2, "dynamic")

            # Set the previous tests
            if prev_plugin_test:
                prev_prev_plugin_test = prev_plugin_test
            prev_plugin_test = plugin_test

        log.info('####################################################################')
        log.info('Successfully Tested Dynamic Plugins for Memory Corruption' + msg + '.')
        log.info('####################################################################\n')

        if replication_run:
            # We're done.
            break
        else:
            log.info('Resume replication and run everything one more time')
            topology_m2.resume_all_replicas()

            replication_run = True
            msg = ' (replication enabled)'
            time.sleep(1)

    ############################################################################
    # Check replication, and data are in sync
    ############################################################################
    check_replicas(topology_m2)

#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
@pytest.mark.tier2
def test_stress(topology_m2):
    """Test plugins while under a big load. Perform the test 5 times

    :id: 96136538-0151-4b09-9933-0e0cbf2c7863
    :setup: 2 Supplier Instances
    :steps:
        1. Pause all replication
        2. Set nsslapd-dynamic-plugins to on
        3. Try to update LDBM config entry
        4. Do one run through all tests
        5. Enable Referential integrity and MemberOf plugins
        6. Launch three new threads to add a bunch of users
        7. While we are adding users restart the MemberOf and
           Linked Attributes plugins many times
        8. Wait for the 'adding' threads to complete
        9. Now launch three threads to delete the users
        10. Restart both the MemberOf, Referential integrity and
            Linked Attributes plugins during these deletes
        11. Wait for the 'deleting' threads to complete
        12. Now make sure both the MemberOf and Referential integrity plugins still work correctly
        13. Cleanup the stress tests (delete the group entry)
        14. Perform 4-13 steps five times
        15. Resume replication
        16. Go through 4-14 steps once more
        17. Check that data in sync and replication is working
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
        10. Success
        11. Success
        12. Success
        13. Success
        14. Success
        15. Success
        16. Success
        17. Success
    """

    m1 = topology_m2.ms["supplier1"]
    msg = ' (no replication)'
    replication_run = False
    stress_max_runs = 5

    # First part of the test should be without replication
    topology_m2.pause_all_replicas()

    # First enable dynamic plugins
    m1.config.replace('nsslapd-dynamic-plugins', 'on')

    # Test that critical plugins can be updated even though the change might not be applied
    ldbm_config = LDBMConfig(m1)
    ldbm_config.replace('description', 'test')

    while True:
        # First run the tests with replication disabled, then rerun them with replication set up

        log.info('Do one run through all tests ' + msg + '...')
        acceptance_test.check_all_plugins(topology_m2)

        log.info('####################################################################')
        log.info('Stressing Dynamic Plugins' + msg + '...')
        log.info('####################################################################\n')

        stress_tests.configureMO(m1)
        stress_tests.configureRI(m1)

        stress_count = 0
        while stress_count < stress_max_runs:
            log.info('####################################################################')
            log.info('Running stress test' + msg + '.  Run (%d/%d)...' % (stress_count + 1, stress_max_runs))
            log.info('####################################################################\n')

            # Launch three new threads to add a bunch of users
            add_users = stress_tests.AddUsers(m1, 'employee', True)
            add_users.start()
            add_users2 = stress_tests.AddUsers(m1, 'entry', True)
            add_users2.start()
            add_users3 = stress_tests.AddUsers(m1, 'person', True)
            add_users3.start()
            time.sleep(1)

            # While we are adding users restart the MO plugin and an idle plugin
            m1.plugins.disable(name=PLUGIN_MEMBER_OF)
            m1.plugins.enable(name=PLUGIN_MEMBER_OF)
            time.sleep(1)
            m1.plugins.disable(name=PLUGIN_MEMBER_OF)
            time.sleep(1)
            m1.plugins.enable(name=PLUGIN_MEMBER_OF)
            m1.plugins.disable(name=PLUGIN_LINKED_ATTRS)
            m1.plugins.enable(name=PLUGIN_LINKED_ATTRS)
            time.sleep(1)
            m1.plugins.disable(name=PLUGIN_MEMBER_OF)
            m1.plugins.enable(name=PLUGIN_MEMBER_OF)
            time.sleep(2)
            m1.plugins.disable(name=PLUGIN_MEMBER_OF)
            time.sleep(1)
            m1.plugins.enable(name=PLUGIN_MEMBER_OF)
            m1.plugins.disable(name=PLUGIN_LINKED_ATTRS)
            m1.plugins.enable(name=PLUGIN_LINKED_ATTRS)
            m1.plugins.disable(name=PLUGIN_MEMBER_OF)
            time.sleep(1)
            m1.plugins.enable(name=PLUGIN_MEMBER_OF)
            m1.plugins.disable(name=PLUGIN_MEMBER_OF)
            m1.plugins.enable(name=PLUGIN_MEMBER_OF)

            # Wait for the 'adding' threads to complete
            add_users.join()
            add_users2.join()
            add_users3.join()

            # Now launch three threads to delete the users
            del_users = stress_tests.DelUsers(m1, 'employee')
            del_users.start()
            del_users2 = stress_tests.DelUsers(m1, 'entry')
            del_users2.start()
            del_users3 = stress_tests.DelUsers(m1, 'person')
            del_users3.start()
            time.sleep(1)

            # Restart both the MO, RI plugins during these deletes, and an idle plugin
            m1.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
            m1.plugins.disable(name=PLUGIN_MEMBER_OF)
            m1.plugins.enable(name=PLUGIN_MEMBER_OF)
            m1.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
            time.sleep(1)
            m1.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
            time.sleep(1)
            m1.plugins.disable(name=PLUGIN_MEMBER_OF)
            time.sleep(1)
            m1.plugins.enable(name=PLUGIN_MEMBER_OF)
            time.sleep(1)
            m1.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
            m1.plugins.disable(name=PLUGIN_LINKED_ATTRS)
            m1.plugins.enable(name=PLUGIN_LINKED_ATTRS)
            m1.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
            m1.plugins.disable(name=PLUGIN_MEMBER_OF)
            m1.plugins.enable(name=PLUGIN_MEMBER_OF)
            m1.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
            time.sleep(2)
            m1.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
            time.sleep(1)
            m1.plugins.disable(name=PLUGIN_MEMBER_OF)
            time.sleep(1)
            m1.plugins.enable(name=PLUGIN_MEMBER_OF)
            time.sleep(1)
            m1.plugins.enable(name=PLUGIN_REFER_INTEGRITY)
            m1.plugins.disable(name=PLUGIN_LINKED_ATTRS)
            m1.plugins.enable(name=PLUGIN_LINKED_ATTRS)

            # Wait for the 'deleting' threads to complete
            del_users.join()
            del_users2.join()
            del_users3.join()

            # Now make sure both the MO and RI plugins still work correctly
            acceptance_test.func_tests[8](topology_m2, "dynamic")  # RI plugin
            acceptance_test.func_tests[5](topology_m2, "dynamic")  # MO plugin

            # Cleanup the stress tests
            stress_tests.cleanup(m1)

            stress_count += 1
            log.info('####################################################################')
            log.info('Successfully Stressed Dynamic Plugins' + msg +
                     '.  Completed (%d/%d)' % (stress_count, stress_max_runs))
            log.info('####################################################################\n')

        if replication_run:
            # We're done.
            break
        else:
            log.info('Resume replication and run everything one more time')
            topology_m2.resume_all_replicas()

            replication_run = True
            msg = ' (replication enabled)'
            time.sleep(1)

    ############################################################################
    # Check replication, and data are in sync
    ############################################################################
    check_replicas(topology_m2)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
