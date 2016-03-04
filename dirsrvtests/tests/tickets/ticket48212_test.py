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
from lib389.utils import *
from ldap.controls import SimplePagedResultsControl

log = logging.getLogger(__name__)

installation1_prefix = None

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
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Delete each instance in the end
    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)

def runDbVerify(topology):
    topology.standalone.log.info("\n\n	+++++ dbverify +++++\n")
    sbin_dir = get_sbin_dir(prefix=topology.standalone.prefix)
    dbverifyCMD = sbin_dir + "/dbverify -Z " + topology.standalone.inst + " -V"
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
    sbin_dir = get_sbin_dir(prefix=topology.standalone.prefix)
    indexCMD = sbin_dir + "/db2index.pl -Z " + topology.standalone.inst + " -D \"" + DN_DM + "\" -w \"" + PASSWORD + "\" -n " + MYSUFFIXBE + " -t uidnumber"

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
        
def test_ticket48212(topology):
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

    log.info('Testcase PASSED')

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

