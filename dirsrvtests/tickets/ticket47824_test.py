import os
import sys
import time
import ldap
import logging
import socket
import time
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from constants import *
from ldap.controls import SimplePagedResultsControl

log = logging.getLogger(__name__)

installation_prefix = None

MYSUFFIX = 'o=ticket47824.org'
MYSUFFIXBE = 'ticket47824'
SUBSUFFIX0 = 'ou=OU0,o=ticket47824.org'
SUBSUFFIX0BE = 'OU0'
SUBSUFFIX1 = 'ou=OU1,o=ticket47824.org'
SUBSUFFIX1BE = 'OU1'
SUBSUFFIX2 = 'ou=OU2,o=ticket47824.org'
SUBSUFFIX2BE = 'OU2'

_MYLDIF = 'ticket47824.ldif'
_SUBLDIF0TMP = 'ticket47824_0.tmp'
_SUBLDIF0 = 'ticket47824_0.ldif'
_SUBLDIF1TMP = 'ticket47824_1.tmp'
_SUBLDIF1 = 'ticket47824_1.ldif'
_SUBLDIF2TMP = 'ticket47824_2.tmp'
_SUBLDIF2 = 'ticket47824_2.ldif'

SEARCHFILTER = '(objectclass=*)'

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
        At the beginning, It may exists a standalone instance.
        It may also exists a backup for the standalone instance.
    
        Principle:
            If standalone instance exists:
                restart it
            If backup of standalone exists:
                create/rebind to standalone

                restore standalone instance from backup
            else:
                Cleanup everything
                    remove instance
                    remove backup
                Create instance
                Create backup
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
        
    # Get the status of the backups
    backup_standalone = standalone.checkBackupFS()
    
    # Get the status of the instance and restart it if it exists
    instance_standalone   = standalone.exists()
    if instance_standalone:
        # assuming the instance is already stopped, just wait 5 sec max
        standalone.stop(timeout=5)
        standalone.start(timeout=10)

    if backup_standalone:
        # The backup exist, assuming it is correct 
        # we just re-init the instance with it
        if not instance_standalone:
            standalone.create()
            # Used to retrieve configuration information (dbdir, confdir...)
            standalone.open()
        
        # restore standalone instance from backup
        standalone.stop(timeout=10)
        standalone.restoreFS(backup_standalone)
        standalone.start(timeout=10)
        
    else:
        # We should be here only in two conditions
        #      - This is the first time a test involve standalone instance
        #      - Something weird happened (instance/backup destroyed)
        #        so we discard everything and recreate all
        
        # Remove the backup. So even if we have a specific backup file
        # (e.g backup_standalone) we clear backup that an instance may have created
        if backup_standalone:
            standalone.clearBackupFS()
        
        # Remove the instance
        if instance_standalone:
            standalone.delete()
            
        # Create the instance
        standalone.create()
        
        # Used to retrieve configuration information (dbdir, confdir...)
        standalone.open()
                
        # Time to create the backups
        standalone.stop(timeout=10)
        standalone.backupfile = standalone.backupFS()
        standalone.start(timeout=10)

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    # 
    # Here we have standalone instance up and running
    # Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyStandalone(standalone)


