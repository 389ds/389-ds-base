# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import ldap
import logging
import os
import pytest
from lib389.backend import Backends, Backend
from lib389.dbgen import dbgen_users
from lib389.mappingTree import MappingTrees
from lib389.topologies import topology_st

try:
    from lib389.backend import BackendSuffixView
    has_orphan_attribute = True
except ImportError:
    has_orphan_attribute = False

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

BESTRUCT = [
    { "bename" : "parent", "suffix": "dc=parent" },
    { "bename" : "child1", "suffix": "dc=child1,dc=parent" },
    { "bename" : "child2", "suffix": "dc=child2,dc=parent" },
]


@pytest.fixture(scope="function")
def topo(topology_st, request):
    bes = []

    def fin():
        for be in bes:
            be.delete()

    if not DEBUGGING:
        request.addfinalizer(fin)

    inst = topology_st.standalone
    ldif_files = {}
    for d in BESTRUCT:
        bename = d['bename']
        suffix = d['suffix']
        log.info(f'Adding suffix: {suffix} and backend: {bename}...')
        backends = Backends(inst)
        try:
            be = backends.create(properties={'nsslapd-suffix': suffix, 'name': bename})
            # Insert at list head so that children backends get deleted before parent one.
            bes.insert(0, be)
        except ldap.UNWILLING_TO_PERFORM as e:
            if str(e) == "Mapping tree for this suffix exists!":
                pass
            else:
                raise e

        ldif_dir = inst.get_ldif_dir()
        ldif_files[bename] = os.path.join(ldif_dir, f'default_{bename}.ldif')
        dbgen_users(inst, 5, ldif_files[bename], suffix)
    inst.stop()
    for d in BESTRUCT:
        bename = d['bename']
        inst.ldif2db(bename, None, None, None, ldif_files[bename])
    inst.start()
    return topology_st

# Parameters for test_change_repl_passwd
EXPECTED_ENTRIES = (("dc=parent", 39), ("dc=child1,dc=parent", 13), ("dc=child2,dc=parent", 13))
@pytest.mark.parametrize(
    "orphan_param",
    [
        pytest.param( ( True, { "dc=parent": 2, "dc=child1,dc=parent":1, "dc=child2,dc=parent":1}), id="orphan-is-true" ),
        pytest.param( ( False, { "dc=parent": 3, "dc=child1,dc=parent":1, "dc=child2,dc=parent":1}), id="orphan-is-false" ),
        pytest.param( ( None, { "dc=parent": 3, "dc=child1,dc=parent":1, "dc=child2,dc=parent":1}), id="no-orphan" ),
    ],
)


@pytest.mark.bz2083589
@pytest.mark.skipif(not has_orphan_attribute, reason = "compatibility attribute not yet implemented in this version")
def test_sub_suffixes(topo, orphan_param):
    """ check the entries found on suffix/sub-suffix
    used int

    :id: 5b4421c2-d851-11ec-a760-482ae39447e5
    :feature: mapping-tree
    :setup: Standalone instance with 3 additional backends:
            dc=parent, dc=child1,dc=parent, dc=childr21,dc=parent
    :steps:
        1. Det orphan attribute mapping tree entry for dc=child1,dc=parent according to orphan_param value
        2. Restart the server to rebuild the mapping tree
        3. For each suffix: search the suffix
    :expectedresults:
        1. Success
        2. Success
        3. Number of entries should be the expected one
    """
    mt = MappingTrees(topo.standalone).get('dc=child1,dc=parent')
    orphan = orphan_param[0]
    expected_values = orphan_param[1]
    if orphan is True:
        mt.replace('orphan', 'true')
    elif orphan is False:
        mt.replace('orphan', 'false')
    elif orphan is None:
        mt.remove_all('orphan')
    topo.standalone.restart()

    for suffix, expected in expected_values.items():
        log.info(f'Verifying domain component entries count for search under {suffix} ...')
        entries = topo.standalone.search_s(suffix, ldap.SCOPE_SUBTREE, "(dc=*)")
        assert len(entries) == expected
        log.info('Found {expected} domain component entries as expected while searching {suffix}')

    log.info('Test PASSED')


