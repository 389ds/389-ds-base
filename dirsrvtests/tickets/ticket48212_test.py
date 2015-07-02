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
_MYLDIF = 'example1k_posix.ldif'
UIDNUMBERDN = "cn=uidnumber,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config"

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

def runDbVerify(topology):
    topology.standalone.log.info("\n\n	+++++ dbverify +++++\n")
    dbverifyCMD = topology.standalone.sroot + "/slapd-" + topology.standalone.inst + "/dbverify -V"
    dbverifyOUT = os.popen(dbverifyCMD, "r")
    topology.standalone.log.info("Running %s" % dbverifyCMD)
    running = True
    error = False
    while running:
        l = dbverifyOUT.readline()
        if l == "":
            running = False
        elif "libdb:" in l:
            running = False
            error = True
            topology.standalone.log.info("%s" % l)
        elif "verify failed" in l:
            error = True
            running = False
            topology.standalone.log.info("%s" % l)

    if error:
        topology.standalone.log.fatal("dbverify failed")
        assert False
    else:
        topology.standalone.log.info("dbverify passed")
        
def reindexUidNumber(topology):
    topology.standalone.log.info("\n\n	+++++ reindex uidnumber +++++\n")
    indexCMD = topology.standalone.sroot + "/slapd-" + topology.standalone.inst + "/db2index.pl -D \"" + DN_DM + "\" -w \"" + PASSWORD + "\" -n " + MYSUFFIXBE + " -t uidnumber"

    indexOUT = os.popen(indexCMD, "r")
    topology.standalone.log.info("Running %s" % indexCMD)

    time.sleep(10)

    tailCMD = "tail -n 3 " + topology.standalone.errlog
    tailOUT = os.popen(tailCMD, "r")
    running = True
    done = False
    while running:
        l = tailOUT.readline()
        if l == "":
            running = False
        elif "Finished indexing" in l:
            running = False
            done = True
            topology.standalone.log.info("%s" % l)
        
    if done:
        topology.standalone.log.info("%s done" % indexCMD)
    else:
        topology.standalone.log.fatal("%s did not finish" % indexCMD)
        assert False
        
def test_ticket48212_run(topology):
    """
    Import posixAccount entries.
    Index uidNumber
    add nsMatchingRule: integerOrderingMatch
    run dbverify to see if it reports the db corruption or not
    delete nsMatchingRule: integerOrderingMatch
    run dbverify to see if it reports the db corruption or not
    if no corruption is reported, the bug fix was verified.
    """
    log.info('Testing Ticket 48212 - Dynamic nsMatchingRule changes had no effect on the attrinfo thus following reindexing, as well.')

    # bind as directory manager
    topology.standalone.log.info("Bind as %s" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)


    data_dir_path = topology.standalone.getDir(__file__, DATA_DIR)
    ldif_file = data_dir_path + "ticket48212/" + _MYLDIF
    topology.standalone.log.info("\n\n######################### Import Test data (%s) ######################\n" % ldif_file)
    args = {TASK_WAIT: True}
    importTask = Tasks(topology.standalone)
    importTask.importLDIF(MYSUFFIX, MYSUFFIXBE, ldif_file, args)
    args = {TASK_WAIT: True}

    runDbVerify(topology)

    topology.standalone.log.info("\n\n######################### Add index by uidnumber ######################\n")
    try:
        topology.standalone.add_s(Entry((UIDNUMBERDN, {'objectclass': "top nsIndex".split(),
                                                       'cn': 'uidnumber',
                                                       'nsSystemIndex': 'false',
                                                       'nsIndexType': "pres eq".split()})))
    except ValueError:
        topology.standalone.log.fatal("add_s failed: %s", ValueError)

    topology.standalone.log.info("\n\n######################### reindexing... ######################\n")
    reindexUidNumber(topology)

    runDbVerify(topology)

    topology.standalone.log.info("\n\n######################### Add nsMatchingRule ######################\n")
    try:
        topology.standalone.modify_s(UIDNUMBERDN, [(ldap.MOD_ADD, 'nsMatchingRule', 'integerOrderingMatch')])
    except ValueError:
        topology.standalone.log.fatal("modify_s failed: %s", ValueError)

    topology.standalone.log.info("\n\n######################### reindexing... ######################\n")
    reindexUidNumber(topology)

    runDbVerify(topology)

    topology.standalone.log.info("\n\n######################### Delete nsMatchingRule ######################\n")
    try:
        topology.standalone.modify_s(UIDNUMBERDN, [(ldap.MOD_DELETE, 'nsMatchingRule', 'integerOrderingMatch')])
    except ValueError:
        topology.standalone.log.fatal("modify_s failed: %s", ValueError)

    reindexUidNumber(topology)

    runDbVerify(topology)

    topology.standalone.log.info("ticket48212 was successfully verified.")


def test_ticket48212_final(topology):
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
    test_ticket48212_run(topo)

    test_ticket48212_final(topo)


if __name__ == '__main__':
    run_isolated()

