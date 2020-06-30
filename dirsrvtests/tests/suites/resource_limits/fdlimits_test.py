import logging
import pytest
import os
import ldap
import resource
from lib389._constants import *
from lib389.topologies import topology_st
from lib389.utils import ds_is_older, ensure_str
from subprocess import check_output

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

FD_ATTR = "nsslapd-maxdescriptors"
GLOBAL_LIMIT = resource.getrlimit(resource.RLIMIT_NOFILE)[1]
SYSTEMD_LIMIT = ensure_str(check_output("systemctl show -p LimitNOFILE dirsrv@standalone1".split(" ")).strip()).split('=')[1]
CUSTOM_VAL = str(int(SYSTEMD_LIMIT) - 10)
TOO_HIGH_VAL = str(GLOBAL_LIMIT * 2)
TOO_HIGH_VAL2 = str(int(SYSTEMD_LIMIT) * 2)
TOO_LOW_VAL = "0"

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

    # Check systemd default
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == SYSTEMD_LIMIT

    # Check custom value is applied
    topology_st.standalone.config.set(FD_ATTR, CUSTOM_VAL)
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == CUSTOM_VAL

    # # Attempt to use value that is higher than the global system limit
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology_st.standalone.config.set(FD_ATTR, TOO_HIGH_VAL)
    max_fd = topology_st.standalone.config.get_attr_val_utf8(FD_ATTR)
    assert max_fd == CUSTOM_VAL

    # Attempt to use value that is higher than the value defined in the systemd service
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology_st.standalone.config.set(FD_ATTR, TOO_HIGH_VAL2)
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
