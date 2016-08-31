# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest

from lib389 import DirSrv
from lib389.cli_base import LogCapture, FakeArgs

from lib389.instance.setup import SetupDs
from lib389.instance.options import General2Base, Slapd2Base
from lib389._constants import *

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'

DEBUGGING = True

class TopologyInstance(object):
    def __init__(self, standalone, logcap):
        # For these tests, we don't want to open the instance.
        # instance.open()
        self.standalone = standalone
        self.logcap = logcap

# Need a teardown to destroy the instance.
@pytest.fixture
def topology(request):

    lc = LogCapture()
    instance = DirSrv(verbose=DEBUGGING)
    instance.log.debug("Instance allocated")
    args = {SER_PORT: INSTANCE_PORT,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    instance.allocate(args)
    if instance.exists():
        instance.delete()

    # This will need to change to instance.create in the future
    # when it's linked up!
    sds = SetupDs(verbose=DEBUGGING, dryrun=False, log=lc.log)

    # Get the dicts from Type2Base, as though they were from _validate_ds_2_config
    # IE get the defaults back just from Slapd2Base.collect
    # Override instance name, root password, port and secure port.

    general_options = General2Base(lc.log)
    general_options.verify()
    general = general_options.collect()

    # Need an args -> options2 ...
    slapd_options = Slapd2Base(lc.log)
    slapd_options.set('instance_name', INSTANCE_SERVERID)
    slapd_options.set('port', INSTANCE_PORT)
    slapd_options.set('root_password', PW_DM)
    slapd_options.verify()
    slapd = slapd_options.collect()

    sds.create_from_args(general, slapd, {}, None)
    insts = instance.list(serverid=INSTANCE_SERVERID)
    # Assert we did change the system.
    assert(len(insts) == 1)
    # Make sure we can connect
    instance.open(connOnly=True)

    def fin():
        if instance.exists() and not DEBUGGING:
            instance.delete()
    request.addfinalizer(fin)

    return TopologyInstance(instance, lc)
