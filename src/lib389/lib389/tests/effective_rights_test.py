# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from lib389._constants import *
from lib389 import DirSrv, Entry
import pytest
import logging

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'
TEST_USER = 'uid=test,%s' % DEFAULT_SUFFIX
TEST_GROUP = 'cn=testgroup,%s' % DEFAULT_SUFFIX


class TopologyInstance(object):
    def __init__(self, instance):
        instance.open()
        self.instance = instance


@pytest.fixture(scope="module")
def topology(request):
    instance = DirSrv(verbose=False)
    instance.log.debug("Instance allocated")
    args = {SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    instance.allocate(args)
    if instance.exists():
        instance.delete()
    instance.create()
    instance.open()

    def fin():
        if instance.exists():
            instance.delete()
    request.addfinalizer(fin)

    return TopologyInstance(instance)


@pytest.fixture(scope="module")
def add_user(topology):
    """Create a user entry"""

    log.info('Create a user entry: %s' % TEST_USER)
    uentry = Entry(TEST_USER)
    uentry.setValues('objectclass', 'top', 'extensibleobject')
    uentry.setValues('uid', 'test')
    topology.instance.add_s(uentry)
    # topology.instance.log.debug("Created user entry as:" ,uentry.dn)


@pytest.fixture(scope="module")
def add_group(topology):
    """Create a group for the user to have some rights to"""

    log.info('Create a group entry: %s' % TEST_GROUP)
    gentry = Entry(TEST_GROUP)
    gentry.setValues('objectclass', 'top', 'extensibleobject')
    gentry.setValues('cn', 'testgroup')
    topology.instance.add_s(gentry)


def test_effective_rights(topology, add_user, add_group):
    """Run an effective rights search
    and compare actual results with expected
    """

    log.info('Search for effective rights with get_effective_rights() '
             'function')
    result = topology.instance.get_effective_rights(TEST_USER,
                                                    filterstr='(cn=testgroup)',
                                                    attrlist=['cn'])

    rights = result[0]
    log.info('Assert that "attributeLevelRights: cn:rsc" and '
             '"entryLevelRights: v"')
    assert rights.getValue('attributeLevelRights') == 'cn:rsc'
    assert rights.getValue('entryLevelRights') == 'v'


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
