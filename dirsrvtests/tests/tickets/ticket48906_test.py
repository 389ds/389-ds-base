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
import shutil

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st
from lib389.utils import *

from lib389._constants import DEFAULT_SUFFIX, DN_LDBM, DN_DM, PASSWORD, SUFFIX

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")]

log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
RDN_VAL_SUFFIX = 'ticket48906.org'
MYSUFFIX = 'dc=%s' % RDN_VAL_SUFFIX
MYSUFFIXBE = 'ticket48906'

SEARCHFILTER = '(objectclass=person)'

OTHER_NAME = 'other_entry'
MAX_OTHERS = 10
DBLOCK_DEFAULT = "10000"
DBLOCK_LDAP_UPDATE = "20000"
DBLOCK_EDIT_UPDATE = "40000"
DBLOCK_MIN_UPDATE = DBLOCK_DEFAULT
DBLOCK_ATTR_CONFIG = "nsslapd-db-locks"
DBLOCK_ATTR_MONITOR = "nsslapd-db-configured-locks"
DBLOCK_ATTR_GUARDIAN = "locks"

DBCACHE_LDAP_UPDATE = "20000000"
DBCACHE_EDIT_UPDATE = "40000000"
DBCACHE_ATTR_CONFIG = "nsslapd-dbcachesize"
DBCACHE_ATTR_GUARDIAN = "cachesize"

ldbm_config = "cn=config,%s" % (DN_LDBM)
bdb_ldbm_config = "cn=bdb,cn=config,%s" % (DN_LDBM)
ldbm_monitor = "cn=database,cn=monitor,%s" % (DN_LDBM)


def test_ticket48906_setup(topology_st):
    """
    Check there is no core
    Create a second backend
    stop DS (that should trigger the core)
    check there is no core
    """
    log.info('Testing Ticket 48906 - ns-slapd crashes during the shutdown after adding attribute with a matching rule')

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # check there is no core
    entry = topology_st.standalone.search_s(CONFIG_DN, ldap.SCOPE_BASE, "(cn=config)", ['nsslapd-workingdir'])
    assert entry
    assert entry[0]
    assert entry[0].hasAttr('nsslapd-workingdir')
    path = entry[0].getValue('nsslapd-workingdir')
    cores = fnmatch.filter(os.listdir(path), b'core.*')
    assert len(cores) == 0

    # add dummy entries on backend
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology_st.standalone.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))

    topology_st.standalone.log.info("\n\n######################### SEARCH ALL ######################\n")
    topology_st.standalone.log.info("Bind as %s and add the READ/SEARCH SELFDN aci" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    entries = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, SEARCHFILTER)
    topology_st.standalone.log.info("Returned %d entries.\n", len(entries))

    assert MAX_OTHERS == len(entries)

    topology_st.standalone.log.info('%d person entries are successfully created under %s.' % (len(entries), SUFFIX))


def _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=None, required=False):
    entries = topology_st.standalone.search_s(bdb_ldbm_config, ldap.SCOPE_BASE, 'cn=bdb')
    if required:
        assert (entries[0].hasValue(attr))
    elif entries[0].hasValue(attr):
        assert (entries[0].getValue(attr) == ensure_bytes(expected_value))


def _check_monitored_value(topology_st, expected_value):
    entries = topology_st.standalone.search_s(ldbm_monitor, ldap.SCOPE_BASE, '(objectclass=*)')
    assert (entries[0].hasValue(DBLOCK_ATTR_MONITOR) and entries[0].getValue(DBLOCK_ATTR_MONITOR) == ensure_bytes(expected_value))


def _check_dse_ldif_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE):
    dse_ref_ldif = topology_st.standalone.confdir + '/dse.ldif'
    dse_ref = open(dse_ref_ldif, "r")

    # Check the DBLOCK in dse.ldif
    value = None
    while True:
        line = dse_ref.readline()
        if (line == ''):
            break
        elif attr in line.lower():
            value = line.split()[1]
            assert (value == expected_value)
            break
    assert (value)


