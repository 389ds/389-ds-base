import logging
import pytest
import os
import ldap
from lib389._constants import *
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

FD_ATTR = "nsslapd-maxdescriptors"
SYSTEMD_VAL = "16384"
CUSTOM_VAL = "9000"
TOO_HIGH_VAL = "65536"
TOO_LOW_VAL = "0"


def test_fd_limits(topology_st):
    """Test the default limits, and custom limits

    :id: fa0a5106-612f-428f-84c0-9c85c34d0433
    :setup: Standalone Instance
    :steps:
        1. Check default limit
        2. Change default limit
        3. Check invalid/too high limit is rejected
        4. Check invalid/too low limit is rejected
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4  Success
    """

    # Check systemd default
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == SYSTEMD_VAL

    # Check custom value is applied
    topology_st.standalone.config.set(FD_ATTR, CUSTOM_VAL)
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == CUSTOM_VAL

    # Attempt to use val that is too high
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology_st.standalone.config.set(FD_ATTR, TOO_HIGH_VAL)
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == CUSTOM_VAL

    # Attempt to use val that is too low
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topology_st.standalone.config.set(FD_ATTR, TOO_LOW_VAL)
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == CUSTOM_VAL

    log.info("Test PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
