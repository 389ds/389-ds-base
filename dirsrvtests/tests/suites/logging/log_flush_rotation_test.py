# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import time
import pytest
from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.tasks import ImportTask
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_st as topo


log = logging.getLogger(__name__)


def test_log_flush_and_rotation_crash(topo):
    """Make sure server does not crash whening flushing a buffer and rotating
    the log at the same time

    :id: d4b0af2f-48b2-45f5-ae8b-f06f692c3133
    :setup: Standalone Instance
    :steps:
        1. Enable all logs
        2. Enable log buffering for all logs
        3. Set rotation time unit to 1 minute
        4. Make sure server is still running after 1 minute
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    inst = topo.standalone

    # Enable logging and buffering
    inst.config.set("nsslapd-auditlog-logging-enabled", "on")
    inst.config.set("nsslapd-accesslog-logbuffering", "on")
    inst.config.set("nsslapd-auditlog-logbuffering", "on")
    inst.config.set("nsslapd-errorlog-logbuffering", "on")
    inst.config.set("nsslapd-securitylog-logbuffering", "on")

    # Set rotation policy to trigger rotation asap
    inst.config.set("nsslapd-accesslog-logrotationtimeunit", "minute")
    inst.config.set("nsslapd-auditlog-logrotationtimeunit", "minute")
    inst.config.set("nsslapd-errorlog-logrotationtimeunit", "minute")
    inst.config.set("nsslapd-securitylog-logrotationtimeunit", "minute")

    #
    # Performs ops to populate all the logs
    #
    # Access & audit log
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user()
    user.set("userPassword", PW_DM)
    # Security log
    user.bind(PW_DM)
    # Error log
    import_task = ImportTask(inst)
    import_task.import_suffix_from_ldif(ldiffile="/not/here",
                                        suffix=DEFAULT_SUFFIX)

    # Wait a minute and make sure the server did not crash
    log.info("Sleep until logs are flushed and rotated")
    time.sleep(61)

    assert inst.status()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

