import os
import sys
import time
import ldap
import ldap.sasl
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *

log = logging.getLogger(__name__)

installation_prefix = None

USER_DN = 'uid=user1,%s' % (DEFAULT_SUFFIX)
SCHEMA_RELOAD_COUNT = 10


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


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

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def task_complete(conn, task_dn):
    finished = False

    try:
        task_entry = conn.search_s(task_dn, ldap.SCOPE_BASE, 'objectclass=*')
        if not task_entry:
            log.fatal('wait_for_task: Search failed to find task: ' + task_dn)
            assert False
        if task_entry[0].hasAttr('nstaskexitcode'):
            # task is done
            finished = True
    except ldap.LDAPError, e:
        log.fatal('wait_for_task: Search failed: ' + e.message['desc'])
        assert False

    return finished


def test_ticket47973(topology):
    """
        During the schema reload task there is a small window where the new schema is not loaded
        into the asi hashtables - this results in searches not returning entries.
    """

    log.info('Testing Ticket 47973 - Test the searches still work as expected during schema reload tasks')

    #
    # Add a user
    #
    try:
        topology.standalone.add_s(Entry((USER_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError, e:
        log.error('Failed to add user1: error ' + e.message['desc'])
        assert False

    #
    # Run a series of schema_reload tasks while searching for our user.  Since
    # this is a race condition, run it several times.
    #
    task_count = 0
    while task_count < SCHEMA_RELOAD_COUNT:
        #
        # Add a schema reload task
        #

        TASK_DN = 'cn=task-' + str(task_count) + ',cn=schema reload task, cn=tasks, cn=config'
        try:
            topology.standalone.add_s(Entry((TASK_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'cn': 'task-' + str(task_count)
                          })))
        except ldap.LDAPError, e:
            log.error('Failed to add task entry: error ' + e.message['desc'])
            assert False

        #
        # While we wait for the task to complete keep searching for our user
        #
        search_count = 0
        while search_count < 100:
            #
            # Now check the user is still being returned
            #
            try:
                entries = topology.standalone.search_s(DEFAULT_SUFFIX,
                                                      ldap.SCOPE_SUBTREE,
                                                      '(uid=user1)')
                if not entries or not entries[0]:
                    log.fatal('User was not returned from search!')
                    assert False
            except ldap.LDAPError, e:
                log.fatal('Unable to search for entry %s: error %s' % (USER_DN, e.message['desc']))
                assert False

            #
            # Check if task is complete
            #
            if task_complete(topology.standalone, TASK_DN):
                break

            search_count += 1

        task_count += 1


def test_ticket47973_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


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
    test_ticket47973(topo)
    test_ticket47973_final(topo)


if __name__ == '__main__':
    run_isolated()
