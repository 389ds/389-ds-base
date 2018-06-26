# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import re

import ldap.sasl
import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

from lib389._constants import SUFFIX, DEFAULT_SUFFIX

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
        log.fatal('wait_for_task: Search failed: ' + e.args[0]['desc'])
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
        log.error('Failed to add user1: error ' + e.args[0]['desc'])
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
            log.error('Failed to add task entry: error ' + e.args[0]['desc'])
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
                log.fatal('Unable to search for entry %s: error %s' % (USER_DN, e.args[0]['desc']))
                assert False

            #
            # Check if task is complete
            #
            if task_complete(topology_st.standalone, TASK_DN):
                break

            search_count += 1

        task_count += 1


def test_ticket47973_case(topology_st):
    log.info('Testing Ticket 47973 (case) - Test the cases in the original schema are preserved.')

    log.info('case 1 - Test the cases in the original schema are preserved.')

    tsfile = topology_st.standalone.schemadir + '/98test.ldif'
    tsfd = open(tsfile, "w")
    Mozattr0 = "MoZiLLaaTTRiBuTe"
    testschema = "dn: cn=schema\nattributetypes: ( 8.9.10.11.12.13.14 NAME '" + Mozattr0 + "' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN 'Mozilla Dummy Schema' )\nobjectclasses: ( 1.2.3.4.5.6.7 NAME 'MozillaObject' SUP top MUST ( objectclass $ cn ) MAY ( " + Mozattr0 + " ) X-ORIGIN 'user defined' )"
    tsfd.write(testschema)
    tsfd.close()

    try:
        # run the schema reload task with the default schemadir
        topology_st.standalone.tasks.schemaReload(schemadir=topology_st.standalone.schemadir,
                                                  args={TASK_WAIT: False})
    except ValueError:
        log.error('Schema Reload task failed.')
        assert False

    time.sleep(5)

    try:
        schemaentry = topology_st.standalone.search_s("cn=schema", ldap.SCOPE_BASE,
                                             'objectclass=top',
                                             ["objectclasses"])
        oclist = schemaentry[0].data.get("objectclasses")
    except ldap.LDAPError as e:
        log.error('Failed to get schema entry: error (%s)' % e.args[0]['desc'])
        raise e

    found = 0
    for oc in oclist:
        log.info('OC: %s' % oc)
        moz = re.findall(Mozattr0, oc.decode('utf-8'))
        if moz:
            found = 1
            log.info('case 1: %s is in the objectclasses list -- PASS' % Mozattr0)

    if found == 0:
        log.error('case 1: %s is not in the objectclasses list -- FAILURE' % Mozattr0)
        assert False

    log.info('case 2 - Duplicated schema except cases are not loaded.')

    tsfile = topology_st.standalone.schemadir + '/97test.ldif'
    tsfd = open(tsfile, "w")
    Mozattr1 = "MOZILLAATTRIBUTE"
    testschema = "dn: cn=schema\nattributetypes: ( 8.9.10.11.12.13.14 NAME '" + Mozattr1 + "' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN 'Mozilla Dummy Schema' )\nobjectclasses: ( 1.2.3.4.5.6.7 NAME 'MozillaObject' SUP top MUST ( objectclass $ cn ) MAY ( " + Mozattr1 + " ) X-ORIGIN 'user defined' )"
    tsfd.write(testschema)
    tsfd.close()

    try:
        # run the schema reload task with the default schemadir
        topology_st.standalone.tasks.schemaReload(schemadir=topology_st.standalone.schemadir,
                                                  args={TASK_WAIT: False})
    except ValueError:
        log.error('Schema Reload task failed.')
        assert False

    time.sleep(5)

    try:
        schemaentry = topology_st.standalone.search_s("cn=schema", ldap.SCOPE_BASE,
                                             'objectclass=top',
                                             ["objectclasses"])
        oclist = schemaentry[0].data.get("objectclasses")
    except ldap.LDAPError as e:
        log.error('Failed to get schema entry: error (%s)' % e.args[0]['desc'])
        raise e

    for oc in oclist:
        log.info('OC: %s' % oc)
        moz = re.findall(Mozattr1, oc.decode('utf-8'))
        if moz:
            log.error('case 2: %s is in the objectclasses list -- FAILURE' % Mozattr1)
            assert False

    log.info('case 2: %s is not in the objectclasses list -- PASS' % Mozattr1)

    Mozattr2 = "mozillaattribute"
    log.info('case 2-1: Use the custom schema with %s' % Mozattr2)
    name = "test user"
    try:
        topology_st.standalone.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
                                            'objectclass': "top person MozillaObject".split(),
                                                            'sn': name,
                                                            'cn': name,
                                                            Mozattr2: name})))
    except ldap.LDAPError as e:
        log.error('Failed to add a test entry: error (%s)' % e.args[0]['desc'])
        raise e

    try:
        testentry = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE,
                                                    'objectclass=mozillaobject',
                                                    [Mozattr2])
    except ldap.LDAPError as e:
        log.error('Failed to get schema entry: error (%s)' % e.args[0]['desc'])
        raise e

    mozattrval = testentry[0].data.get(Mozattr2)
    if mozattrval[0] == name:
        log.info('case 2-1: %s: %s found-- PASS' % (Mozattr2, name))
    else:
        log.info('case 2-1: %s: %s not found-- FAILURE' % (Mozattr2, mozattrval[0]))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
