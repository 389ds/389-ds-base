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
import itertools
import ldap
import pytest
import re
import subprocess
import time
from lib389._constants import DEFAULT_SUFFIX
from lib389.dirsrv_log import DirsrvAccessJSONLog, DirsrvAccessLog
from test389.topologies import topology_st as topo


log = logging.getLogger(__name__)

FGOT_KEYS = [ 'wqtime', 'wtime', 'etime', 'optime', 'writetime' ]
tested_keys = []
for r in range(1,len(FGOT_KEYS)):
    tested_keys.extend(itertools.combinations(FGOT_KEYS, r))

@pytest.fixture(scope="module")
def setup_topo(topo):
    """Configure log settings and threads number"""
    inst = topo.standalone
    inst.config.set("nsslapd-accesslog-level", "256")
    inst.config.replace('nsslapd-accesslog-logbuffering', 'off')
    inst.config.replace('nsslapd-threadnumber', '3')
    inst.restart()
    return topo


@pytest.fixture(params=[True,False])
def setup_json(request, setup_topo):
    inst = setup_topo.standalone
    format = 'json' if request.param else 'default'
    inst.config.replace('nsslapd-accesslog-log-format', format)
    return (setup_topo, request.param)


def get_last_logs(inst, nblines, jsonlog):
    if jsonlog:
        access_log = DirsrvAccessJSONLog(inst)
    else:
        access_log = DirsrvAccessLog(inst)
    log_lines = access_log.readlines()
    while (len(log_lines) > nblines):
        log_lines.pop(0)
    if jsonlog:
        for line in log_lines:
            event = access_log.parse_line(line)
            if event is None or 'header' in event:
                # Skip non-json or header lines
                continue
            if event['operation'] == 'RESULT':
                yield event
    else:
        pattern = r'(\w+)=("(?:[^"\\]|\\.)*"|\S+)'
        for line in log_lines:
            if 'RESULT' in line:
                matches = re.findall(pattern, line)
                d = { m[0]: m[1] for m in matches }
                yield d


def test_wqtime(setup_json):
    """Check wqtime impact when not having enough worker threads

    :id: 8559e688-220b-11f1-b2cc-c85309d5c3e3
    :setup: Standalone instance with very few working threads
    :steps:
        1. Hammer server with searches using ldclt
        2. Check that on some operations wqtime is the main contributor
    :expectedresults:
        1. Success
        2. Success
    """
    topo, json_log = setup_json

    inst = topo.standalone
    cmd = [ 'ldclt', '-H', f'ldap://localhost:{inst.port}', '-e',
            'esearch', '-f', '(uid=*)', '-N', '3' ]
    subprocess.run(cmd, check=True)

    max_wqtime_ratio = 0
    for event in get_last_logs(inst, 40, json_log):
        wqtime = float(event['wqtime'])
        etime = float(event['etime'])
        ratio = int(100 * wqtime / etime)
        if (ratio > max_wqtime_ratio):
            max_wqtime_ratio = ratio
    assert max_wqtime_ratio > 50


@pytest.mark.parametrize("tested_key", tested_keys)
def test_fgot_config(setup_json, tested_key):
    """Check fine grain operation timing configuration

    :id: 38af4f8a-26c0-11f1-920b-c85309d5c3e3
    :setup: Standalone instance with very few working threads
    :steps:
        1. Configure ds-fine-grain-operation-timing toi tested value
        2. Perform a search
        3. Check that result has the proper fine grain operation timing
           keywords that are set and nonme of the unset one
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    topo, json_log = setup_json
    inst = topo.standalone

    fgot_val = "+".join(tested_key)
    inst.config.replace('ds-fine-grain-operation-timing', fgot_val)
    inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(uid=*)")
    # Wait enough to ensure the result is logged
    log.info(f'Trying: {tested_key}')
    time.sleep(2)
    event = next(get_last_logs(inst, 1, json_log))
    log.info(f'Got: {event}')
    for k in FGOT_KEYS:
        if k in tested_key:
            assert k in event
        else:
            assert k not in event


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