def _check_guardian_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=None):
    guardian_file = os.path.join(topology_st.standalone.dbdir, 'guardian')
    assert (os.path.exists(guardian_file))
    guardian = open(guardian_file, "r")

    value = None
    while True:
        line = guardian.readline()
        if (line == ''):
            break
        elif attr in line.lower():
            value = line.split(':')[1].replace("\n", "")
            print("line")
            print(line)
            print("expected_value")
            print(expected_value)
            print("value")
            print(value)
            assert (str(value) == str(expected_value))
            break
    assert (value)


def test_ticket48906_dblock_default(topology_st):
    topology_st.standalone.log.info('###################################')
    topology_st.standalone.log.info('###')
    topology_st.standalone.log.info('### Check that before any change config/monitor')
    topology_st.standalone.log.info('### contains the default value')
    topology_st.standalone.log.info('###')
    topology_st.standalone.log.info('###################################')
    _check_monitored_value(topology_st, DBLOCK_DEFAULT)
    _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_DEFAULT, required=False)


def test_ticket48906_dblock_ldap_update(topology_st):
    topology_st.standalone.log.info('###################################')
    topology_st.standalone.log.info('###')
    topology_st.standalone.log.info('### Check that after ldap update')
    topology_st.standalone.log.info('###  - monitor contains DEFAULT')
    topology_st.standalone.log.info('###  - configured contains DBLOCK_LDAP_UPDATE')
    topology_st.standalone.log.info('###  - After stop dse.ldif contains DBLOCK_LDAP_UPDATE')
    topology_st.standalone.log.info('###  - After stop guardian contains DEFAULT')
    topology_st.standalone.log.info('###    In fact guardian should differ from config to recreate the env')
    topology_st.standalone.log.info('### Check that after restart (DBenv recreated)')
    topology_st.standalone.log.info('###  - monitor contains DBLOCK_LDAP_UPDATE ')
    topology_st.standalone.log.info('###  - configured contains DBLOCK_LDAP_UPDATE')
    topology_st.standalone.log.info('###  - dse.ldif contains DBLOCK_LDAP_UPDATE')
    topology_st.standalone.log.info('###')
    topology_st.standalone.log.info('###################################')

    topology_st.standalone.modify_s(ldbm_config, [(ldap.MOD_REPLACE, DBLOCK_ATTR_CONFIG, ensure_bytes(DBLOCK_LDAP_UPDATE))])
    _check_monitored_value(topology_st, DBLOCK_DEFAULT)
    _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE, required=True)

    topology_st.standalone.stop(timeout=10)
    _check_dse_ldif_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE)
    _check_guardian_value(topology_st, attr=DBLOCK_ATTR_GUARDIAN, expected_value=DBLOCK_DEFAULT)

    # Check that the value is the same after restart and recreate
    topology_st.standalone.start(timeout=10)
    _check_monitored_value(topology_st, DBLOCK_LDAP_UPDATE)
    _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE, required=True)
    _check_dse_ldif_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE)


