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
import ldap

from lib389._constants import *
from lib389.properties import *
from lib389 import DirSrv, InvalidArgumentError, UnwillingToPerformError

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_REPL_DN = "uid=test,%s" % DEFAULT_SUFFIX
INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'
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

DEBUGGING = False


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    standalone = DirSrv(verbose=DEBUGGING)
    standalone.log.debug("Instance allocated")
    args = {SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
            SER_DEPLOYED_DIR: INSTANCE_PREFIX,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    standalone.allocate(args)
    if standalone.exists():
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        if not DEBUGGING:
            standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def test_list(topology):
    """Test backend.list() function behaviour after:
    - creating new suffixes
    - filter with suffix
    - filter with backend name (bename)
    - filter with backend dn
    - filter with invalid suffix/bename
    """

    ents = topology.standalone.backends.list()
    nb_backend = len(ents)
    for ent in ents:
        topology.standalone.log.info("List(%d): backend %s" %
                                     (nb_backend, ent.dn))

    log.info("Create a first backend and check list all backends")
    topology.standalone.backends.create(
        None, properties={
            BACKEND_NAME: NEW_BACKEND_1,
            'suffix': NEW_SUFFIX_1,
        })

    ents = topology.standalone.backends.list()
    for ent in ents:
        topology.standalone.log.info("List(%d): backend %s" %
                                     (nb_backend + 1, ent.dn))
    assert len(ents) == (nb_backend + 1)

    log.info("Create a second backend and check list all backends")
    topology.standalone.backends.create(
        None, properties={
            BACKEND_NAME: NEW_BACKEND_2,
            'suffix': NEW_SUFFIX_2,
         })

    ents = topology.standalone.backends.list()
    for ent in ents:
        topology.standalone.log.info("List(%d): backend %s" %
                                     (nb_backend + 2, ent.dn))
    assert len(ents) == (nb_backend + 2)

    log.info("Check list a backend per suffix")
    # In the new api this becomes a get over the selector type
    # In the case this fails, this actually throws exception
    ent = topology.standalone.backends.get(NEW_SUFFIX_1)
    assert ent is not None
    topology.standalone.log.info("List suffix (%d): backend %s" %
                                 (1, ent.dn))

    log.info("Check list a backend by its name")
    ent = topology.standalone.backends.get(NEW_BACKEND_2)
    assert ent is not None
    topology.standalone.log.info("List name (%d): backend %s" %
                                 (1, ent.dn))

    log.info("Check get backends by their DN")
    ents = topology.standalone.backends.get(dn=ent.dn)
    assert ents is not None
    topology.standalone.log.info("List DN (%d): backend %s" % (1, ents.dn))

    # The new backends api just does selection on a single attr, and
    # You would then validate the other attributes on retrival.
    # But what would really be a case for this? If you know the suffix, get
    # that. If you know the name, get that. Why both?
    #  log.info("Check list with valid backend DN but invalid suffix/bename")
    #  all = topology.standalone.backend.list()
    #  for ent in all:
    #      ents = topology.standalone.backend.list(suffix="o=dummy",
    #                                              backend_dn=ent.dn,
    #                                              bename="dummydb")
    #      for bck in ents:
    #          topology.standalone.log.info("List invalid suffix+bename "
    #                                       "(%d): backend %s" % (1, bck.dn))
    #      assert len(ents) == 1

    log.info("Just to make it clean in the end")
    b1 = topology.standalone.backends.get(NEW_SUFFIX_1)
    b2 = topology.standalone.backends.get(NEW_SUFFIX_2)
    b1.delete()
    b2.delete()


def test_create(topology):
    """Test backend.create() function with:
    - specifying no suffix
    - specifying already existing backend suffix
    - specifying already existing backend name, but new suffix
    - specifying no properties
    """

    log.info("Create a backend")
    topology.standalone.backend.create(suffix=NEW_SUFFIX_1,
                                       properties={BACKEND_NAME:
                                                   NEW_BACKEND_1})

    log.info("Check behaviour with missing suffix")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM) as excinfo:
        topology.standalone.backend.create()
    assert 'Missing Suffix' in str(excinfo.value)

    log.info("Check behaviour with already existing backend for that suffix")
    with pytest.raises(ldap.ALREADY_EXISTS):
        topology.standalone.backend.create(suffix=NEW_SUFFIX_1)

    log.info("Check behaviour with already existing backend DN, "
             "but new suffix")
    with pytest.raises(ldap.ALREADY_EXISTS) as excinfo:
        topology.standalone.backend.create(suffix=NEW_SUFFIX_2,
                                           properties={BACKEND_NAME:
                                                       NEW_BACKEND_1})
    assert 'Backend already exists' in str(excinfo.value)

    log.info("Create a backend without properties")
    topology.standalone.backend.create(suffix=NEW_SUFFIX_2)
    ents = topology.standalone.backend.list(suffix=NEW_SUFFIX_2)
    assert len(ents) == 1

    log.info("Just to make it clean in the end")
    topology.standalone.backend.delete(suffix=NEW_SUFFIX_1)
    topology.standalone.backend.delete(suffix=NEW_SUFFIX_2)


