# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest

from lib389.cli_conf.directory_manager import password_change

from lib389.cli_base import LogCapture, FakeArgs
from lib389.tests.cli import topology

# Topology is pulled from __init__.py
def test_directory_manager(topology):
    # 
    args = FakeArgs()
    args.password = 'password'
    password_change(topology.standalone, None, topology.logcap.log, args)