def test_ticket48906_dblock_edit_update(topology_st):
    topology_st.standalone.log.info('###################################')
    topology_st.standalone.log.info('###')
    topology_st.standalone.log.info('### Check that after stop')
    topology_st.standalone.log.info('###  - dse.ldif contains DBLOCK_LDAP_UPDATE')
    topology_st.standalone.log.info('###  - guardian contains DBLOCK_LDAP_UPDATE')
    topology_st.standalone.log.info('### Check that edit dse+restart')
    topology_st.standalone.log.info('###  - monitor contains DBLOCK_EDIT_UPDATE')
    topology_st.standalone.log.info('###  - configured contains DBLOCK_EDIT_UPDATE')
    topology_st.standalone.log.info('### Check that after stop')
    topology_st.standalone.log.info('###  - dse.ldif contains DBLOCK_EDIT_UPDATE')
    topology_st.standalone.log.info('###  - guardian contains DBLOCK_EDIT_UPDATE')
    topology_st.standalone.log.info('###')
    topology_st.standalone.log.info('###################################')

    topology_st.standalone.stop(timeout=10)
    _check_dse_ldif_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE)
    _check_guardian_value(topology_st, attr=DBLOCK_ATTR_GUARDIAN, expected_value=DBLOCK_LDAP_UPDATE)

    dse_ref_ldif = topology_st.standalone.confdir + '/dse.ldif'
    dse_new_ldif = topology_st.standalone.confdir + '/dse.ldif.new'
    dse_ref = open(dse_ref_ldif, "r")
    dse_new = open(dse_new_ldif, "w")

    # Change the DBLOCK in dse.ldif
    value = None
    while True:
        line = dse_ref.readline()
        if (line == ''):
            break
        elif DBLOCK_ATTR_CONFIG in line.lower():
            value = line.split()[1]
            assert (value == DBLOCK_LDAP_UPDATE)
            new_value = [line.split()[0], DBLOCK_EDIT_UPDATE, ]
            new_line = "%s\n" % " ".join(new_value)
        else:
            new_line = line
        dse_new.write(new_line)

    assert (value)
    dse_ref.close()
    dse_new.close()
    shutil.move(dse_new_ldif, dse_ref_ldif)

    # Check that the value is the same after restart
    topology_st.standalone.start(timeout=10)
    _check_monitored_value(topology_st, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_EDIT_UPDATE, required=True)

    topology_st.standalone.stop(timeout=10)
    _check_dse_ldif_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_EDIT_UPDATE)
    _check_guardian_value(topology_st, attr=DBLOCK_ATTR_GUARDIAN, expected_value=DBLOCK_EDIT_UPDATE)


def test_ticket48906_dblock_robust(topology_st):
    topology_st.standalone.log.info('###################################')
    topology_st.standalone.log.info('###')
    topology_st.standalone.log.info('### Check that the following values are rejected')
    topology_st.standalone.log.info('###  - negative value')
    topology_st.standalone.log.info('###  - insuffisant value')
    topology_st.standalone.log.info('###  - invalid value')
    topology_st.standalone.log.info('### Check that minimum value is accepted')
    topology_st.standalone.log.info('###')
    topology_st.standalone.log.info('###################################')

    topology_st.standalone.start(timeout=10)
    _check_monitored_value(topology_st, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_EDIT_UPDATE, required=True)

    # Check negative value
    try:
        topology_st.standalone.modify_s(ldbm_config, [(ldap.MOD_REPLACE, DBLOCK_ATTR_CONFIG, b"-1")])
    except ldap.UNWILLING_TO_PERFORM:
        pass
    _check_monitored_value(topology_st, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE, required=True)

    # Check insuffisant value
    too_small = int(DBLOCK_MIN_UPDATE) - 1
    try:
        topology_st.standalone.modify_s(ldbm_config, [(ldap.MOD_REPLACE, DBLOCK_ATTR_CONFIG, ensure_bytes(str(too_small)))])
    except ldap.UNWILLING_TO_PERFORM:
        pass
    _check_monitored_value(topology_st, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE, required=True)

    # Check invalid value
    try:
        topology_st.standalone.modify_s(ldbm_config, [(ldap.MOD_REPLACE, DBLOCK_ATTR_CONFIG, b"dummy")])
    except ldap.UNWILLING_TO_PERFORM:
        pass
    _check_monitored_value(topology_st, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_LDAP_UPDATE, required=True)

    # now check the minimal value
    topology_st.standalone.modify_s(ldbm_config, [(ldap.MOD_REPLACE, DBLOCK_ATTR_CONFIG, ensure_bytes(DBLOCK_MIN_UPDATE))])
    _check_monitored_value(topology_st, DBLOCK_EDIT_UPDATE)
    _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_MIN_UPDATE, required=True)

    topology_st.standalone.stop(timeout=10)
    _check_dse_ldif_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_MIN_UPDATE)
    _check_guardian_value(topology_st, attr=DBLOCK_ATTR_GUARDIAN, expected_value=DBLOCK_EDIT_UPDATE)

    topology_st.standalone.start(timeout=10)
    _check_monitored_value(topology_st, DBLOCK_MIN_UPDATE)
    _check_configured_value(topology_st, attr=DBLOCK_ATTR_CONFIG, expected_value=DBLOCK_MIN_UPDATE, required=True)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
