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
import shutil
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from ldap.controls import SimplePagedResultsControl
from ldap.controls.simple import GetEffectiveRightsControl
import fnmatch

log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
RDN_VAL_SUFFIX = 'ticket48906.org'
MYSUFFIX = 'dc=%s' % RDN_VAL_SUFFIX
MYSUFFIXBE = 'ticket48906'

SEARCHFILTER = '(objectclass=person)'

OTHER_NAME = 'other_entry'
MAX_OTHERS = 10
DBLOCK_DEFAULT="10000"
DBLOCK_LDAP_UPDATE="20000"
DBLOCK_EDIT_UPDATE="40000"
DBLOCK_MIN_UPDATE=DBLOCK_DEFAULT
DBLOCK_ATTR_CONFIG="nsslapd-db-locks"
DBLOCK_ATTR_MONITOR="nsslapd-db-configured-locks"
DBLOCK_ATTR_GUARDIAN="locks"

DBCACHE_DEFAULT="10000000"
DBCACHE_LDAP_UPDATE="20000000"
DBCACHE_EDIT_UPDATE="40000000"
DBCACHE_ATTR_CONFIG="nsslapd-dbcachesize"
DBCACHE_ATTR_GUARDIAN="cachesize"

ldbm_config = "cn=config,%s" % (DN_LDBM)
ldbm_monitor = "cn=database,cn=monitor,%s" % (DN_LDBM)

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
    '''
    standalone = DirSrv(verbose=True)

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


def test_ticket48906_setup(topology):
    """
    Check there is no core
    Create a second backend
    stop DS (that should trigger the core)
    check there is no core
    """
    log.info('Testing Ticket 48906 - ns-slapd crashes during the shutdown after adding attribute with a matching rule')

    # bind as directory manager
    topology.standalone.log.info("Bind as %s" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    # check there is no core
    entry = topology.standalone.search_s(CONFIG_DN, ldap.SCOPE_BASE, "(cn=config)",['nsslapd-workingdir'])
    assert entry
    assert entry[0]
    assert entry[0].hasAttr('nsslapd-workingdir')
    path = entry[0].getValue('nsslapd-workingdir')
    cores = fnmatch.filter(os.listdir(path), 'core.*')
    assert len(cores) == 0


    # add dummy entries on backend
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology.standalone.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
                                            'objectclass': "top person".split(),
                                            'sn': name,
                                            'cn': name})))

    topology.standalone.log.info("\n\n######################### SEARCH ALL ######################\n")
    topology.standalone.log.info("Bind as %s and add the READ/SEARCH SELFDN aci" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    entries = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, SEARCHFILTER)
    topology.standalone.log.info("Returned %d entries.\n", len(entries))

    assert MAX_OTHERS == len(entries)

    topology.standalone.log.info('%d person entries are successfully created under %s.' % (len(entries), SUFFIX))

def _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG, expected_value=None, required=False):
    entries = topology.standalone.search_s(ldbm_config, ldap.SCOPE_BASE, 'cn=config')
    if required:
            assert(entries[0].hasValue(attr))
    elif entries[0].hasValue(attr):
            assert(entries[0].getValue(attr) == expected_value)

def _check_monitored_value(topology, expected_value):
    entries = topology.standalone.search_s(ldbm_monitor, ldap.SCOPE_BASE, '(objectclass=*)')
    assert(entries[0].hasValue(DBLOCK_ATTR_MONITOR) and entries[0].getValue(DBLOCK_ATTR_MONITOR) == expected_value)

def _check_dse_ldif_value(topology, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE):
    dse_ref_ldif = topology.standalone.confdir + '/dse.ldif'
    dse_ref = open(dse_ref_ldif, "r")

    # Check the DBLOCK in dse.ldif
    value=None
    while True:
        line = dse_ref.readline()
        if (line == ''):
            break
        elif attr in line.lower():
            value = line.split()[1]
            assert(value == expected_value)
            break
    assert(value)

def _check_guardian_value(topology, attr=DBLOCK_ATTR_CONFIG, expected_value=None):
    guardian_file = topology.standalone.dbdir + '/db/guardian'
    assert(os.path.exists(guardian_file))
    guardian = open(guardian_file, "r")

    value=None
    while True:
        line = guardian.readline()
        if (line == ''):
            break
        elif attr in line.lower():
            value = line.split(':')[1].replace("\n", "")
            print "line"
            print line
            print "expected_value"
            print expected_value
            print "value"
            print value
            assert(str(value) == str(expected_value))
            break
    assert(value)

def test_ticket48906_dblock_default(topology):
    topology.standalone.log.info('###################################')
    topology.standalone.log.info('###')
    topology.standalone.log.info('### Check that before any change config/monitor')
    topology.standalone.log.info('### contains the default value')
    topology.standalone.log.info('###')
    topology.standalone.log.info('###################################')
    _check_monitored_value(topology, DBLOCK_DEFAULT)
    _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_DEFAULT, required=False)
    _check_configured_value(topology, attr=DBCACHE_ATTR_CONFIG, expected_value=DBCACHE_DEFAULT, required=False)


def test_ticket48906_dblock_ldap_update(topology):
    topology.standalone.log.info('###################################')
    topology.standalone.log.info('###')
    topology.standalone.log.info('### Check that after ldap update')
    topology.standalone.log.info('###  - monitor contains DEFAULT')
    topology.standalone.log.info('###  - configured contains DBLOCK_LDAP_UPDATE')
    topology.standalone.log.info('###  - After stop dse.ldif contains DBLOCK_LDAP_UPDATE')
    topology.standalone.log.info('###  - After stop guardian contains DEFAULT')
    topology.standalone.log.info('###    In fact guardian should differ from config to recreate the env')
    topology.standalone.log.info('### Check that after restart (DBenv recreated)')
    topology.standalone.log.info('###  - monitor contains DBLOCK_LDAP_UPDATE ')
    topology.standalone.log.info('###  - configured contains DBLOCK_LDAP_UPDATE')
    topology.standalone.log.info('###  - dse.ldif contains DBLOCK_LDAP_UPDATE')
    topology.standalone.log.info('###')
    topology.standalone.log.info('###################################')

    topology.standalone.modify_s(ldbm_config, [(ldap.MOD_REPLACE, DBLOCK_ATTR_CONFIG, DBLOCK_LDAP_UPDATE)])
    _check_monitored_value(topology, DBLOCK_DEFAULT)
    _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG,  expected_value=DBLOCK_LDAP_UPDATE, required=True)

    topology.standalone.stop(timeout=10)
    _check_dse_ldif_value(topology, attr=DBLOCK_ATTR_CONFIG,    expected_value=DBLOCK_LDAP_UPDATE)
    _check_guardian_value(topology, attr=DBLOCK_ATTR_GUARDIAN,  expected_value=DBLOCK_DEFAULT)

    # Check that the value is the same after restart and recreate
    topology.standalone.start(timeout=10)
    _check_monitored_value(topology, DBLOCK_LDAP_UPDATE)
    _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG,  expected_value=DBLOCK_LDAP_UPDATE, required=True)
    _check_dse_ldif_value(topology, attr=DBLOCK_ATTR_CONFIG,    expected_value=DBLOCK_LDAP_UPDATE)

def test_ticket48906_dblock_edit_update(topology):
    topology.standalone.log.info('###################################')
    topology.standalone.log.info('###')
    topology.standalone.log.info('### Check that after stop')
    topology.standalone.log.info('###  - dse.ldif contains DBLOCK_LDAP_UPDATE')
    topology.standalone.log.info('###  - guardian contains DBLOCK_LDAP_UPDATE')
    topology.standalone.log.info('### Check that edit dse+restart')
    topology.standalone.log.info('###  - monitor contains DBLOCK_EDIT_UPDATE')
    topology.standalone.log.info('###  - configured contains DBLOCK_EDIT_UPDATE')
    topology.standalone.log.info('### Check that after stop')
    topology.standalone.log.info('###  - dse.ldif contains DBLOCK_EDIT_UPDATE')
    topology.standalone.log.info('###  - guardian contains DBLOCK_EDIT_UPDATE')
    topology.standalone.log.info('###')
    topology.standalone.log.info('###################################')

    topology.standalone.stop(timeout=10)
    _check_dse_ldif_value(topology, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE)
    _check_guardian_value(topology, attr=DBLOCK_ATTR_GUARDIAN,  expected_value=DBLOCK_LDAP_UPDATE)

    dse_ref_ldif = topology.standalone.confdir + '/dse.ldif'
    dse_new_ldif = topology.standalone.confdir + '/dse.ldif.new'
    dse_ref = open(dse_ref_ldif, "r")
    dse_new = open(dse_new_ldif, "w")

    # Change the DBLOCK in dse.ldif
    value=None
    while True:
        line = dse_ref.readline()
        if (line == ''):
            break
        elif DBLOCK_ATTR_CONFIG in line.lower():
            value = line.split()[1]
            assert(value == DBLOCK_LDAP_UPDATE)
            new_value = [line.split()[0], DBLOCK_EDIT_UPDATE, ]
            new_line = "%s\n" % " ".join(new_value)
        else:
            new_line = line
        dse_new.write(new_line)

    assert(value)
    dse_ref.close()
    dse_new.close()
    shutil.move(dse_new_ldif, dse_ref_ldif)

    # Check that the value is the same after restart
    topology.standalone.start(timeout=10)
    _check_monitored_value(topology, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_EDIT_UPDATE, required=True)

    topology.standalone.stop(timeout=10)
    _check_dse_ldif_value(topology, attr=DBLOCK_ATTR_CONFIG,   expected_value=DBLOCK_EDIT_UPDATE)
    _check_guardian_value(topology, attr=DBLOCK_ATTR_GUARDIAN, expected_value=DBLOCK_EDIT_UPDATE)

def test_ticket48906_dblock_robust(topology):
    topology.standalone.log.info('###################################')
    topology.standalone.log.info('###')
    topology.standalone.log.info('### Check that the following values are rejected')
    topology.standalone.log.info('###  - negative value')
    topology.standalone.log.info('###  - insuffisant value')
    topology.standalone.log.info('###  - invalid value')
    topology.standalone.log.info('### Check that minimum value is accepted')
    topology.standalone.log.info('###')
    topology.standalone.log.info('###################################')

    topology.standalone.start(timeout=10)
    _check_monitored_value(topology, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_EDIT_UPDATE, required=True)

    # Check negative value
    try:
        topology.standalone.modify_s(ldbm_config, [(ldap.MOD_REPLACE, DBLOCK_ATTR_CONFIG, "-1")])
    except ldap.UNWILLING_TO_PERFORM:
        pass
    _check_monitored_value(topology, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG,  expected_value=DBLOCK_LDAP_UPDATE, required=True)

    # Check insuffisant value
    too_small = int(DBLOCK_MIN_UPDATE) - 1
    try:
        topology.standalone.modify_s(ldbm_config, [(ldap.MOD_REPLACE, DBLOCK_ATTR_CONFIG, str(too_small))])
    except ldap.UNWILLING_TO_PERFORM:
        pass
    _check_monitored_value(topology, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG,  expected_value=DBLOCK_LDAP_UPDATE, required=True)

    # Check invalid value
    try:
        topology.standalone.modify_s(ldbm_config, [(ldap.MOD_REPLACE, DBLOCK_ATTR_CONFIG, "dummy")])
    except ldap.UNWILLING_TO_PERFORM:
        pass
    _check_monitored_value(topology, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG,  expected_value=DBLOCK_LDAP_UPDATE, required=True)

    #now check the minimal value
    topology.standalone.modify_s(ldbm_config, [(ldap.MOD_REPLACE, DBLOCK_ATTR_CONFIG, DBLOCK_MIN_UPDATE)])
    _check_monitored_value(topology, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG,  expected_value=DBLOCK_MIN_UPDATE, required=True)

    topology.standalone.stop(timeout=10)
    _check_dse_ldif_value(topology, attr=DBLOCK_ATTR_CONFIG,    expected_value=DBLOCK_MIN_UPDATE)
    _check_guardian_value(topology, attr=DBLOCK_ATTR_GUARDIAN,  expected_value=DBLOCK_EDIT_UPDATE)

    topology.standalone.start(timeout=10)
    _check_monitored_value(topology, DBLOCK_MIN_UPDATE)
    _check_configured_value(topology, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_MIN_UPDATE, required=True)

def text_ticket48906_final(topology):
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
    topo = topology(True)
    test_ticket48906_setup(topo)
    test_ticket48906_dblock_default(topo)
    test_ticket48906_dblock_ldap_update(topo)
    test_ticket48906_dblock_edit_update(topo)
    test_ticket48906_dblock_robust(topo)
    test_ticket48906_final(topo)


if __name__ == '__main__':
    run_isolated()


