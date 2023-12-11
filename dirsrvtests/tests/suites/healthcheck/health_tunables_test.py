# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import subprocess
import pytest
import re
import os
from lib389.utils import *
from lib389.cli_base import FakeArgs
from lib389.topologies import topology_st
from lib389.cli_ctl.health import health_check_run
from lib389.paths import Paths

CMD_OUTPUT = 'No issues found.'
JSON_OUTPUT = '[]'
RET_CODE = 'DSTHPLE0001'

log = logging.getLogger(__name__)
p = Paths()


def run_healthcheck_and_flush_log(topology, instance, searched_code=None, json=False, searched_code2=None,
                                  list_checks=False, list_errors=False, check=None, searched_list=None):
    args = FakeArgs()
    args.instance = instance.serverid
    args.verbose = instance.verbose
    args.list_errors = list_errors
    args.list_checks = list_checks
    args.check = check
    args.dry_run = False
    args.json = json

    log.info('Use healthcheck with --json == {} option'.format(json))
    health_check_run(instance, topology.logcap.log, args)

    if searched_list is not None:
        for item in searched_list:
            assert topology.logcap.contains(item)
            log.info('Healthcheck returned searched item: %s' % item)
    else:
        assert topology.logcap.contains(searched_code)
        log.info('Healthcheck returned searched code: %s' % searched_code)

    if searched_code2 is not None:
        assert topology.logcap.contains(searched_code2)
        log.info('Healthcheck returned searched code: %s' % searched_code2)

    log.info('Clear the log')
    topology.logcap.flush()


def _set_thp_system_mode(mode):
    thp_path = '/sys/kernel/mm/transparent_hugepage/enabled'
    with open(thp_path, 'w') as f:
        log.info(f"Setting THP mode to {mode}")
        f.write(mode)


def _set_thp_instance_mode(inst, disable: bool):
    service_config = f"[Service]\nEnvironment=THP_DISABLE={int(disable)}"
    drop_in_path = f"/etc/systemd/system/dirsrv@{inst.serverid}.service.d/"
    os.makedirs(drop_in_path, exist_ok=True)
    with open(os.path.join(drop_in_path, "thp.conf"), 'w') as f:
        f.write(service_config)
    subprocess.run(['systemctl', 'daemon-reload'], check=True)
    inst.restart()


def _get_thp_system_mode():
    thp_path = '/sys/kernel/mm/transparent_hugepage/enabled'
    enabled_value_pattern = r'\[([^\]]+)\]'
    with open(thp_path, 'r') as f:
        text = f.read().strip()
        mode = re.search(enabled_value_pattern, text)[1]
        log.info(f"Current THP mode is {mode}")
        return mode


@pytest.fixture(scope="function")
def thp_reset(request):
    mode = _get_thp_system_mode()

    def fin():
        _set_thp_system_mode(mode)

    request.addfinalizer(fin)


@pytest.mark.skipif(get_user_is_root() is False,
                    reason="This test requires root permissions to change kernel tunables")
@pytest.mark.skipif(p.with_systemd is False, reason='Needs systemd to run')
@pytest.mark.skipif(ds_is_older("1.4.3.38"), reason="Not implemented")
@pytest.mark.parametrize("system_thp_mode,instance_thp_mode,expected_output",
                       [("always", False, (RET_CODE, RET_CODE)),
                        ("always", True, (CMD_OUTPUT, JSON_OUTPUT)),
                        ("never", False, (CMD_OUTPUT, JSON_OUTPUT)),
                        ("never", True, (CMD_OUTPUT, JSON_OUTPUT))],
                        ids=["System and Instance THP ON",
                             "System THP ON, Instance THP OFF",
                             "System THP OFF, Instance THP ON",
                             "System THP OFF, Instance THP OFF"])
@pytest.mark.usefixtures("thp_reset")
def test_healthcheck_transparent_huge_pages(topology_st, system_thp_mode, instance_thp_mode, expected_output):
    """Check if HealthCheck returns DSTHPLE0001 code

    :id: 1f195e10-6403-4c92-8ac9-724b669e8cf2
    :setup: Standalone instance
    :parametrized: yes
    :steps:
        1. Enable THP system wide and for the instance
        2. Use HealthCheck without --json option
        3. Use HealthCheck with --json option
        4. Enable THP system wide, disable THP for the instance
        5. Use HealthCheck without --json option
        6. Use HealthCheck with --json option
        7. Disable THP system wide, enable THP for the instance
        8. Use HealthCheck without --json option
        9. Use HealthCheck with --json option
        10. Disable THP system wide, disable THP for the instance
        11. Use HealthCheck without --json option
        12. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. HealthCheck should return code DSHTPLE0001
        3. HealthCheck should return code DSTHPLE0001
        4. Success
        5. HealthCheck reports no issue found
        6. HealthCheck reports no issue found
        7. Success
        8. HealthCheck reports no issue found
        9. HealthCheck reports no issue found
        10. Success
        11. HealthCheck reports no issue found
        12. HealthCheck reports no issue found
    """
    standalone = topology_st.standalone
    standalone.config.set("nsslapd-accesslog-logbuffering", "on")

    _set_thp_system_mode(system_thp_mode)
    _set_thp_instance_mode(standalone, instance_thp_mode)
    run_healthcheck_and_flush_log(topology_st, standalone, expected_output[0], json=False)
    run_healthcheck_and_flush_log(topology_st, standalone, expected_output[1], json=True)
