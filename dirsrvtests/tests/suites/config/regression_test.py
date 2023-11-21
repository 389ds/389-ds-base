# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
from lib389.utils import *
from lib389.dseldif import DSEldif
from lib389.config import BDB_LDBMConfig, LDBMConfig
from lib389.backend import Backends
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

CUSTOM_MEM = '9100100100'


# Function to return value of available memory in kb
def get_available_memory():
    with open('/proc/meminfo') as file:
        for line in file:
            if 'MemAvailable' in line:
                free_mem_in_kb = line.split()[1]
    return int(free_mem_in_kb)


@pytest.mark.skipif(get_available_memory() < (int(CUSTOM_MEM)/1024), reason="available memory is too low")
@pytest.mark.bz1627512
@pytest.mark.ds49618
def test_set_cachememsize_to_custom_value(topo):
    """Test if value nsslapd-cachememsize remains set
     at the custom setting of value above 3805132804 bytes
     after changing the value to 9100100100 bytes

    :id: 8a3efc00-65a9-4ee7-b8ee-e35840991ea9
    :setup: Standalone Instance
    :steps:
        1. Disable in the cn=config,cn=ldbm database,cn=plugins,cn=config:
           nsslapd-cache-autosize by setting it to 0
        2. Disable in the cn=config,cn=ldbm database,cn=plugins,cn=config:
           nsslapd-cache-autosize-split by setting it to 0
        3. Restart the instance
        4. Set in the cn=UserRoot,cn=ldbm database,cn=plugins,cn=config:
           nsslapd-cachememsize: CUSTOM_MEM
    :expectedresults:
        1. nsslapd-cache-autosize is successfully disabled
        2. nsslapd-cache-autosize-split is successfully disabled
        3. The instance should be successfully restarted
        4. nsslapd-cachememsize is successfully set
    """

    config_ldbm = LDBMConfig(topo.standalone)
    backends = Backends(topo.standalone)
    userroot_ldbm = backends.get("userroot")

    log.info("Disabling nsslapd-cache-autosize by setting it to 0")
    assert config_ldbm.set('nsslapd-cache-autosize', '0')

    log.info("Disabling nsslapd-cache-autosize-split by setting it to 0")
    assert config_ldbm.set('nsslapd-cache-autosize-split', '0')

    log.info("Restarting instance")
    topo.standalone.restart()
    log.info("Instance restarted successfully")

    log.info("Set nsslapd-cachememsize to value {}".format(CUSTOM_MEM))
    assert userroot_ldbm.set('nsslapd-cachememsize', CUSTOM_MEM)


def test_maxbersize_repl(topo):
    """Check that instance starts when nsslapd-errorlog-maxlogsize
    nsslapd-errorlog-logmaxdiskspace are set in certain order

    :id: 743e912c-2be4-4f5f-9c2a-93dcb18f51a0
    :setup: MMR with two suppliers
    :steps:
        1. Stop the instance
        2. Set nsslapd-errorlog-maxlogsize before/after
           nsslapd-errorlog-logmaxdiskspace
        3. Start the instance
        4. Check the error log for errors
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. The error log should contain no errors
    """

    inst = topo.standalone
    dse_ldif = DSEldif(inst)

    inst.stop()
    log.info("Set nsslapd-errorlog-maxlogsize before nsslapd-errorlog-logmaxdiskspace")
    dse_ldif.replace('cn=config', 'nsslapd-errorlog-maxlogsize', '300')
    dse_ldif.replace('cn=config', 'nsslapd-errorlog-logmaxdiskspace', '500')
    inst.start()
    log.info("Assert no init_dse_file errors in the error log")
    assert not inst.ds_error_log.match('.*ERR - init_dse_file.*')

    inst.stop()
    log.info("Set nsslapd-errorlog-maxlogsize after nsslapd-errorlog-logmaxdiskspace")
    dse_ldif.replace('cn=config', 'nsslapd-errorlog-logmaxdiskspace', '500')
    dse_ldif.replace('cn=config', 'nsslapd-errorlog-maxlogsize', '300')
    inst.start()
    log.info("Assert no init_dse_file errors in the error log")
    assert not inst.ds_error_log.match('.*ERR - init_dse_file.*')


def test_bdb_config(topo):
    """Check that bdb config entry exists

    :id: edbc6f54-7c98-11ee-b1c0-482ae39447e5
    :setup: MMR with two suppliers
    :steps:
        1. Check that bdb config instance exists.
    :expectedresults:
        1. Success
    """

    inst = topo.standalone
    assert BDB_LDBMConfig(inst).exists()
