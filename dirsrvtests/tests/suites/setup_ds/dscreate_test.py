# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
import sys
import pytest
import subprocess
import logging
import grp
import pwd
import re
from tempfile import TemporaryDirectory
from lib389 import DirSrv
from lib389.cli_base import LogCapture
from lib389.instance.setup import SetupDs
from lib389.instance.remove import remove_ds_instance
from lib389.instance.options import General2Base, Slapd2Base
from lib389._constants import *
from lib389.utils import ds_is_older, selinux_label_file, ensure_list_str, ensure_str
from shutil import rmtree

pytestmark = [pytest.mark.tier0,
              pytest.mark.skipif(ds_is_older('1.4.1.2'), reason="Needs a compatible systemd unit, see PR#50213")]

DEBUGGING = os.getenv('DEBUGGING', False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

INSTANCE_PORT = 54321
INSTANCE_SECURE_PORT = 54322
INSTANCE_SERVERID = 'standalone'
#DEBUGGING = True

MAJOR, MINOR, _, _, _ = sys.version_info

CUSTOM_DIR = f'{os.getenv("PREFIX", "")}/var/lib/dirsrv_pytest_test_setup_ds_custom_db_dir'
CUSTOM_DB_DIR = f'{CUSTOM_DIR}/db'

class TopologyInstance(object):
    def __init__(self, standalone):
        # For these tests, we don't want to open the instance.
        # instance.open()
        self.standalone = standalone

# Need a teardown to destroy the instance.
@pytest.fixture
def topology(request):
    instance = DirSrv(verbose=DEBUGGING)
    instance.log.debug("Instance allocated")
    args = {SER_PORT: INSTANCE_PORT,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    instance.allocate(args)
    if instance.exists():
        instance.delete()
    # Cleanup custom dir
    selinux_label_file(CUSTOM_DB_DIR, None)
    rmtree(CUSTOM_DIR, ignore_errors=True)

    def fin():
        if not DEBUGGING:
            if instance.exists():
                instance.delete()
            selinux_label_file(CUSTOM_DB_DIR, None)
            rmtree(CUSTOM_DIR, ignore_errors=True)
    request.addfinalizer(fin)

    return TopologyInstance(instance)


def run_cmd(cmd):
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    args = ' '.join(ensure_list_str(result.args))
    stdout = ensure_str(result.stdout)
    stderr = ensure_str(result.stderr)
    log.info(f"CMD: {args} returned {result.returncode} STDOUT: {stdout} STDERR: {stderr}")
    return stdout


def test_setup_ds_minimal_dry(topology):
    """Test minimal DS setup - dry run

    :id: 82637910-e279-11ec-a785-3497f624ea11
    :setup: standalone instance
    :steps:
        1. Create the setupDS
        2. Give it the right types
        3. Get the dicts from Type2Base, as though they were from _validate_ds_2_config
        4. Override instance name, root password, port and secure port
        5. Assert we did not change the system
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    # Unset PYTHONPATH to avoid mixing old CLI tools and new lib389
    tmp_env = os.environ
    if "PYTHONPATH" in tmp_env:
        del tmp_env["PYTHONPATH"]

    # Create the setupDs
    lc = LogCapture()
    # Give it the right types.
    sds = SetupDs(verbose=DEBUGGING, dryrun=True, log=lc.log)

    # Get the dicts from Type2Base, as though they were from _validate_ds_2_config
    # IE get the defaults back just from Slapd2Base.collect
    # Override instance name, root password, port and secure port.

    general_options = General2Base(lc.log)
    general_options.verify()
    general = general_options.collect()

    slapd_options = Slapd2Base(lc.log)
    slapd_options.set('instance_name', INSTANCE_SERVERID)
    slapd_options.set('port', INSTANCE_PORT)
    slapd_options.set('secure_port', INSTANCE_SECURE_PORT)
    slapd_options.set('root_password', PW_DM)
    slapd_options.verify()
    slapd = slapd_options.collect()

    sds.create_from_args(general, slapd, {}, None)

    insts = topology.standalone.list(serverid=INSTANCE_SERVERID)
    # Assert we did not change the system.
    assert(len(insts) == 0)

def test_setup_ds_minimal(topology):
    """Test minimal DS setup

    :id: 563c3ec4-e27b-11ec-970e-3497f624ea11
    :setup: standalone instance
    :steps:
        1. Create the setupDS
        2. Give it the right types
        3. Get the dicts from Type2Base, as though they were from _validate_ds_2_config
        4. Override instance name, root password, port and secure port
        5. Assert we did change the system
        6. Make sure we can connect
        7. Make sure we can start stop.
        8. Remove the instance
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """
    # Create the setupDs
    lc = LogCapture()
    # Give it the right types.
    sds = SetupDs(verbose=DEBUGGING, dryrun=False, log=lc.log)

    # Get the dicts from Type2Base, as though they were from _validate_ds_2_config
    # IE get the defaults back just from Slapd2Base.collect
    # Override instance name, root password, port and secure port.

    general_options = General2Base(lc.log)
    general_options.verify()
    general = general_options.collect()

    slapd_options = Slapd2Base(lc.log)
    slapd_options.set('instance_name', INSTANCE_SERVERID)
    slapd_options.set('port', INSTANCE_PORT)
    slapd_options.set('secure_port', INSTANCE_SECURE_PORT)
    slapd_options.set('root_password', PW_DM)
    slapd_options.verify()
    slapd = slapd_options.collect()

    sds.create_from_args(general, slapd, {}, None)
    insts = topology.standalone.list(serverid=INSTANCE_SERVERID)
    # Assert we did change the system.
    assert(len(insts) == 1)
    # Make sure we can connect
    topology.standalone.open()
    # Make sure we can start stop.
    topology.standalone.stop()
    topology.standalone.start()
    # Okay, actually remove the instance
    remove_ds_instance(topology.standalone)


@pytest.mark.skipif(not os.path.exists('/usr/sbin/semanage'), reason="semanage is not installed. Please run dnf install policycoreutils-python-utils -y")
@pytest.mark.skipif(os.getuid()!=0, reason="pytest non run by root user")
def test_setup_ds_custom_db_dir(topology):
    """Test DS setup using custom uid,gid and db_dir path

    :id: 5a596887-cabb-4862-a91c-5eedafe222cd
    :setup: standalone instance
    :steps:
        1. Create the user that will run ns-slapd
        2. Create the setupDS
        3. Give it the right types
        4. Get the dicts from Type2Base, as though they were from _validate_ds_2_config
        5. Override instance name, root password, port, secure port, user, group and dir_path
        6. Assert we did change the system
        7. Make sure we can connect
        8. Make sure we can start stop.
        9. Remove the instance
        10. Check that there is not any dirsrv_* labels in in file local selinux customizations
        11. Check that there is not any wldap_port_t labels in port local selinux customizations
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
    """
  # Add linux user NON_ROOT_USER if it does not already exist
    CUSTOM_USER='ldapsrv1'
    try:
        pwd_cu = pwd.getpwnam(CUSTOM_USER)
    except KeyError:
        subprocess.run(('/usr/sbin/useradd', CUSTOM_USER), check=True)
        pwd_cu = pwd.getpwnam(CUSTOM_USER)
    grp_cu = grp.getgrgid(pwd_cu.pw_gid)
    log.info(f'Custom user: {pwd_cu} {grp_cu}')

    # Create the setupDs
    lc = LogCapture()
    # Give it the right types.
    sds = SetupDs(verbose=DEBUGGING, dryrun=False, log=lc.log)

    # Get the dicts from Type2Base, as though they were from _validate_ds_2_config
    # IE get the defaults back just from Slapd2Base.collect
    # Override instance name, root password, port, secure port, user,  group and db_dir.

    general_options = General2Base(lc.log)
    general_options.verify()
    general = general_options.collect()

    slapd_options = Slapd2Base(lc.log)
    slapd_options.set('instance_name', INSTANCE_SERVERID)
    slapd_options.set('port', INSTANCE_PORT)
    slapd_options.set('secure_port', INSTANCE_SECURE_PORT)
    slapd_options.set('root_password', PW_DM)
    slapd_options.set('user', pwd_cu.pw_name)
    slapd_options.set('group', grp_cu.gr_name)
    slapd_options.set('db_dir', CUSTOM_DB_DIR)
    slapd_options.verify()
    slapd = slapd_options.collect()

    sds.create_from_args(general, slapd, {}, None)
    insts = topology.standalone.list(serverid=INSTANCE_SERVERID)
    # Assert we did change the system.
    assert(len(insts) == 1)
    # Make sure we can connect
    topology.standalone.open()
    # Make sure we can start stop.
    topology.standalone.stop()
    topology.standalone.start()
    # Okay, actually remove the instance
    insts = topology.standalone.list(all=True)
    remove_ds_instance(topology.standalone)
    if (len(insts) == 1):
        res = run_cmd(["semanage", "fcontext", "--list", "-C"])
        assert not "dirsrv_" in res
        res = run_cmd(["semanage", "port", "--list", "-C"])
        assert not "ldap_port_t" in res

