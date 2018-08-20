# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import logging
import ldap

from lib389._constants import *
from lib389.monitor import MonitorBackend
from lib389.mappingTree import MappingTrees
from lib389.index import Indexes
from lib389.backend import Backends
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

DUMMY_BACKEND = 'dummydb'
DUMMY_BACKEND_DN = "cn=dummy_dn,cn=ldbm database,cn=plugins,cn=config"
NEW_SUFFIX_1_RDN = "o=new_suffix_1"
NEW_SUFFIX_2_RDN = "o=new_suffix_2"
BACKEND_NAME_1 = "new_backend_1"
BACKEND_NAME_2 = "new_backend_2"


@pytest.fixture()
def backend(topology_st, request):
    """Create and remove a backend"""

    log.info('Create a backend')
    backends = Backends(topology_st.standalone)
    backend = backends.create(properties={'nsslapd-suffix': NEW_SUFFIX_1_RDN,
                                          'cn': BACKEND_NAME_1})

    def fin():
        log.info("Just make it clean in the end")
        if backend.exists():
            backend.delete()
    request.addfinalizer(fin)

    return backend


def test_list(topology_st):
    """Test basic list method functionality

    :id: 084c0937-0b39-4e89-8561-081ae2b144c6
    :setup: Standalone instance
    :steps:
        1. List all backends
        2. Create a backend
        3. List all backends
        4. Create one more backend
        5. List all backends
        6. Clean up the created backends
    :expectedresults:
        1. Operation should be successful
        2. Backend should be created
        3. Created backend should be listed
        4. Backend should be created
        5. Created backend should be listed
        6. Operation should be successful
    """

    backends = Backends(topology_st.standalone)

    ents = backends.list()
    nb_backend = len(ents)
    for ent in ents:
        topology_st.standalone.log.info("List(%d): backend %s" %
                                        (nb_backend, ent.dn))

    log.info("Create a first backend and check list all backends")
    b1 = backends.create(properties={'cn': BACKEND_NAME_1,
                                     'nsslapd-suffix': NEW_SUFFIX_1_RDN})

    ents = backends.list()
    for ent in ents:
        topology_st.standalone.log.info("List(%d): backend %s" %
                                        (nb_backend + 1, ent.dn))
    assert len(ents) == (nb_backend + 1)

    log.info("Create a second backend and check list all backends")
    b2 = backends.create(properties={'cn': BACKEND_NAME_2,
                                     'nsslapd-suffix': NEW_SUFFIX_2_RDN})

    ents = backends.list()
    for ent in ents:
        topology_st.standalone.log.info("List(%d): backend %s" %
                                        (nb_backend + 2, ent.dn))
    assert len(ents) == (nb_backend + 2)

    log.info("Just make it clean in the end")
    b1.delete()
    b2.delete()


def test_create(topology_st):
    """Test basic list method functionality

    :id: df55a60b-f4dd-4f18-975d-4b223e63091f
    :setup: Standalone instance
    :steps:
        2. Create a backend specifying properties with a name and a suffix
        2. Create a backend specifying no properties
        3. Create a backend specifying suffix that already exist
        4. Create a backend specifying existing backend name but new suffix
        5. Create a backend specifying no backend name
        6. Create a backend specifying no backend suffix
        7. Clean up the created backend
    :expectedresults:
        1. Backend should be created
        2. Unwilling to perform error should be raised
        3. Unwilling to perform error should be raised
        4. Unwilling to perform error should be raised
        5. Unwilling to perform error should be raised
        6. Unwilling to perform error should be raised
        7. Operation should be successful
    """

    backends = Backends(topology_st.standalone)

    log.info("Create a backend")
    backend = backends.create(properties={'cn': BACKEND_NAME_1,
                                          'nsslapd-suffix': NEW_SUFFIX_1_RDN})

    log.info("Check behaviour with missing properties")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        backends.create()

    log.info("Check behaviour with already existing backend for that suffix")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        backends.create(properties={'cn': BACKEND_NAME_2,
                                    'nsslapd-suffix': NEW_SUFFIX_1_RDN})

    log.info("Check behaviour with already existing backend nasme, "
             "but new suffix")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        backends.create(properties={'cn': BACKEND_NAME_1,
                                    'nsslapd-suffix': NEW_SUFFIX_2_RDN})

    log.info("Create a backend without BACKEND_NAME")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        backends.create(properties={'nsslapd-suffix': NEW_SUFFIX_2_RDN})
        ents = backends.list()
        assert len(ents) == 1

    log.info("Create a backend without BACKEND_SUFFIX")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        backends.create(properties={'cn': BACKEND_NAME_1})
        ents = backends.list()
        assert len(ents) == 1

    log.info("Just make it clean in the end")
    backend.delete()


