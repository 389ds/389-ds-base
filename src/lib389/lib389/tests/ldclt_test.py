# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
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

import ldap
import ldap.sasl

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'


class TopologyInstance(object):
    def __init__(self, instance):
        instance.open()
        self.instance = instance


@pytest.fixture(scope="module")
def topology(request):
    # Create the realm
    instance = DirSrv(verbose=False)
    instance.log.debug("Instance allocated")
    args = {SER_PORT: INSTANCE_PORT,
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


def test_ldctl(topology):
    """
    """
    # Batch create users
    topology.instance.ldclt.create_users('ou=People,%s' % DEFAULT_SUFFIX, max=1999)
    results = topology.instance.search_s('ou=People,%s' % DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filterstr='(objectClass=posixAccount)', attrlist=['uid'])
    assert(len(results) == 1000)

    # Run the load test for a few rounds
    topology.instance.ldclt.bind_loadtest('ou=People,%s' % DEFAULT_SUFFIX, max=1999)

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
