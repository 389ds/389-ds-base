# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import ldap.sasl
import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

log = logging.getLogger(__name__)

USER_DN = 'uid=user1,%s' % (DEFAULT_SUFFIX)
SCHEMA_RELOAD_COUNT = 10


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
    except ldap.LDAPError as e:
        log.fatal('wait_for_task: Search failed: ' + e.message['desc'])
        assert False

    return finished


def test_ticket47973(topology_st):
    """
        During the schema reload task there is a small window where the new schema is not loaded
        into the asi hashtables - this results in searches not returning entries.
    """

    log.info('Testing Ticket 47973 - Test the searches still work as expected during schema reload tasks')

    #
    # Add a user
    #
    try:
        topology_st.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'user1'
        })))
    except ldap.LDAPError as e:
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
            topology_st.standalone.add_s(Entry((TASK_DN, {
                'objectclass': 'top extensibleObject'.split(),
                'cn': 'task-' + str(task_count)
            })))
        except ldap.LDAPError as e:
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
                entries = topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                                          ldap.SCOPE_SUBTREE,
                                                          '(uid=user1)')
                if not entries or not entries[0]:
                    log.fatal('User was not returned from search!')
                    assert False
            except ldap.LDAPError as e:
                log.fatal('Unable to search for entry %s: error %s' % (USER_DN, e.message['desc']))
                assert False

            #
            # Check if task is complete
            #
            if task_complete(topology_st.standalone, TASK_DN):
                break

            search_count += 1

        task_count += 1


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
