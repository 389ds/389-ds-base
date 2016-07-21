# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *

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

    # Get the status of the instance
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def test_ticket47560(topology):
    """
       This test case does the following:
          SETUP
            - Create entry cn=group,SUFFIX
            - Create entry cn=member,SUFFIX
            - Update 'cn=member,SUFFIX' to add "memberOf: cn=group,SUFFIX"
            - Enable Memberof Plugins

            # Here the cn=member entry has a 'memberOf' but
            # cn=group entry does not contain 'cn=member' in its member

          TEST CASE
            - start the fixupmemberof task
            - read the cn=member entry
            - check 'memberOf is now empty

           TEARDOWN
            - Delete entry cn=group,SUFFIX
            - Delete entry cn=member,SUFFIX
            - Disable Memberof Plugins
    """

    def _enable_disable_mbo(value):
        """
            Enable or disable mbo plugin depending on 'value' ('on'/'off')
        """
        # enable/disable the mbo plugin
        if value == 'on':
            topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
        else:
            topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)

        log.debug("-------------> _enable_disable_mbo(%s)" % value)

        topology.standalone.stop(timeout=120)
        time.sleep(1)
        topology.standalone.start(timeout=120)
        time.sleep(3)

        # need to reopen a connection toward the instance
        topology.standalone.open()

    def _test_ticket47560_setup():
        """
        - Create entry cn=group,SUFFIX
        - Create entry cn=member,SUFFIX
        - Update 'cn=member,SUFFIX' to add "memberOf: cn=group,SUFFIX"
        - Enable Memberof Plugins
        """
        log.debug("-------- > _test_ticket47560_setup\n")

        #
        # By default the memberof plugin is disabled create
        # - create a group entry
        # - create a member entry
        # - set the member entry as memberof the group entry
        #
        entry = Entry(group_DN)
        entry.setValues('objectclass', 'top', 'groupOfNames', 'inetUser')
        entry.setValues('cn', 'group')
        try:
            topology.standalone.add_s(entry)
        except ldap.ALREADY_EXISTS:
            log.debug("Entry %s already exists" % (group_DN))

        entry = Entry(member_DN)
        entry.setValues('objectclass', 'top', 'person', 'organizationalPerson', 'inetorgperson', 'inetUser')
        entry.setValues('uid', 'member')
        entry.setValues('cn', 'member')
        entry.setValues('sn', 'member')
        try:
            topology.standalone.add_s(entry)
        except ldap.ALREADY_EXISTS:
            log.debug("Entry %s already exists" % (member_DN))

        replace = [(ldap.MOD_REPLACE, 'memberof', group_DN)]
        topology.standalone.modify_s(member_DN, replace)

        #
        # enable the memberof plugin and restart the instance
        #
        _enable_disable_mbo('on')

        #
        # check memberof attribute is still present
        #
        filt = 'uid=member'
        ents = topology.standalone.search_s(member_DN, ldap.SCOPE_BASE, filt)
        assert len(ents) == 1
        ent = ents[0]
        #print ent
        value = ent.getValue('memberof')
        #print "memberof: %s" % (value)
        assert value == group_DN

    def _test_ticket47560_teardown():
        """
            - Delete entry cn=group,SUFFIX
            - Delete entry cn=member,SUFFIX
            - Disable Memberof Plugins
        """
        log.debug("-------- > _test_ticket47560_teardown\n")
        # remove the entries group_DN and member_DN
        try:
            topology.standalone.delete_s(group_DN)
        except:
            log.warning("Entry %s fail to delete" % (group_DN))
        try:
            topology.standalone.delete_s(member_DN)
        except:
            log.warning("Entry %s fail to delete" % (member_DN))
        #
        # disable the memberof plugin and restart the instance
        #
        _enable_disable_mbo('off')

    group_DN  = "cn=group,%s"   % (SUFFIX)
    member_DN = "uid=member,%s" % (SUFFIX)

    #
    # Initialize the test case
    #
    _test_ticket47560_setup()

    #
    # start the test
    #   - start the fixup task
    #   - check the entry is fixed (no longer memberof the group)
    #
    log.debug("-------- > Start ticket tests\n")

    filt = 'uid=member'
    ents = topology.standalone.search_s(member_DN, ldap.SCOPE_BASE, filt)
    assert len(ents) == 1
    ent = ents[0]
    log.debug("Unfixed entry %r\n" % ent)

    # run the fixup task
    topology.standalone.tasks.fixupMemberOf(suffix=SUFFIX, args={TASK_WAIT: True})

    ents = topology.standalone.search_s(member_DN, ldap.SCOPE_BASE, filt)
    assert len(ents) == 1
    ent = ents[0]
    log.debug("Fixed entry %r\n" % ent)

    if ent.getValue('memberof') == group_DN:
        log.warning("Error the fixupMemberOf did not fix %s" % (member_DN))
        result_successful = False
    else:
        result_successful = True

    #
    # cleanup up the test case
    #
    _test_ticket47560_teardown()

    assert result_successful is True


def test_ticket47560_final(topology):
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
    test_ticket47560(topo)
    test_ticket47560_final(topo)


if __name__ == '__main__':
    run_isolated()