def test_get_valid(topology_st, backend):
    """Test basic get method functionality

    :id: d3e5ebf2-5598-41cd-b1fa-ac18d3057f80
    :setup: Standalone instance
    :steps:
        1. Get the backend with suffix
        2. Get the backend with backend name
        3. Get the backend with backend DN
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
    """

    backends = Backends(topology_st.standalone)

    log.info("Try to get a backend with backend suffix")
    d1 = backends.get(NEW_SUFFIX_1_RDN)
    assert d1 is not None
    topology_st.standalone.log.info("Suffix (%d): backend %s" % (1, d1.dn))

    log.info("Try to get a backend with backend name")
    d2 = backends.get(BACKEND_NAME_1)
    assert d2 is not None
    topology_st.standalone.log.info("Backend (%d): backend %s" % (1, d2.dn))

    log.info("Try to get a backend with backend DN")
    d3 = backends.get(dn=backend.dn)
    assert d3 is not None
    topology_st.standalone.log.info("DN (%d): backend %s" % (1, d3.dn))


def test_get_invalid(topology_st, backend):
    """Test the invalid situations while using get method

    :id: c5028350-1381-4d6d-82b8-4959a9b82964
    :setup: Standalone instance
    :steps:
        1. Get the backend with invalid suffix
        2. Get the backend with invalid backend name
        3. Get the backend with invalid backend DN
    :expectedresults:
        1. No such object error should be raised
        2. No such object error should be raised
        3. No such object error should be raised
    """

    backends = Backends(topology_st.standalone)

    log.info("Try to get the backend with invalid backend suffix")
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        backends.get(NEW_SUFFIX_2_RDN)

    log.info("Try to get the backend with invalid backend name")
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        backends.get(DUMMY_BACKEND)

    log.info("Try to get the backend with invalid backend DN")
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        backends.get(dn=DUMMY_BACKEND_DN)


def test_delete(topology_st, backend):
    """Delete the backend and check that mapping tree and index were deleted too

    :id: d44dac3a-dae8-48e8-bd43-5be15237d093
    :setup: Standalone instance
    :steps:
        1. Create a backend
        2. Delete the backend
        3. Check all backend indexes were deleted
        4. Check backend mapping tree was deleted
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. There should be no backend indexes
        4. There should be no backend mapping tree
    """

    log.info('Delete a backend')
    backend.delete()

    log.info("Check that all indices are deleted")
    indexes = Indexes(topology_st.standalone, "cn=index,{}".format(backend.dn))
    assert not indexes.list()

    with pytest.raises(ldap.NO_SUCH_OBJECT):
        mts = MappingTrees(topology_st.standalone)
        mts.get(BACKEND_NAME_1)


def test_lint(topology_st, backend):
    """Test basic lint method functionality

    :id: 79e23980-e764-4839-b040-667357195710
    :setup: Standalone instance
    :steps:
        1. Create a backend
        2. Remove its mapping tree
        3. Run lint method on the backend
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. It should detect the missing mapping tree
    """

    log.info('Remove the mapping tree')
    mps = MappingTrees(topology_st.standalone)
    mps.delete(suffix=NEW_SUFFIX_1_RDN, bename=BACKEND_NAME_1)

    error_found = False
    for item in backend.lint():
        if item['dsle'] == 'DSBLE0001':
            error_found = True
    assert error_found


def test_create_sample_entries(topology_st, backend):
    """Test basic create_sample_entries method functionality

    :id: 9c995027-4884-494a-9af0-c8d2f43a0123
    :setup: Standalone instance
    :steps:
        1. Create a backend
        2. Create entries using create_sample_entries method
        3. Search the created entries (pick ou=People rdn)
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. The entry should be found
    """

    log.info('Create sample entries')
    backend.create_sample_entries(version='001003006')

    log.info('Search the entries')
    entries = topology_st.standalone.search_s('ou=people,{}'.format(NEW_SUFFIX_1_RDN), ldap.SCOPE_SUBTREE,
                                              '(objectclass=*)')
    assert entries


def test_get_monitor(topology_st, backend):
    """Test basic get_monitor method functionality

    :id: 7e22f967-d1a6-4e84-9659-f708fdd0b1ed
    :setup: Standalone instance
    :steps:
        1. Create a backend
        2. Get a MonitorBackend object instance using get_monitor function
        3. Directly define a MonitorBackend instance
        4. Assert that entries DN and attributes are the same
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. The entries DN and attribute should be the same
    """

    log.info('Use get_monitor method to get MonitorBackend object')
    backend_monitor = backend.get_monitor()

    log.info('Directly define a MonitorBackend instance')
    monitor = MonitorBackend(topology_st.standalone, "cn=monitor,{}".format(backend.dn))

    log.info('Check the objects are the same')
    assert backend_monitor.dn == monitor.dn


def test_get_indexes(topology_st, backend):
    """Test basic get_indexes method functionality

    :id: 4e01d9e8-c355-4dd4-b7d9-a1d26afed768
    :setup: Standalone instance
    :steps:
        1. Create a backend
        2. Get an Indexes object instance using get_indexes function
        3. Directly define an Indexes instance
        4. Assert that the objects are the same
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. The entries should match
    """

    log.info('Use get_indexes method to get Indexes object')
    backend_indexes = backend.get_indexes()

    log.info('Directly define an Indexes instance')
    indexes = Indexes(topology_st.standalone, "cn=index,{}".format(backend.dn))

    log.info('Check the objects are the same')
    index_found = False
    for i, j in zip(indexes.list(), backend_indexes.list()):
        if i.dn == j.dn:
            index_found = True
    assert index_found


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
