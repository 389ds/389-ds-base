# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# Test the cli tools from the dsadm command for correct behaviour.

import pytest
from lib389.cli_ctl.instance import instance_list
from lib389 import DirSrv
from lib389.cli_base import LogCapture


def test_instance_list():
    lc = LogCapture()
    inst = DirSrv()
    instance_list(inst, lc.log, None)
    assert(lc.contains("No instances of Directory Server") or lc.contains("instance: "))
    # Now assert the logs in the capture.

# Need a fixture to install an instance.

# Test start

# Test stop


