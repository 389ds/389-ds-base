# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import pytest
import time
from lib389.utils import *
from lib389.dseldif import DSEldif
from lib389.config import BDB_LDBMConfig, LDBMConfig, Config
from lib389.backend import Backends
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389._constants import DEFAULT_SUFFIX, PASSWORD, DN_DM

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

DEBUGGING = os.getenv("DEBUGGING", default=False)
CUSTOM_MEM = '9100100100'
IDLETIMEOUT = 5
DN_TEST_USER = f'uid={TEST_USER_PROPERTIES["uid"]},ou=People,{DEFAULT_SUFFIX}'


@pytest.fixture(scope="module")
def idletimeout_topo(topo, request):
    """Create an instance with a test user and set idletimeout"""
    inst = topo.standalone
    config = Config(inst)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create(properties={
        **TEST_USER_PROPERTIES,
        'userpassword' : PASSWORD,
    })
    config.replace('nsslapd-idletimeout', str(IDLETIMEOUT))

    def fin():
        if not DEBUGGING:
            config.reset('nsslapd-idletimeout')
            user.delete()

    request.addfinalizer(fin)
    return topo


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
    :setup: Standalone Instance
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
    :setup: standalone
    :steps:
        1. Check that bdb config instance exists.
    :expectedresults:
        1. Success
    """

    inst = topo.standalone
    assert BDB_LDBMConfig(inst).exists()


@pytest.mark.parametrize("dn,expected_result", [(DN_TEST_USER, True), (DN_DM, False)])
def test_idletimeout(idletimeout_topo, dn, expected_result):
    """Check that bdb config entry exists

    :id: b20f2826-942a-11ee-827b-482ae39447e5
    :parametrized: yes
    :setup: Standalone Instance with test user and idletimeout
    :steps:
        1. Open new ldap connection
        2. Bind with the provided dn
        3. Wait longer than idletimeout
        4. Try to bind again the provided dn and check if
           connection is closed or not.
        5. Check if result is the expected one.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    inst = idletimeout_topo.standalone

    l = ldap.initialize(f'ldap://localhost:{inst.port}')
    l.bind_s(dn, PASSWORD)
    time.sleep(IDLETIMEOUT+1)
    try:
        l.bind_s(dn, PASSWORD)
        result = False
    except ldap.SERVER_DOWN:
        result = True
    assert expected_result == result
