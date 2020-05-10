# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


import sys
import pytest
from lib389 import DirSrv
from lib389.cli_base import LogCapture
from lib389.instance.setup import SetupDs
from lib389.instance.remove import remove_ds_instance
from lib389.instance.options import General2Base, Slapd2Base
from lib389._constants import *
from lib389.utils import ds_is_older

import tempfile

pytestmark = [pytest.mark.tier0,
              pytest.mark.skipif(ds_is_older('1.4.1.2'), reason="Needs a compatible systemd unit, see PR#50213")]

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'

DEBUGGING = True

MAJOR, MINOR, _, _, _ = sys.version_info

class TopologyInstance(object):
    def __init__(self, standalone):
        # For these tests, we don't want to open the instance.
        # instance.open()
        self.standalone = standalone

# Need a teardown to destroy the instance.
@pytest.fixture
def topology(request):
    instance = DirSrv(verbose=DEBUGGING)
    instance.log.debug("Instance allocated")
    args = {SER_PORT: INSTANCE_PORT,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    instance.allocate(args)
    if instance.exists():
        instance.delete()

    def fin():
        if instance.exists() and not DEBUGGING:
            instance.delete()
    request.addfinalizer(fin)

    return TopologyInstance(instance)

def test_setup_ds_minimal_dry(topology):
    # Unset PYTHONPATH to avoid mixing old CLI tools and new lib389
    tmp_env = os.environ
    if "PYTHONPATH" in tmp_env:
        del tmp_env["PYTHONPATH"]

    # Create the setupDs
    lc = LogCapture()
    # Give it the right types.
    sds = SetupDs(verbose=DEBUGGING, dryrun=True, log=lc.log)

    # Get the dicts from Type2Base, as though they were from _validate_ds_2_config
    # IE get the defaults back just from Slapd2Base.collect
    # Override instance name, root password, port and secure port.

    general_options = General2Base(lc.log)
    general_options.verify()
    general = general_options.collect()

    slapd_options = Slapd2Base(lc.log)
    slapd_options.set('instance_name', INSTANCE_SERVERID)
    slapd_options.set('port', INSTANCE_PORT)
    slapd_options.set('root_password', PW_DM)
    slapd_options.verify()
    slapd = slapd_options.collect()

    sds.create_from_args(general, slapd, {}, None)

    insts = topology.standalone.list(serverid=INSTANCE_SERVERID)
    # Assert we did not change the system.
    assert(len(insts) == 0)

def test_setup_ds_minimal(topology):
    # Create the setupDs
    lc = LogCapture()
    # Give it the right types.
    sds = SetupDs(verbose=DEBUGGING, dryrun=False, log=lc.log)

    # Get the dicts from Type2Base, as though they were from _validate_ds_2_config
    # IE get the defaults back just from Slapd2Base.collect
    # Override instance name, root password, port and secure port.

    general_options = General2Base(lc.log)
    general_options.verify()
    general = general_options.collect()

    slapd_options = Slapd2Base(lc.log)
    slapd_options.set('instance_name', INSTANCE_SERVERID)
    slapd_options.set('port', INSTANCE_PORT)
    slapd_options.set('root_password', PW_DM)
    slapd_options.verify()
    slapd = slapd_options.collect()

    sds.create_from_args(general, slapd, {}, None)
    insts = topology.standalone.list(serverid=INSTANCE_SERVERID)
    # Assert we did change the system.
    assert(len(insts) == 1)
    # Make sure we can connect
    topology.standalone.open()
    # Make sure we can start stop.
    topology.standalone.stop()
    topology.standalone.start()
    # Okay, actually remove the instance
    remove_ds_instance(topology.standalone)


def test_setup_ds_inf_minimal(topology):
    if MAJOR < 3:
        return
    # Write a template inf
    # Check it?
    # Setup the server

    pass

