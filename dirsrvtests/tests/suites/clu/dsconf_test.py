# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Test dsconf CLI with LDAPS"""

import subprocess
import logging
import os
from lib389.cli_base import LogCapture
import pytest
import ldap
from lib389._constants import DEFAULT_SUFFIX, DN_DM, ReplicaRole
from lib389.topologies import create_topology


pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def enable_config(request, topology_st, config_type):
    if config_type == 'ldapfile':
        with open('/tmp/ldap_temp.conf', 'w') as f:
            f.write("TLS_CACERT /etc/dirsrv/slapd-standalone1/ca.crt\n")
            f.close()

    else:
        data = ['[localhost]\n', 'tls_cacertdir = /etc/dirsrv/slapd-standalone1\n',
                f'uri = {topology_st.standalone.get_ldaps_uri()}\n']
        fd = open(f'{os.environ.get("HOME")}/.dsrc', 'w')
        for line in data:
            fd.write(line)
        fd.close()

    def fin():
        if config_type == 'ldapfile':
            os.remove('/tmp/ldap_temp.conf')
        else:
            os.remove(f'{os.environ.get("HOME")}/.dsrc')

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def topology_st(request):
    """Create DS standalone instance"""

    topology = create_topology({ReplicaRole.STANDALONE: 1})

    topology.logcap = LogCapture()
    return topology


@pytest.mark.parametrize('config_type', ('ldapfile','dsrfile'))
def test_dsconf_with_ldaps(topology_st, enable_config, config_type):
    """Test dsconf CLI with LDAPS

    :id: 5288a288-60f0-4e81-a44b-d2ee2611ca86
    :parametrized: yes
    :customerscenario: True
    :setup: Standalone Instance
    :steps:
        1. Enable TLS
        2. Set only ldap.conf
        3. Verify dsconf command is working correctly on ldaps
        4. Set only ~/.dsrc file
        5. Verify dsconf command is working correctly on ldaps
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    log.info("Enable TLS")
    topology_st.standalone.enable_tls()
    if config_type == 'ldapfile':
        log.info("Use dsconf to list certificate")
        cmdline=['/usr/sbin/dsconf', topology_st.standalone.get_ldaps_uri(), '-D',
                 DN_DM, '-w', 'password', 'security', 'certificate', 'list']
        log.info(f'Command used : %{cmdline}')
        proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE, env={'LDAPCONF': '/tmp/ldap_temp.conf'})
    else:
        log.info("Use dsconf to list certificate")
        cmdline=['/usr/sbin/dsconf','standalone1', '-D', DN_DM, '-w', 'password',
                 'security', 'certificate', 'list']
        log.info(f'Command used : %{cmdline}')
        proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE)

    msg = proc.communicate()
    log.info(f'output message : {msg[0]}')
    assert proc.returncode == 0


@pytest.mark.parametrize('instance_role', ('consumer', 'hub'))
def test_check_replica_id_rejected (instance_role):
    """Test dsconf CLI does not accept replica-id parameter for comsumer and hubs

    :id: 274b47f8-111a-11ee-8321-98fa9ba19b65
    :parametrized: yes
    :customerscenario: True
    :setup: Create DS instance
    :steps:
        1. Create ldap instance
        2. Use dsconf cli to create replica and specify replica id for a consumer
        3. Verify dsconf command rejects replica_id for consumers
        4. Repeat for a hub use dsconf to create a replica w replica id
        5. Verify dsconf command rejects replica_id for hubs
    :expectedresults:
        1. Success
        2. Success
        3. Setting the "replica-id" manually for consumers not allowed.
        4. Success
        5. Setting the "replica-id" manually for hubs is not allowed.
    """
    print("DN_DM {}".format(DN_DM))
    cmdline = ['/usr/sbin/dsconf', 'standalone1', '-D', DN_DM, '-w', 'password', 'replication', 'enable', '--suffix', DEFAULT_SUFFIX, '--role', instance_role, '--replica-id=1']
    log.info(f'Command used : {cmdline}')
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE)

    msg = proc.communicate()
    msg = msg[0].decode('utf-8')
    log.info(f'output message : {msg}')
    assert "Replication successfully enabled for" not in msg, f"Test Failed: --replica-id option is accepted....It shouldn't for {instance_role}"
    log.info(f"Test PASSED: --replica-id option is NOT accepted for {instance_role}.")
