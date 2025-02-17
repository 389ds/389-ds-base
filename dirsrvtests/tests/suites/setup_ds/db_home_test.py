# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest

import os
from lib389.utils import *
from lib389.topologies import topology_st as topo
from lib389._constants import *
from lib389.dseldif import *
from lib389.cli_conf.backend import *
from lib389.config import BDB_LDBMConfig
from .... conftest import get_rpm_version
from lib389.paths import DEFAULTS_PATH

# Check if we are in a container
container_result = subprocess.run(["systemd-detect-virt", "-c"], stdout=subprocess.PIPE)

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(get_default_db_lib() == "mdb", reason='dbhome is meaningless on lmdb'),
              pytest.mark.skipif(get_rpm_version("selinux-policy") <= "3.14.3-79" or
                                 get_rpm_version("selinux-policy") <= "34.1.19-1",
                                 reason="Will fail because of incorrect selinux labels"),
              pytest.mark.skipif(ds_is_older('1.4.3.28'), reason='Not implemented'),
              pytest.mark.skipif(container_result.returncode == 0, reason='db_home_dir is in old location in container')]

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

PREFIX = os.getenv('PREFIX')

def expected_dbhome_value(inst):
    if os.getuid() != 0:
        log.info('expected_dbhome_value: Non root install')
        return inst.dbdir
    if inst.is_in_container():
        log.info('expected_dbhome_value: Container install')
        return inst.db_dir
    if PREFIX is not None:
        log.info('expected_dbhome_value: Prefixed install')
        return inst.db_dir
    log.info('expected_dbhome_value: Standard install')
    return '/dev/shm/slapd-{}'.format(inst.serverid)


def test_check_db_home_dir_in_config(topo):
    """Test to check nsslapd-db-home-directory is set to /dev/shm/slapd-instance in cn=config

    :id: 9a1d0fcf-ca31-4f60-8b31-4de495b0b3ce
    :customerscenario: True
    :setup: Standalone Instance
    :steps:
        1. Create instance
        2. Check nsslapd-db-home-directory is set to /dev/shm/slapd-instance in cn=config
    :expectedresults:
        1. Success
        2. Success
    """

    standalone = topo.standalone
    bdb_ldbmconfig = BDB_LDBMConfig(standalone)
    dbhome_value = expected_dbhome_value(standalone)

    log.info('Check the config value of nsslapd-db-home-directory')
    assert bdb_ldbmconfig.get_attr_val_utf8('nsslapd-db-home-directory') == dbhome_value


def test_check_db_home_dir_contents(topo):
    """Test to check contents of /dev/shm/slapd-instance

    :id: a2d36990-2bb6-46af-99ca-f0cb30e68460
    :customerscenario: True
    :setup: Standalone Instance
    :steps:
        1. Create instance
        2. Check the directory /dev/shm/slapd-instance exists
        3. Check the contents of /dev/shm/slapd-instance/
        4. Check the contents of /dev/shm/slapd-instance/ are not present in var/lib/dirsrv/slapd-instance/db
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    standalone = topo.standalone
    file_list = ['__db.001', '__db.002', '__db.003', 'DBVERSION']
    dbhome_value = expected_dbhome_value(standalone)
    old_dbhome = '/var/lib/dirsrv/slapd-{}/db'.format(standalone.serverid)
    existing_files = list(next(os.walk(dbhome_value))[2])
    try:
        old_location_files = list(next(os.walk(old_dbhome))[2])
    except StopIteration:
        old_location_files = []
    log.info(f'existing_files = {existing_files}')
    log.info(f'old_location_files = {old_location_files}')

    log.info('Check the directory exists')
    assert os.path.exists(dbhome_value)

    log.info('Check the files are present in /dev/shm/slapd-instance/')
    for item in file_list:
        assert item in existing_files

    if dbhome_value != old_dbhome:
        log.info('Check these files are not present in old location')
        for item in file_list:
            assert item not in old_location_files


def test_check_db_home_dir_in_dse(topo):
    """Test to check nsslapd-db-home-directory is set to /dev/shm/slapd-instance in dse.ldif

    :id: f25befd2-a57c-4365-8eaf-70ea5fb987ea
    :customerscenario: True
    :setup: Standalone Instance
    :steps:
        1. Create instance
        2. Check nsslapd-db-home-directory is set to /dev/shm/slapd-instance in dse.ldif
    :expectedresults:
        1. Success
        2. Success
    """

    standalone = topo.standalone
    bdb_ldbmconfig = BDB_LDBMConfig(standalone)
    dbhome_value = expected_dbhome_value(standalone)
    dse_ldif = DSEldif(standalone)

    log.info('Check value of nsslapd-db-home-directory in dse.ldif')
    dse_value = dse_ldif.get(bdb_ldbmconfig.dn, 'nsslapd-db-home-directory', True)
    assert dse_value == dbhome_value


def test_check_db_home_dir_in_defaults(topo):
    """Test to check nsslapd-db-home-directory is set to /dev/shm/slapd-instance in defaults.inf file

    :id: 995ef963-acb1-4210-887e-803fc63e716c
    :customerscenario: True
    :setup: Standalone Instance
    :steps:
        1. Create instance
        2. Check nsslapd-db-home-directory is set to /dev/shm/slapd-instance in defaults.inf file
    :expectedresults:
        1. Success
        2. Success
    """

    standalone = topo.standalone
    def_val = expected_dbhome_value(standalone)
    def_val = def_val.replace(f'slapd-{standalone.serverid}', 'slapd-{instance_name}')
    dbhome_value = f'db_home_dir = {def_val}'

    log.info('Get defaults.inf path')
    def_loc = standalone.ds_paths._get_defaults_loc(DEFAULTS_PATH)

    log.info('Check db_home value is /dev/shm/slapd-{instance_name} in defaults.inf')
    with open(def_loc) as f:
        assert dbhome_value in f.read()


def test_delete_db_home_dir(topo):
    """Test to check behaviour when deleting contents of /dev/shm/slapd-instance/ and restarting the instance

    :id: 07764487-4cb1-438f-a327-bba7d762fea3
    :customerscenario: True
    :setup: Standalone Instance
    :steps:
        1. Create instance
        2. Delete contents of /dev/shm/slapd-instance
        3. Restart instance
        4. Check the contents of /dev/shm/slapd-instance are recreated
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    standalone = topo.standalone
    file_list = ['__db.001', '__db.002', '__db.003', 'DBVERSION']
    dbhome_value = expected_dbhome_value(standalone)
    existing_files = list(next(os.walk(dbhome_value))[2])

    log.info('Stop the instance')
    standalone.stop()

    log.info('Remove contents of /dev/shm/slapd-instance/')
    for f in os.listdir(dbhome_value):
        if f in file_list:
            os.remove(os.path.join(dbhome_value, f))

    if dbhome_value != standalone.dbdir:
        expected_len = 0
        expected_len_when_started = 0
    else:
        # Should have: 'log.0000000001', 'guardian', 'userRoot'
        expected_len = 3
        expected_len_when_started = expected_len -1 # No guardian when started

    log.info('Check there are no unexpected files')
    assert len(os.listdir(dbhome_value)) == expected_len

    log.info('Restart the instance')
    standalone.restart()

    log.info('Check number of files')
    assert len(os.listdir(dbhome_value)) == 4 + expected_len_when_started

    log.info('Check the filenames')
    for item in file_list:
        assert item in existing_files


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
