# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import glob
import ldap
import logging
import pytest
import os
import time
from lib389._constants import DEFAULT_SUFFIX
from lib389.dseldif import DSEldif
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain

log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1

def log_rotated_count(log_type, log_dir, check_compressed=False):
    # Check if the log was rotated
    log_file = f'{log_dir}/{log_type}.2*'
    if check_compressed:
        log_file += ".gz"
    return len(glob.glob(log_file))


def update_and_sleep(suffix, sleep=True):
    for loop in range(2):
        for count in range(10):
            suffix.replace('description', str(count))
            suffix.get_attr_val('description')
            with pytest.raises(ldap.OBJECT_CLASS_VIOLATION):
                suffix.add('doesNotExist', 'error')
        if sleep:
            # log rotation smallest unit is 1 minute
            time.sleep(61)
        else:
            # should still sleep for a little bit
            time.sleep(1)


def test_logging_compression(topo):
    """Test logging compression works, and log rotation/deletion is still
    functional.  This also tests a mix of non-compressed/compressed logs.

    :id: 15b5ed0e-628c-48e5-a61e-43908590c9f1
    :setup: Standalone Instance
    :steps:
        1. Enable all the logs (audit,and auditfail)
        2. Set an aggressive rotation/deletion policy
        3. Make sure all logs are rotated at least once
        4. Enable log compression on all logs
        5. Make sure all logs are rotated again and are compressed
        6. Make sure log deletion is working
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    inst = topo.standalone
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    timeunit = "minute"  # This is the smallest time unit available
    log_dir = inst.get_log_dir()

    # Enable all the logs (audit, and auditfail)
    inst.stop()
    dse_ldif = DSEldif(inst)
    dse_ldif.replace('cn=config', 'nsslapd-auditfaillog', log_dir + "/auditfail")
    inst.start()

    inst.config.set('nsslapd-auditlog-logging-enabled', 'on')
    inst.config.set('nsslapd-auditfaillog-logging-enabled', 'on')
    inst.config.set('nsslapd-accesslog-logbuffering', 'off')
    inst.config.set('nsslapd-errorlog-level', '64')

    # Set an aggressive rotation/deletion policy for all logs
    for ds_log in ['accesslog', 'auditlog', 'auditfaillog', 'errorlog']:
        inst.config.set('nsslapd-' + ds_log + '-logrotationtime', '1')
        inst.config.set('nsslapd-' + ds_log + '-logrotationtimeunit', timeunit)
        inst.config.set('nsslapd-' + ds_log + '-maxlogsize', '1')
        inst.config.set('nsslapd-' + ds_log + '-maxlogsperdir', '3')

    # Perform ops that will write to each log
    update_and_sleep(suffix)

    # Make sure logs are rotated
    for log_type in ['access', 'audit', 'auditfail', 'errors']:
        assert log_rotated_count(log_type, log_dir) > 0

    # Enable log compression on all logs
    for ds_log in ['accesslog', 'auditlog', 'auditfaillog', 'errorlog']:
        inst.config.set('nsslapd-' + ds_log + '-compress', 'on')

    # Perform ops that will write to each log
    update_and_sleep(suffix)

    # Make sure all logs were rotated again and are compressed
    for log_type in ['access', 'audit', 'auditfail', 'errors']:
        assert log_rotated_count(log_type, log_dir, check_compressed=True) > 0

    # Make sure log deletion is working
    update_and_sleep(suffix, sleep=False)
    for log_type in ['access', 'audit', 'auditfail', 'errors']:
        assert log_rotated_count(log_type, log_dir) == 2


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

