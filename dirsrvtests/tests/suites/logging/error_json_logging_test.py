# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import time
import pytest
from lib389.properties import TASK_WAIT
from lib389.topologies import topology_st as topo
from lib389.dirsrv_log import DirsrvErrorJSONLog

log = logging.getLogger(__name__)

MAIN_KEYS = [
    "local_time",
    "severity",
    "subsystem",
    "msg",
]


@pytest.mark.parametrize("log_format", ["json", "json-pretty"])
def test_error_json_format(topo, log_format):
    """Test error log is in JSON

    :id: c9afb295-43de-4581-af8b-ec8f25a06d75
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Check error log has json and the expected data is present
    :expectedresults:
        1. Success
    """

    inst = topo.standalone
    inst.config.replace('nsslapd-errorlog-logbuffering', 'off')  # Just in case
    inst.config.set("nsslapd-errorlog-log-format", log_format)
    inst.stop()
    inst.deleteErrorLogs()
    inst.start()
    time.sleep(1)

    error_log = DirsrvErrorJSONLog(inst)
    log_lines = error_log.parse_log()
    for event in log_lines:
        if event is None or 'header' in event:
            # Skip non-json or header line
            continue
        for key in MAIN_KEYS:
            assert key in event and event[key] != ""


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
