# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
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
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

ldbm_config = "cn=config,%s" % (DN_LDBM)
txn_begin_flag = "nsslapd-db-transaction-wait"
TEST_USER_DN = 'cn=test,%s' % SUFFIX
TEST_USER = "test"

def _check_configured_value(topology_st, attr=txn_begin_flag, expected_value=None, required=False):
    entries = topology_st.standalone.search_s(ldbm_config, ldap.SCOPE_BASE, 'cn=config')
    if required:
        assert (entries[0].hasValue(attr))
    if entries[0].hasValue(attr):
        topology_st.standalone.log.info('Current value is %s' % entries[0].getValue(attr))
        assert (entries[0].getValue(attr) == ensure_bytes(expected_value))
        
def _update_db(topology_st):
    topology_st.standalone.add_s(
        Entry((TEST_USER_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                              'cn': TEST_USER,
                              'sn': TEST_USER,
                              'givenname': TEST_USER})))
    topology_st.standalone.delete_s(TEST_USER_DN)

def test_ticket49076(topo):
    """Write your testcase here...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).
    """
    
    # check default value is DB_TXN_NOWAIT
    _check_configured_value(topo, expected_value="off")
    
    # tests we are able to update DB
    _update_db(topo)
    
    # switch to wait mode
    topo.standalone.modify_s(ldbm_config,
                                    [(ldap.MOD_REPLACE, txn_begin_flag, b"on")])
                                    # check default value is DB_TXN_NOWAIT
    _check_configured_value(topo, expected_value="on")
    _update_db(topo)
    
    
    # switch back to "normal mode"
    topo.standalone.modify_s(ldbm_config,
                                    [(ldap.MOD_REPLACE, txn_begin_flag, b"off")])
    # check default value is DB_TXN_NOWAIT
    _check_configured_value(topo, expected_value="off")
    # tests we are able to update DB
    _update_db(topo)
    
    # check that settings are not reset by restart
    topo.standalone.modify_s(ldbm_config,
                                    [(ldap.MOD_REPLACE, txn_begin_flag, b"on")])
                                    # check default value is DB_TXN_NOWAIT
    _check_configured_value(topo, expected_value="on")
    _update_db(topo)
    topo.standalone.restart(timeout=10)
    _check_configured_value(topo, expected_value="on")
    _update_db(topo)
    
    # switch default value
    topo.standalone.modify_s(ldbm_config,
                                    [(ldap.MOD_DELETE, txn_begin_flag, None)])
    # check default value is DB_TXN_NOWAIT
    _check_configured_value(topo, expected_value="off")
    # tests we are able to update DB
    _update_db(topo)
    topo.standalone.restart(timeout=10)
    _check_configured_value(topo, expected_value="off")
    # tests we are able to update DB
    _update_db(topo)    
                              

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

