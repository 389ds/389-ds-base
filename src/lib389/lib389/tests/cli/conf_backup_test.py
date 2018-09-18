# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import pytest
import shutil

from lib389.cli_conf.backup import backup_create, backup_restore
from lib389.cli_base import LogCapture, FakeArgs
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_st
from lib389.utils import ds_is_older
from lib389._constants import DEFAULT_SUFFIX
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")


def test_basic(topology_st):
    BACKUP_DIR = os.path.join(topology_st.standalone.ds_paths.backup_dir, "basic_backup")
    topology_st.logcap = LogCapture()
    args = FakeArgs()

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user()
    user.replace("description", "backup_test")

    # Clean the backup dir first
    if os.path.exists(BACKUP_DIR):
        shutil.rmtree(BACKUP_DIR)

    # Create the backup
    args.archive = BACKUP_DIR
    args.db_type = None
    backup_create(topology_st.standalone, None, topology_st.logcap.log, args)
    assert os.listdir(BACKUP_DIR)

    # Restore the backup
    args.archive = topology_st.standalone.ds_paths.backup_dir
    args.db_type = None
    backup_restore(topology_st.standalone, None, topology_st.logcap.log, args)
    assert user.present("description", "backup_test")

    # No error has happened! Done!
    # Clean up
    if os.path.exists(BACKUP_DIR):
        shutil.rmtree(BACKUP_DIR)
