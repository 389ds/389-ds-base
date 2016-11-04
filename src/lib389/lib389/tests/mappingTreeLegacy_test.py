# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import pytest
import logging
from lib389._constants import *
from lib389.properties import *
from lib389 import DirSrv, InvalidArgumentError

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_REPL_DN = "uid=test,%s" % DEFAULT_SUFFIX
INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'
INSTANCE_BACKUP = os.environ.get('BACKUPDIR', DEFAULT_BACKUPDIR)
NEW_SUFFIX_1 = 'o=test_create'
NEW_BACKEND_1 = 'test_createdb'
NEW_CHILDSUFFIX_1 = 'o=child1,o=test_create'
NEW_CHILDBACKEND_1 = 'test_createchilddb'
NEW_SUFFIX_2 = 'o=test_bis_create'
NEW_BACKEND_2 = 'test_bis_createdb'
NEW_CHILDSUFFIX_2 = 'o=child2,o=test_bis_create'
NEW_CHILDBACKEND_2 = 'test_bis_createchilddb'


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
        instance.delete()
    request.addfinalizer(fin)

    return TopologyInstance(instance)


def test_legacy_list(topology):
    """This test list with only no param, suffix,
    bename, suffix+bename (also with invalid values)
    """

    ents = topology.instance.mappingtree.list()
    for ent in ents:
            log.info('test_list: %r' % ent)

    log.info("get with a dedicated suffix")
    ents = topology.instance.mappingtree.list(suffix=DEFAULT_SUFFIX)
    assert len(ents) == 1
    log.info('test_list(suffix): %r' % ent)

    ents = topology.instance.mappingtree.list(suffix="dc=dummy")
    assert len(ents) == 0

    log.info("get with a dedicated backend name")
    ents = topology.instance.mappingtree.list(bename=DEFAULT_BENAME)
    assert len(ents) == 1
    log.info('test_list(bename): %r' % ent)

    ents = topology.instance.mappingtree.list(bename="dummy")
    assert len(ents) == 0

    log.info("check backend is taken first")
    ents = topology.instance.mappingtree.list(suffix="dc=dummy",
                                              bename=DEFAULT_BENAME)
    assert len(ents) == 1
    log.info('test_list(suffix, bename): %r' % ent)


def test_legacy_create(topology):
    """This test will create 2 backends/mapping trees,
    then 2 childs backend/mapping tree
    """

    log.info("before creating mapping trees, the backends must exists")
    backendEntry = topology.instance.backend.create(
        NEW_SUFFIX_1, {BACKEND_NAME: NEW_BACKEND_1})
    backendEntry = topology.instance.backend.create(
        NEW_SUFFIX_2, {BACKEND_NAME: NEW_BACKEND_2})

    ents = topology.instance.mappingtree.list()
    nb_mappingtree = len(ents)

    log.info("create a first additional mapping tree")
    topology.instance.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree + 1)

    log.info("create a second additional mapping tree")
    topology.instance.mappingtree.create(NEW_SUFFIX_2, bename=NEW_BACKEND_2)
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree + 2)

    log.info("Creating a mapping tree that already exists => "
             "it just returns the existing MT")
    topology.instance.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree + 2)

    log.info("Creating a mapping tree that already exists => "
             "it just returns the existing MT")
    topology.instance.mappingtree.create(NEW_SUFFIX_2, bename=NEW_BACKEND_2)
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree + 2)

    log.info("before creating mapping trees, the backends must exists")
    backendEntry = topology.instance.backend.create(
        NEW_CHILDSUFFIX_1, {BACKEND_NAME: NEW_CHILDBACKEND_1})
    backendEntry = topology.instance.backend.create(
        NEW_CHILDSUFFIX_2, {BACKEND_NAME: NEW_CHILDBACKEND_2})

    log.info("create a third additional mapping tree, that is child "
             "of NEW_SUFFIX_1")
    topology.instance.mappingtree.create(
        NEW_CHILDSUFFIX_1, bename=NEW_CHILDBACKEND_1, parent=NEW_SUFFIX_1)
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree + 3)

    ents = topology.instance.mappingtree.list(suffix=NEW_CHILDSUFFIX_1)
    assert len(ents) == 1
    ent = ents[0]
    assert ent.hasAttr(MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX]) and \
        (ent.getValue(MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX]) ==
         NEW_SUFFIX_1)

    log.info("create a fourth additional mapping tree, that is child "
             "of NEW_SUFFIX_2")
    topology.instance.mappingtree.create(NEW_CHILDSUFFIX_2,
                                         bename=NEW_CHILDBACKEND_2,
                                         parent=NEW_SUFFIX_2)
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree + 4)

    ents = topology.instance.mappingtree.list(suffix=NEW_CHILDSUFFIX_2)
    assert len(ents) == 1
    ent = ents[0]
    assert ent.hasAttr(MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX]) and \
        (ent.getValue(MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX]) ==
         NEW_SUFFIX_2)