def test_delete_valid(topology):
    """Test the various possibilities to delete a backend:
    - with suffix
    - with backend name
    - with backend DN
    """

    ents = topology.standalone.backend.list()
    nb_backend = len(ents)

    log.info("Try to delete a backend with suffix")
    topology.standalone.backend.create(suffix=NEW_SUFFIX_1,
                                       properties={BACKEND_NAME:
                                                   NEW_BACKEND_1})
    topology.standalone.backend.delete(suffix=NEW_SUFFIX_1)
    ents = topology.standalone.backend.list()
    assert len(ents) == nb_backend

    log.info("Try to delete a backend with backend name")
    topology.standalone.backend.create(suffix=NEW_SUFFIX_1,
                                       properties={BACKEND_NAME:
                                                   NEW_BACKEND_1})
    topology.standalone.backend.delete(bename=NEW_BACKEND_1)
    ents = topology.standalone.backend.list()
    assert len(ents) == nb_backend

    log.info("Try to delete a backend with backend DN")
    topology.standalone.backend.create(suffix=NEW_SUFFIX_1,
                                       properties={BACKEND_NAME:
                                                   NEW_BACKEND_1})
    ents = topology.standalone.backend.list(suffix=NEW_SUFFIX_1)
    assert len(ents) == 1
    topology.standalone.backend.delete(backend_dn=ents[0].dn)
    ents = topology.standalone.backend.list()
    assert len(ents) == nb_backend


def test_delete_invalid(topology):
    """Test the invalid situations with the backend removal:
    - no argument
    - invalid suffix
    - existing a mapping tree
    - backend name differs
    """

    topology.standalone.backend.create(suffix=NEW_SUFFIX_1,
                                       properties={BACKEND_NAME:
                                                   NEW_BACKEND_1})
    topology.standalone.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)

    log.info("First no argument -> UNWILLING_TO_PERFORM")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM) as excinfo:
        topology.standalone.backend.delete()
    assert 'suffix and backend DN and backend name are missing' in \
        str(excinfo.value)

    log.info("Second invalid suffix -> UNWILLING_TO_PERFORM")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM) as excinfo:
        topology.standalone.backend.delete(suffix=NEW_SUFFIX_2)
    assert 'Unable to retrieve the backend' in str(excinfo.value)

    topology.standalone.mappingtree.delete(suffix=NEW_SUFFIX_1,
                                            bename=NEW_BACKEND_1)

    log.info("Backend name differs -> UNWILLING_TO_PERFORM")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology.standalone.backend.delete(suffix=NEW_SUFFIX_1, bename='dummydb')
    topology.standalone.backend.delete(suffix=NEW_SUFFIX_1)


def test_toSuffix(topology):
    """Test backend.toSuffix() function
    by comparing its result to the true value
    """

    log.info("Create one backend")
    topology.standalone.backend.create(suffix=NEW_SUFFIX_1,
                                       properties={BACKEND_NAME:
                                                   NEW_BACKEND_1})

    log.info("Run through all backends and compare backend.toSuffix() "
             "function results with true values taken from attributes")
    ents = topology.standalone.backend.list()
    for ent in ents:
        suffix = ent.getValues(BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX])
        values = topology.standalone.backend.toSuffix(name=ent.dn)
        assert suffix[0] in values

    log.info("Clean up after test")
    topology.standalone.backend.delete(suffix=NEW_SUFFIX_1)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