def test_ticket47824_run(topology):
    """
        Add 3 sub suffixes under the primary suffix
        Import 16 entries each
        Search with Simple Paged Results Control from the primary suffix (pagesize = 4)
        If all of them are returned, the bug is verified
    """
    log.info('Testing Ticket 47824 - paged results control is not working in some cases when we have a subsuffix')

    # bind as directory manager
    topology.standalone.log.info("Bind as %s" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    
    topology.standalone.log.info("\n\n######################### SETUP SUFFIX o=ticket47824.org ######################\n")

    topology.standalone.backend.create(MYSUFFIX, {BACKEND_NAME: MYSUFFIXBE})
    topology.standalone.mappingtree.create(MYSUFFIX, bename=MYSUFFIXBE)

    topology.standalone.log.info("\n\n######################### SETUP SUB SUFFIX ou=OU0 ######################\n")

    topology.standalone.backend.create(SUBSUFFIX0, {BACKEND_NAME: SUBSUFFIX0BE})
    topology.standalone.mappingtree.create(SUBSUFFIX0, bename=SUBSUFFIX0BE, parent=MYSUFFIX)

    topology.standalone.log.info("\n\n######################### SETUP SUB SUFFIX ou=OU1 ######################\n")

    topology.standalone.backend.create(SUBSUFFIX1, {BACKEND_NAME: SUBSUFFIX1BE})
    topology.standalone.mappingtree.create(SUBSUFFIX1, bename=SUBSUFFIX1BE, parent=MYSUFFIX)

    topology.standalone.log.info("\n\n######################### SETUP SUB SUFFIX ou=OU2 ######################\n")

    topology.standalone.backend.create(SUBSUFFIX2, {BACKEND_NAME: SUBSUFFIX2BE})
    topology.standalone.mappingtree.create(SUBSUFFIX2, bename=SUBSUFFIX2BE, parent=MYSUFFIX)

    topology.standalone.log.info("\n\n######################### Generate Test data ######################\n")

    # get tmp dir
    mytmp = topology.standalone.getDir(__file__, TMP_DIR)
    if mytmp == None:
        mytmp = "/tmp"

    MYLDIF = '%s%s' % (mytmp, _MYLDIF)
    SUBLDIF0TMP = '%s%s' % (mytmp, _SUBLDIF0TMP)
    SUBLDIF0 = '%s%s' % (mytmp, _SUBLDIF0)
    SUBLDIF1TMP = '%s%s' % (mytmp, _SUBLDIF1TMP)
    SUBLDIF1 = '%s%s' % (mytmp, _SUBLDIF1)
    SUBLDIF2TMP = '%s%s' % (mytmp, _SUBLDIF2TMP)
    SUBLDIF2 = '%s%s' % (mytmp, _SUBLDIF2)

    os.system('ls %s' % MYLDIF)
    os.system('ls %s' % SUBLDIF0TMP)
    os.system('ls %s' % SUBLDIF1TMP)
    os.system('ls %s' % SUBLDIF2TMP)
    os.system('rm -f %s' % MYLDIF)
    os.system('rm -f %s' % SUBLDIF0TMP)
    os.system('rm -f %s' % SUBLDIF1TMP)
    os.system('rm -f %s' % SUBLDIF2TMP)
    if hasattr(topology.standalone, 'prefix'):
        prefix = topology.standalone.prefix
    else:
        prefix = None
    dbgen_prog = prefix + '/bin/dbgen.pl'
    topology.standalone.log.info('dbgen: %s' % dbgen_prog)
    os.system('%s -s %s -o %s -n 10' % (dbgen_prog, MYSUFFIX, MYLDIF))
    os.system('%s -s %s -o %s -n 10' % (dbgen_prog, SUBSUFFIX0, SUBLDIF0TMP))
    os.system('%s -s %s -o %s -n 10' % (dbgen_prog, SUBSUFFIX1, SUBLDIF1TMP))
    os.system('%s -s %s -o %s -n 10' % (dbgen_prog, SUBSUFFIX2, SUBLDIF2TMP))

    os.system('cat %s | sed -e "s/\<objectClass: organization\>/objectClass: organizationalUnit/" | sed -e "/^o:.*/d" > %s' % (SUBLDIF0TMP, SUBLDIF0))
    os.system('cat %s | sed -e "s/\<objectClass: organization\>/objectClass: organizationalUnit/" | sed -e "/^o:.*/d" > %s' % (SUBLDIF1TMP, SUBLDIF1))
    os.system('cat %s | sed -e "s/\<objectClass: organization\>/objectClass: organizationalUnit/" | sed -e "/^o:.*/d" > %s' % (SUBLDIF2TMP, SUBLDIF2))

    cmdline = 'egrep dn: %s %s %s %s | wc -l' % (MYLDIF, SUBLDIF0, SUBLDIF1, SUBLDIF2)
    p = os.popen(cmdline, "r")
    dnnumstr = p.readline()
    dnnum = int(dnnumstr)
    topology.standalone.log.info("We have %d entries.\n", dnnum)

    topology.standalone.log.info("\n\n######################### Import Test data ######################\n")

    args = {TASK_WAIT: True}
    importTask = Tasks(topology.standalone)
    importTask.importLDIF(MYSUFFIX, MYSUFFIXBE, MYLDIF, args)
    importTask.importLDIF(SUBSUFFIX0, SUBSUFFIX0BE, SUBLDIF0, args)
    importTask.importLDIF(SUBSUFFIX1, SUBSUFFIX1BE, SUBLDIF1, args)
    importTask.importLDIF(SUBSUFFIX2, SUBSUFFIX2BE, SUBLDIF2, args)

    topology.standalone.log.info("\n\n######################### SEARCH ALL ######################\n")
    topology.standalone.log.info("Bind as %s and add the READ/SEARCH SELFDN aci" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    entries = topology.standalone.search_s(MYSUFFIX, ldap.SCOPE_SUBTREE, SEARCHFILTER)
    topology.standalone.log.info("Returned %d entries.\n", len(entries))

    #print entries

    assert dnnum == len(entries)

    topology.standalone.log.info('%d entries are successfully imported.' % dnnum)

    topology.standalone.log.info("\n\n######################### SEARCH WITH SIMPLE PAGED RESULTS CONTROL ######################\n")

    page_size = 4
    req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

    known_ldap_resp_ctrls = {
        SimplePagedResultsControl.controlType:SimplePagedResultsControl,
    }

    topology.standalone.log.info("Calling search_ext...")
    msgid = topology.standalone.search_ext(MYSUFFIX, ldap.SCOPE_SUBTREE, SEARCHFILTER, None, serverctrls=[req_ctrl])

    pageddncnt = 0
    pages = 0
    while True:
        pages += 1

        topology.standalone.log.info("Getting page %d" % pages)
        rtype, rdata, rmsgid, serverctrls = topology.standalone.result3(msgid, resp_ctrl_classes=known_ldap_resp_ctrls)
        topology.standalone.log.info("%d results" % len(rdata))
        pageddncnt += len(rdata)
     
        topology.standalone.log.info("Results:")
        for dn, attrs in rdata:
            topology.standalone.log.info("dn: %s" % dn)
     
        pctrls = [
            c for c in serverctrls if c.controlType == SimplePagedResultsControl.controlType
        ]
        if not pctrls:
            topology.standalone.log.info('Warning: Server ignores RFC 2696 control.')
            break
     
        if pctrls[0].cookie:
            req_ctrl.cookie = pctrls[0].cookie
            topology.standalone.log.info("cookie: %s" % req_ctrl.cookie)
            msgid = topology.standalone.search_ext(MYSUFFIX,
                                                   ldap.SCOPE_SUBTREE,
                                                   SEARCHFILTER,
                                                   None,
                                                   serverctrls=[req_ctrl])
        else:
            topology.standalone.log.info("No cookie")
            break

    topology.standalone.log.info("Paged result search returned %d entries.\n", pageddncnt)

    assert dnnum == len(entries)
    topology.standalone.log.info("ticket47824 was successfully verified.");

def test_ticket47824_final(topology):
    topology.standalone.stop(timeout=10)
    
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
    test_ticket47824_run(topo)
    
    test_ticket47824_final(topo)


if __name__ == '__main__':
    run_isolated()

