# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.properties import *
from lib389.topologies import topology_st
from lib389.utils import *

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)


def test_ticket47560(topology_st):
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
            topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
        else:
            topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)

        log.debug("-------------> _enable_disable_mbo(%s)" % value)

        topology_st.standalone.stop(timeout=120)
        time.sleep(1)
        topology_st.standalone.start(timeout=120)
        time.sleep(3)

        # need to reopen a connection toward the instance
        topology_st.standalone.open()

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
            topology_st.standalone.add_s(entry)
        except ldap.ALREADY_EXISTS:
            log.debug("Entry %s already exists" % (group_DN))

        entry = Entry(member_DN)
        entry.setValues('objectclass', 'top', 'person', 'organizationalPerson', 'inetorgperson', 'inetUser')
        entry.setValues('uid', 'member')
        entry.setValues('cn', 'member')
        entry.setValues('sn', 'member')
        try:
            topology_st.standalone.add_s(entry)
        except ldap.ALREADY_EXISTS:
            log.debug("Entry %s already exists" % (member_DN))

        replace = [(ldap.MOD_REPLACE, 'memberof', ensure_bytes(group_DN))]
        topology_st.standalone.modify_s(member_DN, replace)

        #
        # enable the memberof plugin and restart the instance
        #
        _enable_disable_mbo('on')

        #
        # check memberof attribute is still present
        #
        filt = 'uid=member'
        ents = topology_st.standalone.search_s(member_DN, ldap.SCOPE_BASE, filt)
        assert len(ents) == 1
        ent = ents[0]
        # print ent
        value = ensure_str(ent.getValue('memberof'))
        # print "memberof: %s" % (value)
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
            topology_st.standalone.delete_s(group_DN)
        except:
            log.warning("Entry %s fail to delete" % (group_DN))
        try:
            topology_st.standalone.delete_s(member_DN)
        except:
            log.warning("Entry %s fail to delete" % (member_DN))
        #
        # disable the memberof plugin and restart the instance
        #
        _enable_disable_mbo('off')

    group_DN = "cn=group,%s" % (SUFFIX)
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
    ents = topology_st.standalone.search_s(member_DN, ldap.SCOPE_BASE, filt)
    assert len(ents) == 1
    ent = ents[0]
    log.debug("Unfixed entry %r\n" % ent)

    # run the fixup task
    topology_st.standalone.tasks.fixupMemberOf(suffix=SUFFIX, args={TASK_WAIT: True})

    ents = topology_st.standalone.search_s(member_DN, ldap.SCOPE_BASE, filt)
    assert len(ents) == 1
    ent = ents[0]
    log.debug("Fixed entry %r\n" % ent)

    if ensure_str(ent.getValue('memberof')) == group_DN:
        log.warning("Error the fixupMemberOf did not fix %s" % (member_DN))
        result_successful = False
    else:
        result_successful = True

    #
    # cleanup up the test case
    #
    _test_ticket47560_teardown()

    assert result_successful is True

    log.info('Testcase PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
