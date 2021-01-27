# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest
import logging
import os

from lib389 import DEFAULT_SUFFIX
from lib389.cli_idm.organizationalunit import get, get_dn, create, modify, delete, list, rename
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older
from lib389.idm.organizationalunit import OrganizationalUnits
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def create_test_ou(topology_st, request):
    log.info('Create organizational unit')
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    test_ou = ous.create(properties={
        'ou': 'toDelete',
        'description': 'Test OU',
    })

    def fin():
        log.info('Delete organizational unit')
        if test_ou.exists():
            test_ou.delete()

    request.addfinalizer(fin)


@pytest.mark.bz1866294
@pytest.mark.ds4284
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
@pytest.mark.xfail(ds_is_older("1.4.3.16"), reason="Might fail because of bz1866294")
def test_dsidm_organizational_unit_delete(topology_st, create_test_ou):
    """ Test dsidm organizationalunit delete

    :id: 5d35a5ee-85c2-4b83-9101-938ba7732ccd
    :customerscenario: True
    :setup: Standalone instance
    :steps:
         1. Run dsidm organizationalunit delete
         2. Check the ou is deleted
    :expectedresults:
         1. Success
         2. Entry is deleted
    """

    standalone = topology_st.standalone
    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    test_ou = ous.get('toDelete')
    delete_value = 'Successfully deleted {}'.format(test_ou.dn)

    args = FakeArgs()
    args.dn = test_ou.dn

    log.info('Test dsidm organizationalunit delete')
    delete(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=delete_value)

    log.info('Check the entry is deleted')
    assert not test_ou.exists()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
