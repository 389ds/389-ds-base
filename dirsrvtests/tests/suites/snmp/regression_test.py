# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import logging
import os
from subprocess import check_output, PIPE, run
import time

import ldap
import pytest
from lib389._constants import DN_DM, PW_DM
from lib389.monitor import MonitorSNMP
from test389.topologies import topology_st


pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


def get_bind_security_errors(inst):
    return MonitorSNMP(inst).get_attr_val_int('bindsecurityerrors')


def wait_for_bind_security_errors(inst, previous_value):
    for _ in range(30):
        current_value = get_bind_security_errors(inst)
        if current_value > previous_value:
            return current_value
        time.sleep(1)
    return get_bind_security_errors(inst)


@pytest.fixture(scope="function")
def ldapagent_config(topology_st, request):
    """Creates an ldap-agent config for the standalone instance."""

    var_dir = topology_st.standalone.get_local_state_dir()
    config_file = os.path.join(topology_st.standalone.get_sysconf_dir(), 'dirsrv/config/agent.conf')
    config = f"""agentx-supplier {var_dir}/agentx/supplier
agent-logdir {var_dir}/log/dirsrv
server slapd-{topology_st.standalone.serverid}
"""

    with open(config_file, 'w') as agent_config_file:
        agent_config_file.write(config)

    def fin():
        os.remove(config_file)

    request.addfinalizer(fin)

    return config_file


def test_ldapagent_uses_instance_stats_file(topology_st, ldapagent_config):
    """Tests that ldap-agent loads the instance stats file used for SNMP counters

    :id: 9bf83f16-922f-4505-8dc4-d22bd8e426ac
    :setup: Standalone instance
    :steps:
         1. Check the current bindSecurityErrors SNMP monitor counter.
         2. Perform an invalid Directory Manager bind.
         3. Check that bindSecurityErrors increased.
         4. Start ldap-agent with debug logging.
         5. Check that ldap-agent opens the instance .stats file.
         6. Check that ldap-agent does not open the truncated .stat file.
         7. Cleanup - Kill ldap-agent process.
    :expectedresults:
         1. The initial bindSecurityErrors value should be readable.
         2. The invalid bind should fail.
         3. The bindSecurityErrors value should increase.
         4. ldap-agent should start.
         5. ldap-agent should open the full .stats file path used by ns-slapd.
         6. ldap-agent should not open or fail on the truncated .stat path.
         7. ldap-agent process should be successfully killed.
    """

    log.info('Running test_ldapagent_uses_instance_stats_file...')

    if not os.path.exists(os.path.join(topology_st.standalone.get_sbin_dir(), 'ldap-agent')):
        pytest.skip("ldap-agent is not present")

    previous_bind_security_errors = get_bind_security_errors(topology_st.standalone)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        topology_st.standalone.simple_bind_s(DN_DM, 'badpassword')
    topology_st.standalone.simple_bind_s(DN_DM, PW_DM)
    assert wait_for_bind_security_errors(topology_st.standalone, previous_bind_security_errors) > previous_bind_security_errors

    run_dir = topology_st.standalone.get_run_dir()
    pidpath = os.path.join(run_dir, 'ldap-agent.pid')
    agent_log = os.path.join(topology_st.standalone.get_local_state_dir(), 'log', 'dirsrv', 'ldap-agent.log')
    expected_stats_file = os.path.join(run_dir, f'slapd-{topology_st.standalone.serverid}.stats')
    truncated_stats_file = expected_stats_file[:-1]
    expected_log_line = f'Opening stats file ({expected_stats_file})'
    pid = None
    agent_log_content = ''

    if os.path.exists(agent_log):
        os.remove(agent_log)

    try:
        check_output([os.path.join(topology_st.standalone.get_sbin_dir(), 'ldap-agent'), '-D', ldapagent_config])

        with open(pidpath, 'r') as pf:
            pid = pf.readlines()[0].strip()

        for _ in range(30):
            if os.path.exists(agent_log):
                with open(agent_log, 'r') as lf:
                    agent_log_content = lf.read()
                if expected_log_line in agent_log_content:
                    break
            time.sleep(1)

        assert expected_log_line in agent_log_content
        assert f'Opening stats file ({truncated_stats_file})' not in agent_log_content
        assert f'Unable to open stats file ({truncated_stats_file})' not in agent_log_content
    finally:
        if pid:
            log.debug('test_ldapagent_uses_instance_stats_file: Terminating agent %s', pid)
            run(['kill', pid], stdout=PIPE, stderr=PIPE)

    log.info('test_ldapagent_uses_instance_stats_file: PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
