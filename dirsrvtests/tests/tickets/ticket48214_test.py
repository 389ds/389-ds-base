# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

MYSUFFIX = 'dc=example,dc=com'
MYSUFFIXBE = 'userRoot'


def getMaxBerSizeFromDseLdif(topology_st):
    topology_st.standalone.log.info("		+++++ Get maxbersize from dse.ldif +++++\n")
    dse_ldif = topology_st.standalone.confdir + '/dse.ldif'
    grepMaxBerCMD = "egrep nsslapd-maxbersize " + dse_ldif
    topology_st.standalone.log.info("		Run CMD: %s\n" % grepMaxBerCMD)
    grepMaxBerOUT = os.popen(grepMaxBerCMD, "r")
    running = True
    maxbersize = -1
    while running:
        l = grepMaxBerOUT.readline()
        if l == "":
            topology_st.standalone.log.info("		Empty: %s\n" % l)
            running = False
        elif "nsslapd-maxbersize:" in l.lower():
            running = False
            fields = l.split()
            if len(fields) >= 2:
                maxbersize = fields[1]
                topology_st.standalone.log.info("		Right format - %s %s\n" % (fields[0], fields[1]))
            else:
                topology_st.standalone.log.info("		Wrong format - %s\n" % l)
        else:
            topology_st.standalone.log.info("		Else?: %s\n" % l)
    return maxbersize


def checkMaxBerSize(topology_st):
    topology_st.standalone.log.info("	+++++ Check Max Ber Size +++++\n")
    maxbersizestr = getMaxBerSizeFromDseLdif(topology_st)
    maxbersize = int(maxbersizestr)
    isdefault = True
    defaultvalue = 2097152
    if maxbersize < 0:
        topology_st.standalone.log.info("	No nsslapd-maxbersize found in dse.ldif\n")
    elif maxbersize == 0:
        topology_st.standalone.log.info("	nsslapd-maxbersize: %d\n" % maxbersize)
    else:
        isdefault = False
        topology_st.standalone.log.info("	nsslapd-maxbersize: %d\n" % maxbersize)

    try:
        entry = topology_st.standalone.search_s('cn=config', ldap.SCOPE_BASE,
                                                "(cn=*)",
                                                ['nsslapd-maxbersize'])
        if entry:
            searchedsize = entry[0].getValue('nsslapd-maxbersize')
            topology_st.standalone.log.info("	ldapsearch returned nsslapd-maxbersize: %s\n" % searchedsize)
        else:
            topology_st.standalone.log.fatal('ERROR: cn=config is not found?')
            assert False
    except ldap.LDAPError as e:
        topology_st.standalone.log.error('ERROR: Failed to search for user entry: ' + e.message['desc'])
        assert False

    if isdefault:
        topology_st.standalone.log.info("	Checking %d vs %d\n" % (int(searchedsize), defaultvalue))
        assert int(searchedsize) == defaultvalue


def test_ticket48214_run(topology_st):
    """
    Check ldapsearch returns the correct maxbersize when it is not explicitly set.
    """
    log.info('Testing Ticket 48214 - ldapsearch on nsslapd-maxbersize returns 0 instead of current value')

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    topology_st.standalone.log.info("\n\n######################### Out of Box ######################\n")
    checkMaxBerSize(topology_st)

    topology_st.standalone.log.info("\n\n######################### Add nsslapd-maxbersize: 0 ######################\n")
    topology_st.standalone.modify_s('cn=config', [(ldap.MOD_REPLACE, 'nsslapd-maxbersize', b'0')])
    checkMaxBerSize(topology_st)

    topology_st.standalone.log.info(
        "\n\n######################### Add nsslapd-maxbersize: 10000 ######################\n")
    topology_st.standalone.modify_s('cn=config', [(ldap.MOD_REPLACE, 'nsslapd-maxbersize', b'10000')])
    checkMaxBerSize(topology_st)

    topology_st.standalone.log.info("ticket48214 was successfully verified.")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
