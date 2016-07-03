# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import pytest
from lib389._constants import *
from lib389.properties import *
from lib389 import DirSrv


TEST_REPL_DN = "uid=test,%s" % DEFAULT_SUFFIX
INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'
# INSTANCE_PREFIX = os.environ.get('PREFIX', None)
INSTANCE_PREFIX = None
INSTANCE_BACKUP = os.environ.get('BACKUPDIR', DEFAULT_BACKUPDIR)
NEW_SUFFIX_1 = 'o=test_create'
NEW_BACKEND_1 = 'test_createdb'
NEW_CHILDSUFFIX_1 = 'o=child1,o=test_create'
NEW_CHILDBACKEND_1 = 'test_createchilddb'
NEW_SUFFIX_2 = 'o=test_bis_create'
NEW_BACKEND_2 = 'test_bis_createdb'
NEW_CHILDSUFFIX_2 = 'o=child2,o=test_bis_create'
NEW_CHILDBACKEND_2 = 'test_bis_createchilddb'


class TopologyStandalone(object):
    def __init__(self, instance):
        instance.open()
        self.instance = instance


@pytest.fixture(scope="module")
def topology(request):
    instance = DirSrv(verbose=False)
    instance.log.debug("Instance allocated")
    args = {SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
            SER_DEPLOYED_DIR: INSTANCE_PREFIX,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    instance.allocate(args)
    if instance.exists():
        instance.delete()
    instance.create()
    instance.open()

    def fin():
        instance.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(instance)


def test_list(topology):
    # before creating mapping trees, the backends must exists
    backendEntry = topology.instance.backend.create(
        NEW_SUFFIX_1, {BACKEND_NAME: NEW_BACKEND_1})

    ents = topology.instance.mappingtree.list()
    nb_mappingtree = len(ents)

    # create a first additional mapping tree
    topology.instance.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree + 1)

    suffixes = topology.instance.suffix.list()
    assert len(suffixes) == 2
    for suffix in suffixes:
        topology.instance.log.info("suffix is %s" % suffix)


def test_toBackend(topology):
    backends = topology.instance.suffix.toBackend(suffix=NEW_SUFFIX_1)
    assert len(backends) == 1
    backend = backends[0]
    topology.instance.log.info("backend entry is %r" % backend)
    assert backend.getValue(
        BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX]) == NEW_SUFFIX_1
    assert backend.getValue(
        BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME]) == NEW_BACKEND_1


def test_getParent(topology):
    parent = topology.instance.suffix.getParent(suffix=NEW_SUFFIX_1)
    assert parent is None

    backendEntry = topology.instance.backend.create(
        NEW_CHILDSUFFIX_1, {BACKEND_NAME: NEW_CHILDBACKEND_1})
    # create a third additional mapping tree, that is child of NEW_SUFFIX_1
    topology.instance.mappingtree.create(
        NEW_CHILDSUFFIX_1, bename=NEW_CHILDBACKEND_1, parent=NEW_SUFFIX_1)
    parent = topology.instance.suffix.getParent(suffix=NEW_CHILDSUFFIX_1)
    topology.instance.log.info("Retrieved parent of %s:  %s" % (NEW_CHILDSUFFIX_1, parent))
    assert parent == NEW_SUFFIX_1


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -vv %s" % CURRENT_FILE)