def test_legacy_delete(topology):
    """Delete the mapping tree and check the remaining number.
    Delete the sub-suffix first.
    """

    ents = topology.instance.mappingtree.list()
    nb_mappingtree = len(ents)
    deleted = 0

    log.debug("delete MT for suffix " + NEW_CHILDSUFFIX_1)
    topology.instance.mappingtree.delete(suffix=NEW_CHILDSUFFIX_1)
    deleted += 1
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree - deleted)

    log.debug("delete MT with backend " + NEW_CHILDBACKEND_2)
    topology.instance.mappingtree.delete(bename=NEW_CHILDBACKEND_2)
    deleted += 1
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree - deleted)

    log.debug("delete MT for suffix %s and with backend %s" %
              (NEW_SUFFIX_1, NEW_BACKEND_1))
    topology.instance.mappingtree.delete(suffix=NEW_SUFFIX_1,
                                         bename=NEW_BACKEND_1)
    deleted += 1
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree - deleted)

    ents = topology.instance.mappingtree.list(suffix=NEW_SUFFIX_2)
    assert len(ents) == 1
    log.debug("delete MT with DN %s (dummy suffix/backend)" % (ents[0].dn))
    topology.instance.mappingtree.delete(suffix="o=dummy", bename="foo",
                                         name=ents[0].dn)


def test_legacy_getProperties(topology):
    """Create one additional mapping tree
    and try to get properties from it
    """

    ents = topology.instance.mappingtree.list()
    nb_mappingtree = len(ents)

    log.info("create a first additional mapping tree")
    topology.instance.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
    ents = topology.instance.mappingtree.list()
    assert len(ents) == (nb_mappingtree + 1)

    log.info("check we can get properties from suffix")
    prop_ref = topology.instance.mappingtree.getProperties(suffix=NEW_SUFFIX_1)
    log.info("properties [suffix] %s: %r" % (NEW_SUFFIX_1, prop_ref))

    log.info("check we can get properties from backend name")
    properties = topology.instance.mappingtree.getProperties(
        bename=NEW_BACKEND_1)
    for key in list(properties.keys()):
        assert prop_ref[key] == properties[key]
    log.info("properties [backend] %s: %r" % (NEW_SUFFIX_1, properties))

    log.info("check we can get properties from suffix AND backend name")
    properties = topology.instance.mappingtree.getProperties(
        suffix=NEW_SUFFIX_1, bename=NEW_BACKEND_1)
    for key in list(properties.keys()):
        assert prop_ref[key] == properties[key]
    log.info("properties [suffix+backend]%s: %r" % (NEW_SUFFIX_1, properties))

    log.info("check we can get properties from MT entry")
    ents = topology.instance.mappingtree.list(suffix=NEW_SUFFIX_1)
    assert len(ents) == 1
    ent = ents[0]
    properties = topology.instance.mappingtree.getProperties(name=ent.dn)
    for key in list(properties.keys()):
        assert prop_ref[key] == properties[key]
    log.info("properties [MT entry DN] %s: %r" % (NEW_SUFFIX_1, properties))

    log.info("check we can get only one properties")
    properties = topology.instance.mappingtree.getProperties(
        name=ent.dn, properties=[MT_STATE])
    assert len(properties) == 1
    assert properties[MT_STATE] == prop_ref[MT_STATE]

    with pytest.raises(KeyError):
        properties = topology.instance.mappingtree.getProperties(
            name=ent.dn, properties=['dummy'])


def test_legacy_toSuffix(topology):
    """Try to get suffix name from existing mapping tree"""

    ents = topology.instance.mappingtree.list()
    log.info("check we can get suffix from a mapping tree by entry")
    suffix = topology.instance.mappingtree.toSuffix(entry=ents[0])
    assert len(suffix) > 0
    log.info("suffix (entry) is %s" % suffix[0])

    log.info("check we can get suffix from a mapping tree by name")
    suffix = topology.instance.mappingtree.toSuffix(name=ents[0].dn)
    assert len(suffix) > 0
    log.info("suffix (dn) is %s" % suffix[0])

    log.info("check we can not get suffix from a mapping tree by nothing")
    with pytest.raises(InvalidArgumentError):
        suffix = topology.instance.mappingtree.toSuffix()


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
