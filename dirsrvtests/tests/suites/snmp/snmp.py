# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import pytest
import logging
import subprocess
import ldap
from datetime import datetime
from shutil import copyfile
from lib389.topologies import topology_m2 as topo_m2
from lib389.utils import selinux_present


DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


SNMP_USER = 'user_name'
SNMP_PASSWORD = 'authentication_password'
SNMP_PRIVATE = 'private_password'

# LDAP OID in MIB
LDAP_OID = '.1.3.6.1.4.1.2312.6.1.1'
LDAPCONNECTIONS_OID = f'{LDAP_OID}.21'


def run_cmd(cmd, check_returncode=True):
    """Run a command"""

    log.info(f'Run: {cmd}')
    result = subprocess.run(cmd, capture_output=True, universal_newlines=True)
    log.info(f'STDOUT of {cmd} is:\n{result.stdout}')
    log.info(f'STDERR of {cmd} is:\n{result.stderr}')
    if check_returncode:
        result.check_returncode()
    return result


def add_lines(lines, filename):
    """Add lines that are not already present at the end of a file"""

    log.info(f'add_lines({lines}, {filename})')
    try:
        with open(filename, 'r') as fd:
            for line in fd:
                try:
                    lines.remove(line.strip())
                except ValueError:
                    pass
    except FileNotFoundError:
        pass
    if lines:
        with open(filename, 'a') as fd:
            for line in lines:
                fd.write(f'{line}\n')


def remove_lines(lines, filename):
    """Remove lines in a file"""

    log.info(f'remove_lines({lines}, {filename})')
    file_lines = []
    with open(filename, 'r') as fd:
        for line in fd:
            if not line.strip() in lines:
                file_lines.append(line)
    with open(filename, 'w') as fd:
        for line in file_lines:
            fd.write(line)


@pytest.fixture(scope="module")
def setup_snmp(topo_m2, request):
    """Install snmp and configure it

    Returns the time just before dirsrv-snmp get restarted
    """

    inst1 = topo_m2.ms["supplier1"]
    inst2 = topo_m2.ms["supplier2"]

    # Check for the test prerequisites
    if os.getuid() != 0:
        pytest.skip('This test should be run by root superuser')
        return None
    if not inst1.with_systemd_running():
        pytest.skip('This test requires systemd')
        return None
    required_packages = {
        '389-ds-base-snmp': os.path.join(inst1.get_sbin_dir(), 'ldap-agent'),
        'net-snmp': '/etc/snmp/snmpd.conf', }
    skip_msg = ""
    for package,file in required_packages.items():
        if not os.path.exists(file):
            skip_msg += f"Package {package} is not installed ({file} is missing).\n"
    if skip_msg != "":
        pytest.skip(f'This test requires the following package(s): {skip_msg}')
        return None

    # Install snmp
    # run_cmd(['/usr/bin/dnf', 'install', '-y', 'net-snmp', 'net-snmp-utils', '389-ds-base-snmp'])

    # Prepare the lines to add/remove in files:
    #  master agentx
    #  snmp user (user_name - authentication_password - private_password)
    #  ldap_agent ds instances
    #
    # Adding rwuser and createUser lines is the same as running:
    #  net-snmp-create-v3-user -A authentication_password -a SHA -X private_password -x AES user_name
    # but has the advantage of removing the user at cleanup phase
    #
    agent_cfg = '/etc/dirsrv/config/ldap-agent.conf'
    lines_dict = { '/etc/snmp/snmpd.conf' : ['master agentx', f'rwuser {SNMP_USER}'],
                   '/var/lib/net-snmp/snmpd.conf' : [
                        f'createUser {SNMP_USER} SHA "{SNMP_PASSWORD}" AES "{SNMP_PRIVATE}"',],
                   agent_cfg : [] }
    for inst in topo_m2:
        lines_dict[agent_cfg].append(f'server slapd-{inst.serverid}')

    # Prepare the cleanup
    def fin():
        run_cmd(['systemctl', 'stop', 'dirsrv-snmp'])
        if not DEBUGGING:
            run_cmd(['systemctl', 'stop', 'snmpd'])
            try:
                os.remove('/usr/share/snmp/mibs/redhat-directory.mib')
            except FileNotFoundError:
                pass
            for filename,lines in lines_dict.items():
                remove_lines(lines, filename)
            run_cmd(['systemctl', 'start', 'snmpd'])

    request.addfinalizer(fin)

    # Copy RHDS MIB in default MIB search path (Ugly because I have not found how to change the search path)
    copyfile('/usr/share/dirsrv/mibs/redhat-directory.mib', '/usr/share/snmp/mibs/redhat-directory.mib')

    run_cmd(['systemctl', 'stop', 'snmpd'])
    for filename,lines in lines_dict.items():
        add_lines(lines, filename)

    run_cmd(['systemctl', 'start', 'snmpd'])

    curtime = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    run_cmd(['systemctl', 'start', 'dirsrv-snmp'])
    return curtime


@pytest.mark.skipif(not os.path.exists('/usr/bin/snmpwalk'), reason="net-snmp-utils package is not installed")
def test_snmpwalk(topo_m2, setup_snmp):
    """snmp smoke tests.

    :id: e5d29998-1c21-11ef-a654-482ae39447e5
    :setup: Two suppliers replication setup, snmp
    :steps:
        1. use snmpwalk to display LDAP statistics
        2.  use snmpwalk to get the number of open connections
    :expectedresults:
        1. Success and no messages in stderr
        2. The number of open connections should be positive
    """

    inst1 = topo_m2.ms["supplier1"]
    inst2 = topo_m2.ms["supplier2"]


    cmd = [ '/usr/bin/snmpwalk', '-v3', '-u', SNMP_USER, '-l', 'AuthPriv',
            '-m', '+RHDS-MIB', '-A', SNMP_PASSWORD, '-a', 'SHA',
            '-X', SNMP_PRIVATE, '-x', 'AES', 'localhost',
            LDAP_OID ]
    result = run_cmd(cmd)
    assert not result.stderr

    cmd = [ '/usr/bin/snmpwalk', '-v3', '-u', SNMP_USER, '-l', 'AuthPriv',
            '-m', '+RHDS-MIB', '-A', SNMP_PASSWORD, '-a', 'SHA',
            '-X', SNMP_PRIVATE, '-x', 'AES', 'localhost',
            f'{LDAPCONNECTIONS_OID}.{inst1.port}', '-Ov' ]
    result = run_cmd(cmd)
    nbconns = int(result.stdout.split()[1])
    log.info(f'There are {nbconns} open connections on {inst1.serverid}')
    assert nbconns > 0


@pytest.mark.skipif(not selinux_present(), reason="SELinux is not enabled")
def test_snmp_avc(topo_m2, setup_snmp):
    """snmp smoke tests.

    :id: fb79728e-1d0d-11ef-9213-482ae39447e5
    :setup: Two suppliers replication setup, snmp
    :steps:
        1. Get the system journal about ldap-agent
    :expectedresults:
        1. No AVC should be present
    """
    result = run_cmd(['journalctl', '-S', setup_snmp, '-g', 'ldap-agent'])
    assert not 'AVC' in result.stdout


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
