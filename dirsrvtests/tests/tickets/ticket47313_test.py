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

log = logging.getLogger(__name__)

ENTRY_NAME = 'test_entry'


def test_ticket47313_run(topology_st):
    """
        It adds 2 test entries
        Search with filters including subtype and !
            It deletes the added entries
    """

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # enable filter error logging
    # mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '32')]
    # topology_st.standalone.modify_s(DN_CONFIG, mod)

    topology_st.standalone.log.info("\n\n######################### ADD ######################\n")

    # Prepare the entry with cn;fr & cn;en
    entry_name_fr = '%s fr' % (ENTRY_NAME)
    entry_name_en = '%s en' % (ENTRY_NAME)
    entry_name_both = '%s both' % (ENTRY_NAME)
    entry_dn_both = 'cn=%s, %s' % (entry_name_both, SUFFIX)
    entry_both = Entry(entry_dn_both)
    entry_both.setValues('objectclass', 'top', 'person')
    entry_both.setValues('sn', entry_name_both)
    entry_both.setValues('cn', entry_name_both)
    entry_both.setValues('cn;fr', entry_name_fr)
    entry_both.setValues('cn;en', entry_name_en)

    # Prepare the entry with one member
    entry_name_en_only = '%s en only' % (ENTRY_NAME)
    entry_dn_en_only = 'cn=%s, %s' % (entry_name_en_only, SUFFIX)
    entry_en_only = Entry(entry_dn_en_only)
    entry_en_only.setValues('objectclass', 'top', 'person')
    entry_en_only.setValues('sn', entry_name_en_only)
    entry_en_only.setValues('cn', entry_name_en_only)
    entry_en_only.setValues('cn;en', entry_name_en)

    topology_st.standalone.log.info("Try to add Add %s: %r" % (entry_dn_both, entry_both))
    topology_st.standalone.add_s(entry_both)

    topology_st.standalone.log.info("Try to add Add %s: %r" % (entry_dn_en_only, entry_en_only))
    topology_st.standalone.add_s(entry_en_only)

    topology_st.standalone.log.info("\n\n######################### SEARCH ######################\n")

    # filter: (&(cn=test_entry en only)(!(cn=test_entry fr)))
    myfilter = '(&(sn=%s)(!(cn=%s)))' % (entry_name_en_only, entry_name_fr)
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1
    assert ents[0].sn == entry_name_en_only
    topology_st.standalone.log.info("Found %s" % ents[0].dn)

    # filter: (&(cn=test_entry en only)(!(cn;fr=test_entry fr)))
    myfilter = '(&(sn=%s)(!(cn;fr=%s)))' % (entry_name_en_only, entry_name_fr)
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1
    assert ents[0].sn == entry_name_en_only
    topology_st.standalone.log.info("Found %s" % ents[0].dn)

    # filter: (&(cn=test_entry en only)(!(cn;en=test_entry en)))
    myfilter = '(&(sn=%s)(!(cn;en=%s)))' % (entry_name_en_only, entry_name_en)
    topology_st.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 0
    topology_st.standalone.log.info("Found none")

    topology_st.standalone.log.info("\n\n######################### DELETE ######################\n")

    topology_st.standalone.log.info("Try to delete  %s " % entry_dn_both)
    topology_st.standalone.delete_s(entry_dn_both)

    topology_st.standalone.log.info("Try to delete  %s " % entry_dn_en_only)
    topology_st.standalone.delete_s(entry_dn_en_only)

    log.info('Testcase PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
