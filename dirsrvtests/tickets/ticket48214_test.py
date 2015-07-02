import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from ldap.controls import SimplePagedResultsControl

log = logging.getLogger(__name__)

installation_prefix = None

MYSUFFIX = 'dc=example,dc=com'
MYSUFFIXBE = 'userRoot'

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

def getMaxBerSizeFromDseLdif(topology):
    topology.standalone.log.info("		+++++ Get maxbersize from dse.ldif +++++\n")
    dse_ldif = topology.standalone.confdir + '/dse.ldif'
    grepMaxBerCMD = "egrep nsslapd-maxbersize " + dse_ldif
    topology.standalone.log.info("		Run CMD: %s\n" % grepMaxBerCMD)
    grepMaxBerOUT = os.popen(grepMaxBerCMD, "r")
    running = True
    maxbersize = -1
    while running:
        l = grepMaxBerOUT.readline()
        if l == "":
            topology.standalone.log.info("		Empty: %s\n" % l)
            running = False
        elif "nsslapd-maxbersize:" in l.lower():
            running = False
            fields = l.split()
            if len(fields) >= 2:
                maxbersize = fields[1]
                topology.standalone.log.info("		Right format - %s %s\n" % (fields[0], fields[1]))
            else:
                topology.standalone.log.info("		Wrong format - %s\n" % l)
        else:
            topology.standalone.log.info("		Else?: %s\n" % l)
    return maxbersize

def checkMaxBerSize(topology):
    topology.standalone.log.info("	+++++ Check Max Ber Size +++++\n")
    maxbersizestr = getMaxBerSizeFromDseLdif(topology)
    maxbersize = int(maxbersizestr)
    isdefault = True
    defaultvalue = 2097152
    if maxbersize < 0:
        topology.standalone.log.info("	No nsslapd-maxbersize found in dse.ldif\n")
    elif maxbersize == 0:
        topology.standalone.log.info("	nsslapd-maxbersize: %d\n" % maxbersize)
    else:
        isdefault = False
        topology.standalone.log.info("	nsslapd-maxbersize: %d\n" % maxbersize)

    try:
        entry = topology.standalone.search_s('cn=config', ldap.SCOPE_BASE,
                                             "(cn=*)",
                                              ['nsslapd-maxbersize'])
        if entry:
            searchedsize = entry[0].getValue('nsslapd-maxbersize')
            topology.standalone.log.info("	ldapsearch returned nsslapd-maxbersize: %s\n" % searchedsize)
        else:
            topology.standalone.log.fatal('ERROR: cn=config is not found?')
            assert False
    except ldap.LDAPError, e:
        topology.standalone.log.error('ERROR: Failed to search for user entry: ' + e.message['desc'])
        assert False

    if isdefault:
        topology.standalone.log.info("	Checking %d vs %d\n" % (int(searchedsize), defaultvalue))
        assert int(searchedsize) == defaultvalue


def test_ticket48214_run(topology):
    """
    Check ldapsearch returns the correct maxbersize when it is not explicitly set.
    """
    log.info('Testing Ticket 48214 - ldapsearch on nsslapd-maxbersize returns 0 instead of current value')

    # bind as directory manager
    topology.standalone.log.info("Bind as %s" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    topology.standalone.log.info("\n\n######################### Out of Box ######################\n")
    checkMaxBerSize(topology)

    topology.standalone.log.info("\n\n######################### Add nsslapd-maxbersize: 0 ######################\n")
    topology.standalone.modify_s('cn=config', [(ldap.MOD_REPLACE, 'nsslapd-maxbersize', '0')])
    checkMaxBerSize(topology)

    topology.standalone.log.info("\n\n######################### Add nsslapd-maxbersize: 10000 ######################\n")
    topology.standalone.modify_s('cn=config', [(ldap.MOD_REPLACE, 'nsslapd-maxbersize', '10000')])
    checkMaxBerSize(topology)

    topology.standalone.log.info("ticket48214 was successfully verified.")


def test_ticket48214_final(topology):
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
    test_ticket48214_run(topo)

    test_ticket48214_final(topo)


if __name__ == '__main__':
    run_isolated()

