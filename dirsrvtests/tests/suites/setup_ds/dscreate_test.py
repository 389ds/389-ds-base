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
import pwd
import re
from tempfile import TemporaryDirectory
from lib389 import DirSrv
from lib389.cli_base import LogCapture
from lib389.instance.setup import SetupDs
from lib389.instance.remove import remove_ds_instance
from lib389.instance.options import General2Base, Slapd2Base
from lib389._constants import *
from lib389.utils import ds_is_older

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

    def fin():
        if instance.exists() and not DEBUGGING:
            instance.delete()
    request.addfinalizer(fin)

    return TopologyInstance(instance)

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

def write_file(fname, pwd, is_runnable, lines):
    log.debug(f'Creating file {fname} with:')
    with open(fname, 'wt') as f:
        for line in lines:
            log.debug(line)
            f.write(line+'\n')
        os.chown(fname, pwd.pw_uid, pwd.pw_gid)
        if (is_runnable):
            os.chmod(fname, 0o755)
    log.debug(f'End of file: {fname}')

def test_setup_ds_as_non_root():
    """Test creating an instance as a non root user

    :id: c727998e-a960-11ec-898e-482ae39447e5
    :setup: no instance
    :steps:
        1. Add linux user NON_ROOT_USER if it does not already exist
        2. Create a temporary directory
        3. Create test.sh script that
             Set Paths
             Run dscreate ds-root
             Run dscreate from-file
             Add a backend
             Search users in backend and store output in a file
             Stop the instance
        4. Create a dscreate template file
        5. Run su - NON_ROOT_USER test.sh
        6. Check that pid file exists and kill the associated process
        7. Check demo_user is in the search result
        8. Check that test.sh returned 0
        9. (Implicit task): remove the temporary


    :expectedresults:
        1. No error.
        2. No error.
        3. No error.
        4. No error.
        5. No error.
        6. Should fail to kill the process (That is supposed to be stopped)
        7. demo_user should be in search result
        8. return code should be 0
        9. No error.

    """

    # Add linux user NON_ROOT_USER if it does not already exist
    NON_ROOT_USER='user1'
    try:
        pwd_nru = pwd.getpwnam(NON_ROOT_USER)
    except KeyError:
        subprocess.run(('/usr/sbin/useradd', NON_ROOT_USER))
        pwd_nru = pwd.getpwnam(NON_ROOT_USER)

    # Create a temporary directory
    with TemporaryDirectory() as dir:
        os.chown(dir, pwd_nru.pw_uid, pwd_nru.pw_gid)
        # Prepare test script
        if DEBUGGING:
            dbg_script = (
                'savedir()',
                '{',
                f'    cp -R {dir} /tmp/dbg_test_setup_ds_as_non_root',
                '}',
                'trap savedir exit',
                'trap savedir err',
            )
        else:
            dbg_script = ( )
        # Export current path and python path in test script
        # to insure that right binary/libraries are used
        path = os.environ['PATH']
        pythonpath = os.getenv('PYTHONPATH', '')
        write_file(f'{dir}/test.sh', pwd_nru, True, (
            '#!/usr/bin/bash',
            'set -x',
            *dbg_script,
            f'export PATH="{dir}/bin:{path}:$PATH"',
            f'export PYTHONPATH="{pythonpath}:$PYTHONPATH"',
            'set -e # Exit on error',
            'type dscreate',
            f'dscreate ds-root {dir}/root {dir}/bin',
            'hash -d dscreate # Remove dscreate from hash to use the new one',
            'type dscreate',
            f'dscreate from-file {dir}/t',
            f'dsconf {INSTANCE_SERVERID} backend create --suffix dc=foo,dc=bar --be-name=foo --create-entries',
            f'ldapsearch -x -H ldap://localhost:{INSTANCE_PORT} -D "cn=directory manager" -w {PW_DM} -b dc=foo,dc=bar "uid=*" | tee {dir}/search.out',
            f'dsctl {INSTANCE_SERVERID} stop',
            'exit 0',
        ))
        # Prepare dscreate template
        write_file(f'{dir}/t', pwd_nru, False, (
            '[general]',
            '[slapd]',
            f'port = {INSTANCE_PORT}',
            f'instance_name = {INSTANCE_SERVERID}',
            f'root_password = {PW_DM}',
            f'secure_port = {INSTANCE_SECURE_PORT}',
            '[backend-userroot]',
            'create_suffix_entry = True',
            'require_index = True',
            'sample_entries = yes',
            'suffix = dc=example,dc=com',
        ))
        # Run the script as NON_ROOT_USER
        log.debug(f'Run script {dir}/test.sh as user {NON_ROOT_USER}')
        result = subprocess.run(('/usr/bin/su', '-', NON_ROOT_USER, f'{dir}/test.sh'), capture_output=True, text=True)
        log.info(f'test.sh stdout is: {str(result.stdout)}')
        log.info(f'test.sh stderr is: {str(result.stderr)}')

        # Check that pid file exists
        log.debug(f'Check that pid file {dir}/root/run/dirsrv/slapd-{INSTANCE_SERVERID}.pid exist')
        pid_filename = f'{dir}/root/run/dirsrv/slapd-{INSTANCE_SERVERID}.pid'
        with open(pid_filename, 'rt') as f:
            pid = int(f.readline())
        assert pid>1
        # Signal the 389ds instance to stop (and raise an exception if we can do that)
        # because the process should already be stopped.
        log.debug(f'Check that instance with pid {pid} is stopped')
        with pytest.raises(OSError) as e_info:
            os.kill(pid, 15)
        # Let check that demo_user is in the search result
        log.debug(f'Check that {dir}/search.out contains with pid {pid} is stopped')
        with open(f'{dir}/search.out', 'rt') as f:
            assert(re.findall('demo_user', f.read()))
        log.debug(f'Check that Wtest.sh finihed successfully.')
        assert(result.returncode == 0)
    # Instance got deleted by the with cleanup
