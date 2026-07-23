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
import resource
from lib389.backend import Backends
from lib389._constants import *
from lib389 import pid_from_file
from test389.topologies import topology_st
from lib389.utils import ds_is_older, ensure_str, is_fips

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

FD_ATTR = "nsslapd-maxdescriptors"
RESRV_FD_ATTR = "nsslapd-reservedescriptors"
SLAPD_DEFAULT_MAXDESCRIPTORS = 1048576
RESRV_DESC_VAL_LOW = 10
TOO_LOW_VAL = "0"


def get_process_fd_limit(inst):
    """Get the file descriptor hard limit from the instance process.

    Reads /proc/<pid>/limits to get RLIMIT_NOFILE hard limit.
    Server caps maxdescriptors at min(process rlim_max, SLAPD_DEFAULT_MAXDESCRIPTORS).
    """
    try:
        pid = pid_from_file(inst.pid_file())
        with open(f"/proc/{pid}/limits", "r") as f:
            for line in f:
                if "Max open files" in line:
                    log.info(line)
                    parts = line.split()
                    hard_limit = parts[4]
                    if hard_limit == "unlimited":
                        return SLAPD_DEFAULT_MAXDESCRIPTORS
                    else:
                        return min(int(hard_limit), SLAPD_DEFAULT_MAXDESCRIPTORS)
    except Exception as e:
        log.warning(f"Failed to read process limits: {e}, using default")
        return SLAPD_DEFAULT_MAXDESCRIPTORS

    return SLAPD_DEFAULT_MAXDESCRIPTORS

@pytest.mark.skipif(ds_is_older("1.4.1.2"), reason="Not implemented")
def test_fd_limits(topology_st):
    """Test the default limits, and custom limits

    :id: fa0a5106-612f-428f-84c0-9c85c34d0433
    :setup: Standalone Instance
    :steps:
        1. Check default limit
        2. Change default limit
        3. Check invalid/too high limits are rejected
        4. Check invalid/too low limit is rejected
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    # Get the process FD limit dynamically
    max_fd_val = get_process_fd_limit(topology_st.standalone)
    custom_val = str(max_fd_val - 10)
    too_high_val = str(max_fd_val + 1)
    log.info(f'test_fd_limits: max_fd_val={max_fd_val} custom_val={custom_val}' +
             f'too_high_val={too_high_val}')

    # Check process default
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == str(max_fd_val)

    # Check custom value is applied
    topology_st.standalone.config.set(FD_ATTR, custom_val)
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == custom_val

    # Attempt to use value that is higher than the process limit
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology_st.standalone.config.set(FD_ATTR, too_high_val)
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == custom_val

    # Attempt to use value that is higher than the process limit (second check)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology_st.standalone.config.set(FD_ATTR, too_high_val)
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == custom_val

    # Attempt to use val that is too low
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topology_st.standalone.config.set(FD_ATTR, TOO_LOW_VAL)
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == custom_val

    log.info("test_fd_limits PASSED")

@pytest.mark.skipif(ds_is_older("1.4.1.2"), reason="Not implemented")
def test_reserve_descriptor_validation(topology_st):
    """Test the reserve descriptor self check

    :id: 9bacdbcc-7754-4955-8a56-1d8c82bce274
    :setup: Standalone Instance
    :steps:
        1. Set attr nsslapd-reservedescriptors to a low value (10)
        2. Verify low value has been set
        3. Restart instance (On restart the reservedescriptor attr will be validated)
        4. Verify corrected value for nsslapd-reservedescriptors > low value
    :expectedresults:
        1. Success
        2. A value of RESRV_DESC_VAL_LOW (10) is returned
        3. Success
        4. Corrected value for nsslapd-reservedescriptors > low value
    """

    # Set nsslapd-reservedescriptors to a low value (10)
    topology_st.standalone.config.set(RESRV_FD_ATTR, str(RESRV_DESC_VAL_LOW))
    resrv_fd = int(topology_st.standalone.config.get_attr_val_utf8(RESRV_FD_ATTR))
    assert resrv_fd == RESRV_DESC_VAL_LOW

    # An instance restart triggers a validation of the configured nsslapd-reservedescriptors attribute
    topology_st.standalone.restart()

    # Get the corrected value
    corrected_fd = int(topology_st.standalone.config.get_attr_val_utf8(RESRV_FD_ATTR))
    assert corrected_fd > RESRV_DESC_VAL_LOW

    log.info(f"test_reserve_descriptor_validation PASSED (corrected from {RESRV_DESC_VAL_LOW} to {corrected_fd})")

@pytest.mark.skipif(ds_is_older("1.4.1.2"), reason="Not implemented")
def test_reserve_descriptors_high(topology_st):
    """Test setting reserve descriptor value to higher than average.

    :id: 19c8991b-ef78-485e-bdf9-a0977fcbcd04
    :setup: Standalone Instance
    :steps:
        1. Set attr nsslapd-maxdescriptors to the server's effective max
        2. Verify value has been set
        3. Set attr nsslapd-reservedescriptors to 2 less than nsslapd-maxdescriptors
        4. Verify value has been set
        5. Restart instance
    :expectedresults:
        1. Success
        2. Value of process max FD is returned
        3. Success
        4. Value of process max FD - 2 is returned
        5. Instance starts correctly
    """

    # Get the process FD limit dynamically
    max_fd_val = get_process_fd_limit(topology_st.standalone)
    log.info(f'test_reserve_descriptors_high: max_fd_val={max_fd_val}')

    # Set nsslapd-maxdescriptors to the process max value
    topology_st.standalone.config.set(FD_ATTR, str(max_fd_val))
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == str(max_fd_val)

    # Set nsslapd-reservedescriptors to 2 less than max value
    topology_st.standalone.config.set(RESRV_FD_ATTR, str(max_fd_val - 2))
    resrv_fd = topology_st.standalone.config.get_attr_val_utf8(RESRV_FD_ATTR)
    assert resrv_fd == str(max_fd_val - 2)

    # Verify instance restart
    topology_st.standalone.restart()
    assert (topology_st.standalone.status())

    log.info("test_reserve_descriptors_high PASSED")

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
