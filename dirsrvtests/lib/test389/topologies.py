# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
# Compatibility shim: on main branch, test389.topologies is the real module.
# On older branches lib389.topologies is the real module.
# This re-export lets backported tests using "from test389.topologies import ..."
# work without modification.

from lib389.topologies import *  # noqa: F401,F403
from lib389.topologies import TopologyMain, create_topology, _remove_ssca_db  # noqa: F401
