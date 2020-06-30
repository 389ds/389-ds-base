# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import io
import sys
import pytest
import time
from lib389.cli_conf.chaining import (config_get, config_set, def_config_get, def_config_set,
                                      create_link, get_link, edit_link, delete_link,
                                      monitor_link, list_links)
from lib389.cli_conf.backend import (backend_create, backend_delete, backend_list)
from lib389.cli_base import LogCapture, FakeArgs
from lib389.tests.cli import check_output
from lib389.topologies import topology_st

from lib389.utils import ds_is_older
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")

LINK_NAME = "mylink"
LINK_SUFFIX = "ou=link,dc=chaining,dc=test"
SUFFIX = "dc=chaining,dc=test"
BE_NAME = "chainingRoot"
stdout = sys.__stdout__


@pytest.fixture(scope="function")
def create_backend(topology_st, request):
    """Create backend "dc=backend,dc=test" / backendRoot
    """
    sys.stdout = io.StringIO()

    args = FakeArgs()
    args.cn = BE_NAME
    args.be_name = BE_NAME
    args.suffix = False
    args.nsslapd_suffix = SUFFIX
    args.skip_subsuffixes = False
    args.json = False
    args.parent_suffix = False
    args.create_entries = True
    args.suffix = SUFFIX
    backend_create(topology_st.standalone, None, None, args)
    check_output("The database was successfully created")

    def fin():
        sys.stdout = io.StringIO()

        args = FakeArgs()
        args.cn = BE_NAME
        args.be_name = BE_NAME
        args.suffix = SUFFIX
        args.skip_subsuffixes = False
        args.json = False

        # Delete backend
        backend_delete(topology_st.standalone, None, None, args, warn=False)
        check_output("successfully deleted")

        # Verify it's removed
        args.suffix = False
        backend_list(topology_st.standalone, None, None, args)
        check_output(BE_NAME, missing=True)

    request.addfinalizer(fin)


def test_chaining_cli(topology_st, create_backend):
    """Test creating, listing, getting, and deleting a backend (and subsuffix)

    :id: 800f432a-52ab-4661-ac66-a2bdd9b984d7
    :setup: Standalone instance
    :steps:
        1. Update config controls and components
        2. Verify update to config
        3. Set default config
        4. Verify update to default config
        5. Add DB Link
        6. Verify Link was created
        7. Edit Link
        8. Verify edit to link
        9. Test monitor
        10. Delete link
        11. Verify link was deleted
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
    """
    topology_st.logcap = LogCapture()
    sys.stdout = io.StringIO()
    args = FakeArgs()
    args.CHAIN_NAME = [LINK_NAME]
    args.suffix = LINK_SUFFIX
    args.json = False
    args.add_control = None
    args.del_control = None
    args.add_comp = None
    args.del_comp = None

    # Set config (add control)
    args.add_control = '1.1.1.1.1.1.1'
    config_set(topology_st.standalone, None, None, args)
    args.add_control = None
    check_output("updated chaining configuration")

    # Verify config change
    config_get(topology_st.standalone, None, None, args)
    check_output("1.1.1.1.1.1.1")

    # Set config (delete control)
    args.del_control = '1.1.1.1.1.1.1'
    config_set(topology_st.standalone, None, None, args)
    args.del_control = None
    check_output("updated chaining configuration")

    # Verify config change
    config_get(topology_st.standalone, None, None, args)
    check_output("1.1.1.1.1.1.1", missing=True)

    # Set config (add comp)
    args.add_comp = 'cn=test,cn=config'
    config_set(topology_st.standalone, None, None, args)
    args.add_comp = None
    check_output("updated chaining configuration")

    # Verify config change
    config_get(topology_st.standalone, None, None, args)
    check_output('cn=test,cn=config')

    # Set config (delete comp)
    args.del_comp = 'cn=test,cn=config'
    config_set(topology_st.standalone, None, None, args)
    args.del_comp = None
    check_output("updated chaining configuration")

    # Verify config change
    config_get(topology_st.standalone, None, None, args)
    check_output("cn=test,cn=config", missing=True)

    # Set default config
    args.time_limit = '5555'
    def_config_set(topology_st.standalone, None, None, args)
    check_output("updated chaining default instance creation configuration")

    # Verify default config change
    def_config_get(topology_st.standalone, None, None, args)
    check_output("nsslapd_timelimit: 5555")

    # Create database link
    args.server_url = "ldap://localhost.localdomain"
    args.bind_dn = "cn=link_admin," + SUFFIX
    args.bind_pw = "secret_157"
    args.bind_mech = "LDAP"
    create_link(topology_st.standalone, None, None, args)
    check_output("created database link")

    # Verify link was created
    list_links(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output(LINK_NAME)

    # Edit link
    args.bind_dn = "uid=newuser,cn=config"
    args.suffix = None
    edit_link(topology_st.standalone, None, None, args)
    check_output("updated database chaining link")

    # Verify link was edited
    args.cn = LINK_NAME
    get_link(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("uid=newuser,cn=config")

    # Test monitor
    time.sleep(2)  # need time for link to start up and generate monitor
    monitor_link(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("nssearchonelevelcount: ")

    # Delete link
    delete_link(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("deleted database link")

    # Verify link was deleted
    list_links(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output(LINK_NAME, missing=True)

    # Done!
