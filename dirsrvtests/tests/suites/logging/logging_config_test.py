# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import ldap
from lib389._constants import *
from lib389.topologies import topology_st as topo

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

big_value = "1111111111111111111111111111111111111111111"

pytestmark = pytest.mark.tier1

@pytest.mark.parametrize("attr, invalid_vals, valid_vals",
                         [
                             ("logexpirationtime", ["-2", "0"], ["1", "-1"]),
                             ("maxlogsize", ["-2", "0"], ["100", "-1"]),
                             ("logmaxdiskspace", ["-2", "0"], ["100", "-1"]),
                             ("logminfreediskspace", ["-2", "0"], ["100", "-1"]),
                             ("mode", ["888", "778", "77", "7777"], ["777", "000", "600"]),
                             ("maxlogsperdir", ["-1", "0"], ["1", "20"]),
                             ("logrotationsynchour", ["-1", "24"], ["0", "23"]),
                             ("logrotationsyncmin", ["-1", "60"], ["0", "59"]),
                             ("logrotationtime", ["-2", "0"], ["100", "-1"])
                         ])
def test_logging_digit_config(topo, attr, invalid_vals, valid_vals):
    """Validate logging config settings

    :id: a0ef30e5-538b-46fa-9762-01a4435a15e9
    :parametrized: yes
    :setup: Standalone Instance
    :steps:
        1. Test log expiration time
        2. Test log max size
        3. Test log max disk space
        4. Test log min disk space
        5. Test log mode
        6. Test log max number of logs
        7. Test log rotation hour
        8. Test log rotation minute
        9. Test log rotation time
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
    """

    accesslog_attr = "nsslapd-accesslog-{}".format(attr)
    auditlog_attr = "nsslapd-auditlog-{}".format(attr)
    auditfaillog_attr = "nsslapd-auditfaillog-{}".format(attr)
    errorlog_attr = "nsslapd-errorlog-{}".format(attr)

    # Test each log
    for attr in [accesslog_attr, auditlog_attr, auditfaillog_attr, errorlog_attr]:
        # Invalid values
        for invalid_val in invalid_vals:
            with pytest.raises(ldap.LDAPError):
                topo.standalone.config.set(attr, invalid_val)

        # Invalid high value
        with pytest.raises(ldap.LDAPError):
            topo.standalone.config.set(attr, big_value)

        # Non digits
        with pytest.raises(ldap.LDAPError):
            topo.standalone.config.set(attr, "abc")

        # Valid values
        for valid_val in valid_vals:
            topo.standalone.config.set(attr, valid_val)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
