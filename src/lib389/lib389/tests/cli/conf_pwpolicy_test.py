# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import io
import sys
import pytest

from lib389.cli_conf.pwpolicy import (create_user_policy, create_subtree_policy,
                                      list_policies, set_local_policy,
                                      get_local_policy, del_local_policy,
                                      get_global_policy, set_global_policy)

from lib389.cli_base import LogCapture, FakeArgs
from lib389.tests.cli import check_output
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389._constants import (DEFAULT_SUFFIX)
from lib389.utils import ds_is_older

pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")

USER_DN = "uid=testuser,ou=people,{}".format(DEFAULT_SUFFIX)
USER_OUTPUT = "{} (user policy)".format(USER_DN)
OU_DN = "ou=people,{}".format(DEFAULT_SUFFIX)
OU_OUTPUT = "{} (subtree policy)".format(OU_DN)


@pytest.fixture(scope="function")
def test_args(dn):
    args = FakeArgs()
    args.suffix = False
    args.json = False
    args.verbose = False
    args.DN = [dn]
    return args


@pytest.fixture(scope="function")
def do_setup(topology_st, request):
    """Create a user and make sure ou=pople exists
    """
    sys.stdout = io.StringIO()

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    users.ensure_state(properties=TEST_USER_PROPERTIES)

    ou = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    ou.ensure_state(properties={'ou': 'people'})


def test_pwp_cli(topology_st, do_setup):
    """Test creating, listing, getting, and deleting a backend (and subsuffix)

    :id: 800f432a-52ab-4661-ac66-a2bdd9b984da
    :setup: Standalone instance
    :steps:
        1. Create User policy
        2. Create Subtree policy
        3. List policies
        4. Set user policy
        5. Get user policy
        6. Set subtree policy
        7. Get subtree policy
        8. Delete user policy
        9. Delete subtree policy
        10. List local policies - make sure none are returned
        11. Get global policy
        12. Set global policy
        13. Verify global policy update
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
        10. Success
        11. Success
        12. Success
        13. Success
    """
    topology_st.logcap = LogCapture()
    sys.stdout = io.StringIO()

    # Create User Policy
    args = test_args(USER_DN)
    args.pwdchange = 'on'
    create_user_policy(topology_st.standalone, None, topology_st.logcap.log, args)

    # Create Subtree Policy
    args = test_args(OU_DN)
    args.pwdchange = 'off'
    create_subtree_policy(topology_st.standalone, None, topology_st.logcap.log, args)

    # List policies
    args = test_args(DEFAULT_SUFFIX)
    list_policies(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output([USER_OUTPUT, OU_OUTPUT])

    # Set User Policy
    args = test_args(USER_DN)
    args.pwdhistory = 'on'
    set_local_policy(topology_st.standalone, None, topology_st.logcap.log, args)

    # Get User Policy
    args = test_args(USER_DN)
    get_local_policy(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("passwordHistory: on")

    # Set Subtree Policy
    args = test_args(OU_DN)
    args.pwdexpire = 'on'
    set_local_policy(topology_st.standalone, None, topology_st.logcap.log, args)

    # Get Subtree Policy
    args = test_args(OU_DN)
    get_local_policy(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("passwordExp: on")

    # Delete User Policy (and verify)
    args = test_args(USER_DN)
    del_local_policy(topology_st.standalone, None, topology_st.logcap.log, args)

    with pytest.raises(ValueError):
        get_local_policy(topology_st.standalone, None, topology_st.logcap.log, args)

    # Delete Subtree Policy (and verify)
    args = test_args(OU_DN)
    del_local_policy(topology_st.standalone, None, topology_st.logcap.log, args)

    with pytest.raises(ValueError):
        get_local_policy(topology_st.standalone, None, topology_st.logcap.log, args)

    # List policies (or lack there of)
    args = test_args(DEFAULT_SUFFIX)
    list_policies(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output([USER_OUTPUT, OU_OUTPUT], missing=True)

    # Get global policy
    args = test_args(DEFAULT_SUFFIX)
    get_global_policy(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output('passwordLockout: off')

    # Set global policy
    args = test_args(DEFAULT_SUFFIX)
    args.pwdlockout = "on"
    set_global_policy(topology_st.standalone, None, topology_st.logcap.log, args)

    # Check update was applied
    get_global_policy(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output('passwordLockout: on')
