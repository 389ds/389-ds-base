# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest

from lib389.utils import *
from lib389.topologies import topology_no_sample
from lib389._constants import *
from lib389.cli_base import FakeArgs
from lib389.cli_idm.initialise import initialise
from lib389.cli_base import FakeArgs
from lib389.idm.user import nsUserAccounts
from lib389.idm.group import Groups
from lib389.idm.organizationalunit import OrganizationalUnits


pytestmark = pytest.mark.tier0


DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.mark.ds4281
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_initialise(topology_no_sample):
    """Check that keep alive entries are created when initializing a master from another one

    :id: eefb59fc-4fdd-4d68-b6c4-b067eb52e881
    :setup: Standalone instance
    :steps:
        1. Create instance without sample entries
        2. Check there are no sample entries
        3. Run dsidm initialise
        4. Check there are sample entries created
        5. Run dsidm initialise again
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Exception should be raised that entries already exist
    """

    standalone = topology_no_sample.standalone

    log.info('Check there are no sample entries before running initialise')
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    groups = Groups(standalone, DEFAULT_SUFFIX)
    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    assert not users.exists('demo_user')
    assert not groups.exists('demo_group')
    assert not ous.exists('Permissions')
    assert not ous.exists('Services')

    log.info('Run dsidm initialise')
    args = FakeArgs()
    args.version = INSTALL_LATEST_CONFIG
    initialise(standalone, DEFAULT_SUFFIX, topology_no_sample.logcap.log, args)

    log.info('Check there are sample entries')
    assert users.exists('demo_user')
    assert groups.exists('demo_group')
    assert ous.exists('Permissions')
    assert ous.exists('Services')

    log.info('Try to initialise again and exception should be raised')
    with pytest.raises(ldap.ALREADY_EXISTS):
        initialise(standalone, DEFAULT_SUFFIX, topology_no_sample.logcap.log, args)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
