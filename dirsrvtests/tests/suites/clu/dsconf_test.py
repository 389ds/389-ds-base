# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Test dsconf CLI with LDAPS"""

import ldap
import logging
import pytest
import subprocess
import os
from lib389._constants import DEFAULT_SUFFIX, DN_DM
from lib389.topologies import topology_st

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


def test_backend_referral(topology_st):
    """Test setting and deleting referral in backend

    :id: e65fa6c3-da7c-49f8-be0c-738be46a1180
    :setup: Standalone Instance
    :steps:
        1. Set referral using dsconf command
        2. Verify that referral is set correctly
        3. Set nsslapd-state to referral and verify
        4. Test a referral error
        5. Restart the server
        6. Cleanup - delete referral and set nsslapd-state to 'backend'
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    dsconf_cmd= ['/usr/sbin/dsconf', topology_st.standalone.serverid, '-D', DN_DM, '-w', 'password']
    # Set referral
    log.info("Use dsconf to set referral")
    cmdline = dsconf_cmd + ['backend', 'suffix', 'set', '--add-referral', 'ldap://localhost.localdomain:389/o%3dnetscaperoot', 'userRoot']
    log.info(f'Command used: %{cmdline}')
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE)
    msg = proc.communicate()
    log.info('output message : %s' % msg[0])
    assert proc.returncode == 0

    # Check referral
    log.info("Verify referral is set correctly")
    cmdline = dsconf_cmd + ['backend', 'suffix', 'get', 'userRoot']
    log.info(f'Command used: %{cmdline}')
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE)
    out, _ = proc.communicate()
    log.info('output message : %s' % out[0])
    assert 'referral: ldap://localhost.localdomain:389/o%3dnetscaperoot' in out.decode('utf-8')

    # Set nsslapd-state to referral
    log.info("Use dsconf to set nsslapd-state to referral")
    cmdline = dsconf_cmd + ['backend', 'suffix', 'set', '--state', 'referral', 'userRoot']
    log.info(f'Command used: %{cmdline}')
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE)
    msg = proc.communicate()
    log.info('output message : %s' % msg[0])
    assert proc.returncode == 0

    # Verify nsslapd-state
    log.info("Verify nsslapd-state is set to referral")
    cmdline = dsconf_cmd + ['backend', 'suffix', 'get', 'userRoot']
    log.info(f'Command used: %{cmdline}')
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE)
    out, _ = proc.communicate()
    log.info('output message : %s' % out[0])
    assert 'state: referral' in out.decode('utf-8')

    # Test a referral error
    topology_st.standalone.set_option(ldap.OPT_REFERRALS, 0)  # Do not follow referral
    with pytest.raises(ldap.REFERRAL):
        topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=top')

    # Restart the server
    log.info('Restarting the server...')
    topology_st.standalone.restart(timeout=10)

    # Cleanup
    log.info('Cleaning up...')
    cmdline = dsconf_cmd + ['backend', 'suffix', 'set', '--state', 'backend', 'userRoot']
    log.info(f'Command used: %{cmdline}')
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE)
    out, _ = proc.communicate()
    log.info('output message : %s' % out[0])
    assert proc.returncode == 0

    cmdline = dsconf_cmd + ['backend', 'suffix', 'set', '--del-referral', 'ldap://localhost.localdomain:389/o%3dnetscaperoot', 'userRoot']
    log.info(f'Command used: %{cmdline}')
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE)
    out, _ = proc.communicate()
    log.info('output message : %s' % out[0])
    assert proc.returncode == 0
    topology_st.standalone.set_option(ldap.OPT_REFERRALS, 1)


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
