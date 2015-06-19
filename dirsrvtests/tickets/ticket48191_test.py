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
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from ldap.controls import SimplePagedResultsControl
from ldap.controls.simple import GetEffectiveRightsControl

log = logging.getLogger(__name__)

installation_prefix = None

CONFIG_DN = 'cn=config'
MYSUFFIX = 'o=ticket48191.org'
MYSUFFIXBE = 'ticket48191'

_MYLDIF = 'ticket48191.ldif'

SEARCHFILTER = '(objectclass=*)'


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


def test_ticket48191_setup(topology):
    """
        Import 20 entries
        Set nsslapd-maxsimplepaged-per-conn in cn=config
        If the val is negative, no limit.
        If the value is 0, the simple paged results is disabled.
        If the value is positive, the value is the max simple paged results requests per connection.
        The setting has to be dynamic.
    """
    log.info('Testing Ticket 48191 - Config parameter nsslapd-maxsimplepaged-per-conn')

    # bind as directory manager
    topology.standalone.log.info("Bind as %s" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    topology.standalone.log.info("\n\n######################### SETUP SUFFIX o=ticket48191.org ######################\n")

    topology.standalone.backend.create(MYSUFFIX, {BACKEND_NAME: MYSUFFIXBE})
    topology.standalone.mappingtree.create(MYSUFFIX, bename=MYSUFFIXBE)

    topology.standalone.log.info("\n\n######################### Generate Test data ######################\n")

    # get tmp dir
    mytmp = topology.standalone.getDir(__file__, TMP_DIR)
    if mytmp is None:
        mytmp = "/tmp"

    MYLDIF = '%s%s' % (mytmp, _MYLDIF)
    os.system('ls %s' % MYLDIF)
    os.system('rm -f %s' % MYLDIF)
    if hasattr(topology.standalone, 'prefix'):
        prefix = topology.standalone.prefix
    else:
        prefix = None
    dbgen_prog = prefix + '/bin/dbgen.pl'
    topology.standalone.log.info('dbgen_prog: %s' % dbgen_prog)
    os.system('%s -s %s -o %s -n 14' % (dbgen_prog, MYSUFFIX, MYLDIF))
    cmdline = 'egrep dn: %s | wc -l' % MYLDIF
    p = os.popen(cmdline, "r")
    dnnumstr = p.readline()
    global dnnum
    dnnum = int(dnnumstr)
    topology.standalone.log.info("We have %d entries.\n", dnnum)

    topology.standalone.log.info("\n\n######################### Import Test data ######################\n")

    args = {TASK_WAIT: True}
    importTask = Tasks(topology.standalone)
    importTask.importLDIF(MYSUFFIX, MYSUFFIXBE, MYLDIF, args)

    topology.standalone.log.info("\n\n######################### SEARCH ALL ######################\n")
    topology.standalone.log.info("Bind as %s and add the READ/SEARCH SELFDN aci" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    global entries
    entries = topology.standalone.search_s(MYSUFFIX, ldap.SCOPE_SUBTREE, SEARCHFILTER)
    topology.standalone.log.info("Returned %d entries.\n", len(entries))

    #print entries

    assert dnnum == len(entries)

    topology.standalone.log.info('%d entries are successfully imported.' % dnnum)

def test_ticket48191_run_0(topology):
    topology.standalone.log.info("\n\n######################### SEARCH WITH SIMPLE PAGED RESULTS CONTROL (no nsslapd-maxsimplepaged-per-conn) ######################\n")

    page_size = 4
    spr_req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

    known_ldap_resp_ctrls = {
        SimplePagedResultsControl.controlType: SimplePagedResultsControl,
    }

    topology.standalone.log.info("Calling search_ext...")
    msgid = topology.standalone.search_ext(MYSUFFIX,
                                           ldap.SCOPE_SUBTREE,
                                           SEARCHFILTER,
                                           ['cn'],
                                           serverctrls=[spr_req_ctrl])
    pageddncnt = 0
    pages = 0
    while True:
        pages += 1

        topology.standalone.log.info("Getting page %d" % pages)
        rtype, rdata, rmsgid, responcectrls = topology.standalone.result3(msgid, resp_ctrl_classes=known_ldap_resp_ctrls)
        topology.standalone.log.info("%d results" % len(rdata))
        pageddncnt += len(rdata)

        topology.standalone.log.info("Results:")
        for dn, attrs in rdata:
            topology.standalone.log.info("dn: %s" % dn)

        pctrls = [
            c for c in responcectrls if c.controlType == SimplePagedResultsControl.controlType
        ]
        if not pctrls:
            topology.standalone.log.info('Warning: Server ignores RFC 2696 control.')
            break

        if pctrls[0].cookie:
            spr_req_ctrl.cookie = pctrls[0].cookie
            topology.standalone.log.info("cookie: %s" % spr_req_ctrl.cookie)
            msgid = topology.standalone.search_ext(MYSUFFIX,
                                                   ldap.SCOPE_SUBTREE,
                                                   SEARCHFILTER,
                                                   ['cn'],
                                                   serverctrls=[spr_req_ctrl])
        else:
            topology.standalone.log.info("No cookie")
            break

    topology.standalone.log.info("Paged result search returned %d entries in %d pages.\n", pageddncnt, pages)

    global dnnum
    global entries
    assert dnnum == len(entries)
    assert pages == (dnnum / page_size)

def test_ticket48191_run_1(topology):
    topology.standalone.log.info("\n\n######################### SEARCH WITH SIMPLE PAGED RESULTS CONTROL (nsslapd-maxsimplepaged-per-conn: 0) ######################\n")

    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-maxsimplepaged-per-conn', '0')])

    page_size = 4
    spr_req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

    known_ldap_resp_ctrls = {
        SimplePagedResultsControl.controlType: SimplePagedResultsControl,
    }

    topology.standalone.log.info("Calling search_ext...")
    msgid = topology.standalone.search_ext(MYSUFFIX,
                                           ldap.SCOPE_SUBTREE,
                                           SEARCHFILTER,
                                           ['cn'],
                                           serverctrls=[spr_req_ctrl])

    topology.standalone.log.fatal('Unexpected success')
    try:
        rtype, rdata, rmsgid, responcectrls = topology.standalone.result3(msgid, resp_ctrl_classes=known_ldap_resp_ctrls)
    except ldap.UNWILLING_TO_PERFORM, e:
        topology.standalone.log.info('Returned the expected RC UNWILLING_TO_PERFORM')
        return
    except ldap.LDAPError, e:
        topology.standalone.log.fatal('Unexpected error: ' + e.message['desc'])
        assert False
    topology.standalone.log.info("Type %d" % rtype)
    topology.standalone.log.info("%d results" % len(rdata))
    assert False

def test_ticket48191_run_2(topology):
    topology.standalone.log.info("\n\n######################### SEARCH WITH SIMPLE PAGED RESULTS CONTROL (nsslapd-maxsimplepaged-per-conn: 1000) ######################\n")

    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-maxsimplepaged-per-conn', '1000')])

    page_size = 4
    spr_req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

    known_ldap_resp_ctrls = {
        SimplePagedResultsControl.controlType: SimplePagedResultsControl,
    }

    topology.standalone.log.info("Calling search_ext...")
    msgid = topology.standalone.search_ext(MYSUFFIX,
                                           ldap.SCOPE_SUBTREE,
                                           SEARCHFILTER,
                                           ['cn'],
                                           serverctrls=[spr_req_ctrl])
    pageddncnt = 0
    pages = 0
    while True:
        pages += 1

        topology.standalone.log.info("Getting page %d" % pages)
        rtype, rdata, rmsgid, responcectrls = topology.standalone.result3(msgid, resp_ctrl_classes=known_ldap_resp_ctrls)
        topology.standalone.log.info("%d results" % len(rdata))
        pageddncnt += len(rdata)

        topology.standalone.log.info("Results:")
        for dn, attrs in rdata:
            topology.standalone.log.info("dn: %s" % dn)

        pctrls = [
            c for c in responcectrls if c.controlType == SimplePagedResultsControl.controlType
        ]
        if not pctrls:
            topology.standalone.log.info('Warning: Server ignores RFC 2696 control.')
            break

        if pctrls[0].cookie:
            spr_req_ctrl.cookie = pctrls[0].cookie
            topology.standalone.log.info("cookie: %s" % spr_req_ctrl.cookie)
            msgid = topology.standalone.search_ext(MYSUFFIX,
                                                   ldap.SCOPE_SUBTREE,
                                                   SEARCHFILTER,
                                                   ['cn'],
                                                   serverctrls=[spr_req_ctrl])
        else:
            topology.standalone.log.info("No cookie")
            break

    topology.standalone.log.info("Paged result search returned %d entries in %d pages.\n", pageddncnt, pages)

    global dnnum
    global entries
    assert dnnum == len(entries)
    assert pages == (dnnum / page_size)

    topology.standalone.log.info("ticket48191 was successfully verified.")


def test_ticket48191_final(topology):
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
    test_ticket48191_setup(topo)
    test_ticket48191_run_0(topo)
    test_ticket48191_run_1(topo)
    test_ticket48191_run_2(topo)
    test_ticket48191_final(topo)


if __name__ == '__main__':
    run_isolated()

