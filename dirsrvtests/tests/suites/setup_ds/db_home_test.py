# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest

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


@pytest.mark.ds2790
@pytest.mark.bz1780842
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

    if standalone.is_in_container():
        dbhome_value = standalone.db_dir
    else:
        dbhome_value = '/dev/shm/slapd-{}'.format(standalone.serverid)
    bdb_ldbmconfig = BDB_LDBMConfig(standalone)

    log.info('Check the config value of nsslapd-db-home-directory')
    assert bdb_ldbmconfig.get_attr_val_utf8('nsslapd-db-home-directory') == dbhome_value


@pytest.mark.ds2790
@pytest.mark.bz1780842
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
    if standalone.is_in_container():
        dbhome_value = standalone.db_dir
    else:
        dbhome_value = '/dev/shm/slapd-{}/'.format(standalone.serverid)
    old_dbhome = '/var/lib/dirsrv/slapd-{}/db'.format(standalone.serverid)
    existing_files = list(next(os.walk(dbhome_value))[2])
    old_location_files = list(next(os.walk(old_dbhome))[2])

    log.info('Check the directory exists')
    assert os.path.exists(dbhome_value)

    log.info('Check the files are present in /dev/shm/slapd-instance/')
    for item in file_list:
        assert item in existing_files

    log.info('Check these files are not present in old location')
    for item in file_list:
        assert item not in old_location_files


@pytest.mark.ds2790
@pytest.mark.bz1780842
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
    if standalone.is_in_container():
        dbhome_value = standalone.db_dir
    else:
        dbhome_value = '/dev/shm/slapd-{}'.format(standalone.serverid)
    dse_ldif = DSEldif(standalone)

    log.info('Check value of nsslapd-db-home-directory in dse.ldif')
    dse_value = dse_ldif.get(bdb_ldbmconfig.dn, 'nsslapd-db-home-directory', True)
    assert dse_value == dbhome_value


@pytest.mark.ds2790
@pytest.mark.bz1780842
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
    if standalone.is_in_container():
        dbhome_value = 'db_home_dir = ' + standalone.db_dir
    else:
        dbhome_value = 'db_home_dir = /dev/shm/slapd-{instance_name}'

    log.info('Get defaults.inf path')
    def_loc = standalone.ds_paths._get_defaults_loc(DEFAULTS_PATH)

    log.info('Check db_home value is /dev/shm/slapd-{instance_name} in defaults.inf')
    with open(def_loc) as f:
        assert dbhome_value in f.read()


@pytest.mark.ds2790
@pytest.mark.bz1780842
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
    if standalone.is_in_container():
        dbhome_value = standalone.db_dir
    else:
        dbhome_value = '/dev/shm/slapd-{}/'.format(standalone.serverid)
    existing_files = list(next(os.walk(dbhome_value))[2])

    log.info('Stop the instance')
    standalone.stop()

    log.info('Remove contents of /dev/shm/slapd-instance/')
    for f in os.listdir(dbhome_value):
        os.remove(os.path.join(dbhome_value, f))

    log.info('Check there are no files')
    assert len(os.listdir(dbhome_value)) == 0

    log.info('Restart the instance')
    standalone.restart()

    log.info('Check number of files')
    assert len(os.listdir(dbhome_value)) == 4

    log.info('Check the filenames')
    for item in file_list:
        assert item in existing_files


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)