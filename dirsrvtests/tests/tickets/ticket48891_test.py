# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import fnmatch
import logging

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

from lib389._constants import DN_DM, PASSWORD, DEFAULT_SUFFIX, BACKEND_NAME, SUFFIX

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
RDN_VAL_SUFFIX = 'ticket48891.org'
MYSUFFIX = 'dc=%s' % RDN_VAL_SUFFIX
MYSUFFIXBE = 'ticket48891'

SEARCHFILTER = '(objectclass=person)'

OTHER_NAME = 'other_entry'
MAX_OTHERS = 10


def test_ticket48891_setup(topology_st):
    """
    Check there is no core
    Create a second backend
    stop DS (that should trigger the core)
    check there is no core
    """
    log.info('Testing Ticket 48891 - ns-slapd crashes during the shutdown after adding attribute with a matching rule')

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # check there is no core
    path = topology_st.standalone.config.get_attr_val_utf8('nsslapd-errorlog').replace('errors', '')
    log.debug('Looking for a core file in: ' + path)
    cores = fnmatch.filter(os.listdir(path), 'core.*')
    assert len(cores) == 0

    topology_st.standalone.log.info(
        "\n\n######################### SETUP SUFFIX o=ticket48891.org ######################\n")

    topology_st.standalone.backend.create(MYSUFFIX, {BACKEND_NAME: MYSUFFIXBE})
    topology_st.standalone.mappingtree.create(MYSUFFIX, bename=MYSUFFIXBE)
    topology_st.standalone.add_s(Entry((MYSUFFIX, {
        'objectclass': "top domain".split(),
        'dc': RDN_VAL_SUFFIX})))

    topology_st.standalone.log.info("\n\n######################### Generate Test data ######################\n")

    # add dummy entries on both backends
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology_st.standalone.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology_st.standalone.add_s(Entry(("cn=%s,%s" % (name, MYSUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))

    topology_st.standalone.log.info("\n\n######################### SEARCH ALL ######################\n")
    topology_st.standalone.log.info("Bind as %s and add the READ/SEARCH SELFDN aci" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    entries = topology_st.standalone.search_s(MYSUFFIX, ldap.SCOPE_SUBTREE, SEARCHFILTER)
    topology_st.standalone.log.info("Returned %d entries.\n", len(entries))

    assert MAX_OTHERS == len(entries)

    topology_st.standalone.log.info('%d person entries are successfully created under %s.' % (len(entries), MYSUFFIX))
    topology_st.standalone.stop(timeout=1)

    cores = fnmatch.filter(os.listdir(path), 'core.*')
    for core in cores:
        core = os.path.join(path, core)
        topology_st.standalone.log.info('cores are %s' % core)
        assert not os.path.isfile(core)

    log.info('Testcase PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
