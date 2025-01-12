# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
from lib389.dseldif import DSEldif
from lib389._constants import DN_CONFIG, LOG_REPLICA, LOG_DEFAULT, LOG_TRACE, LOG_ACL
from lib389.utils import os, logging
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.mark.parametrize("log_level", [(LOG_REPLICA + LOG_DEFAULT), (LOG_ACL + LOG_DEFAULT), (LOG_TRACE + LOG_DEFAULT)])
def test_default_loglevel_stripped(topo, log_level):
    """The default log level 16384 is stripped from the log level returned to a client

    :id: c300f8f1-aa11-4621-b124-e2be51930a6b
    :parametrized: yes
    :setup: Standalone instance

    :steps: 1. Change the error log level to the default and custom value.
            2. Check if the server returns the new value.

    :expectedresults:
            1. Changing the error log level should be successful.
            2. Server should return the new log level.
    """

    assert topo.standalone.config.set('nsslapd-errorlog-level', str(log_level))
    assert topo.standalone.config.get_attr_val_int('nsslapd-errorlog-level') == log_level


def test_dse_config_loglevel_error(topo):
    """Manually setting nsslapd-errorlog-level to 64 in dse.ldif throws error

    :id: 0eeefa17-ec1c-4208-8e7b-44d8fbc38f10

    :setup: Standalone instance

    :steps: 1. Stop the server, edit dse.ldif file and change nsslapd-errorlog-level value to 64
            2. Start the server and observe the error logs.

    :expectedresults:
            1. Server should be successfully stopped and nsslapd-errorlog-level value should be changed.
            2. Server should be successfully started without any errors being reported in the logs.
    """

    topo.standalone.stop(timeout=10)
    dse_ldif = DSEldif(topo.standalone)
    try:
        dse_ldif.replace(DN_CONFIG, 'nsslapd-errorlog-level', 64)
    except:
        log.error('Failed to replace cn=config values of nsslapd-errorlog-level')
        raise
    topo.standalone.start(timeout=10)
    assert not topo.standalone.ds_error_log.match(
        '.*nsslapd-errorlog-level: ignoring 64 \\(since -d 266354688 was given on the command line\\).*')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
