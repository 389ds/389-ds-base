# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st
from lib389.utils import *

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

ATTRIBUTE_UNIQUENESS_PLUGIN = 'cn=attribute uniqueness,cn=plugins,cn=config'
ENTRY_NAME = 'test_entry'


def test_ticket47808_run(topology_st):
    """
        It enables attribute uniqueness plugin with sn as a unique attribute
        Add an entry 1 with sn = ENTRY_NAME
        Add an entry 2 with sn = ENTRY_NAME
        If the second add does not crash the server and the following search found none,
        the bug is fixed.
    """

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    topology_st.standalone.log.info("\n\n######################### SETUP ATTR UNIQ PLUGIN ######################\n")

    # enable attribute uniqueness plugin
    mod = [(ldap.MOD_REPLACE, 'nsslapd-pluginEnabled', b'on'), (ldap.MOD_REPLACE, 'nsslapd-pluginarg0', b'sn'),
           (ldap.MOD_REPLACE, 'nsslapd-pluginarg1', ensure_bytes(SUFFIX))]
    topology_st.standalone.modify_s(ATTRIBUTE_UNIQUENESS_PLUGIN, mod)

    topology_st.standalone.log.info("\n\n######################### ADD USER 1 ######################\n")

    # Prepare entry 1
    entry_name = '%s 1' % (ENTRY_NAME)
    entry_dn_1 = 'cn=%s, %s' % (entry_name, SUFFIX)
    entry_1 = Entry(entry_dn_1)
    entry_1.setValues('objectclass', 'top', 'person')
    entry_1.setValues('sn', ENTRY_NAME)
    entry_1.setValues('cn', entry_name)
    topology_st.standalone.log.info("Try to add Add %s: %r" % (entry_1, entry_1))
    topology_st.standalone.add_s(entry_1)

    topology_st.standalone.log.info("\n\n######################### Restart Server ######################\n")
    topology_st.standalone.stop(timeout=10)
    topology_st.standalone.start(timeout=10)

    topology_st.standalone.log.info("\n\n######################### ADD USER 2 ######################\n")

    # Prepare entry 2 having the same sn, which crashes the server
    entry_name = '%s 2' % (ENTRY_NAME)
    entry_dn_2 = 'cn=%s, %s' % (entry_name, SUFFIX)
    entry_2 = Entry(entry_dn_2)
    entry_2.setValues('objectclass', 'top', 'person')
    entry_2.setValues('sn', ENTRY_NAME)
    entry_2.setValues('cn', entry_name)
    topology_st.standalone.log.info("Try to add Add %s: %r" % (entry_2, entry_2))
    try:
        topology_st.standalone.add_s(entry_2)
    except:
        topology_st.standalone.log.warning("Adding %s failed" % entry_dn_2)
        pass

    topology_st.standalone.log.info("\n\n######################### IS SERVER UP? ######################\n")
    ents = topology_st.standalone.search_s(entry_dn_1, ldap.SCOPE_BASE, '(objectclass=*)')
    assert len(ents) == 1
    topology_st.standalone.log.info("Yes, it's up.")

    topology_st.standalone.log.info("\n\n######################### CHECK USER 2 NOT ADDED ######################\n")
    topology_st.standalone.log.info("Try to search %s" % entry_dn_2)
    try:
        ents = topology_st.standalone.search_s(entry_dn_2, ldap.SCOPE_BASE, '(objectclass=*)')
    except ldap.NO_SUCH_OBJECT:
        topology_st.standalone.log.info("Found none")

    topology_st.standalone.log.info("\n\n######################### DELETE USER 1 ######################\n")

    topology_st.standalone.log.info("Try to delete  %s " % entry_dn_1)
    topology_st.standalone.delete_s(entry_dn_1)
    log.info('Testcase PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
