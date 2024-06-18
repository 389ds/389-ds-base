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
import time
from lib389.backend import Backends, Backend
from lib389._constants import HOST_STANDALONE, PORT_STANDALONE, DN_DM, PW_DM
from lib389.dbgen import dbgen_users
from lib389.mappingTree import MappingTrees
from lib389.topologies import topology_st
from lib389.referral import Referrals, Referral


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

PARENT_SUFFIX = "dc=parent"
CHILD1_SUFFIX = f"dc=child1,{PARENT_SUFFIX}"
CHILD2_SUFFIX = f"dc=child2,{PARENT_SUFFIX}"

PARENT_REFERRAL_DN = f"cn=ref,ou=People,{PARENT_SUFFIX}"
CHILD1_REFERRAL_DN = f"cn=ref,ou=people,{CHILD1_SUFFIX}"
CHILD2_REFERRAL_DN = f"cn=ref,ou=people,{CHILD2_SUFFIX}"

REFERRAL_CHECK_PEDIOD = 7



BESTRUCT = [
    { "bename" : "parent", "suffix": PARENT_SUFFIX },
    { "bename" : "child1", "suffix": CHILD1_SUFFIX },
    { "bename" : "child2", "suffix": CHILD2_SUFFIX },
]


@pytest.fixture(scope="module")
def topo(topology_st, request):
    bes = []

    def fin():
        for be in bes:
            be.delete()

    if not DEBUGGING:
        request.addfinalizer(fin)

    inst = topology_st.standalone
    # Reduce nsslapd-referral-check-period to accelerate test
    topology_st.standalone.config.set("nsslapd-referral-check-period", str(REFERRAL_CHECK_PEDIOD))

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

# Parameters for test_sub_suffixes
@pytest.mark.parametrize(
    "orphan_param",
    [
        pytest.param( ( True, { PARENT_SUFFIX: 2, CHILD1_SUFFIX:1, CHILD2_SUFFIX:1}), id="orphan-is-true" ),
        pytest.param( ( False, { PARENT_SUFFIX: 3, CHILD1_SUFFIX:1, CHILD2_SUFFIX:1}), id="orphan-is-false" ),
        pytest.param( ( None, { PARENT_SUFFIX: 3, CHILD1_SUFFIX:1, CHILD2_SUFFIX:1}), id="no-orphan" ),
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


def test_sub_suffixes_errlog(topo):
    """ check the entries found on suffix/sub-suffix
    used int

    :id: 1db9d52e-28de-11ef-b286-482ae39447e5
    :feature: mapping-tree
    :setup: Standalone instance with 3 additional backends:
            dc=parent, dc=child1,dc=parent, dc=childr21,dc=parent
    :steps:
        1. Check that id2entry error message is not in the error log.
    :expectedresults:
        1. Success
    """
    inst = topo.standalone
    assert not inst.searchErrorsLog('id2entry - Could not open id2entry err 0')


# Parameters for test_referral_subsuffix:
#   a tuple pair containing:
#     -  list of referral dn that must be created
#     -  dict of searches basedn: expected_number_of_referrals
@pytest.mark.parametrize(
    "parameters",
    [
        pytest.param( ((PARENT_REFERRAL_DN, CHILD1_REFERRAL_DN), {PARENT_SUFFIX: 2, CHILD1_SUFFIX:1, CHILD2_SUFFIX:0}), id="Both"),
        pytest.param( ((PARENT_REFERRAL_DN,), {PARENT_SUFFIX: 1, CHILD1_SUFFIX:0, CHILD2_SUFFIX:0}) , id="Parent"),
        pytest.param( ((CHILD1_REFERRAL_DN,), {PARENT_SUFFIX: 1, CHILD1_SUFFIX:1, CHILD2_SUFFIX:0}) , id="Child"),
        pytest.param( ((), {PARENT_SUFFIX: 0, CHILD1_SUFFIX:0, CHILD2_SUFFIX:0}), id="None"),
    ])

def test_referral_subsuffix(topo, request, parameters):
    """Test the results of an inverted parent suffix definition in the configuration.

    For more details see:
    https://www.port389.org/docs/389ds/design/mapping_tree_assembly.html

    :id: 4e111a22-2a5d-11ef-a890-482ae39447e5
    :feature: referrals
    :setup: Standalone instance with 3 additional backends:
            dc=parent, dc=child1,dc=parent, dc=childr21,dc=parent

    :setup: Standalone instance
    :parametrized: yes
    :steps:
        refs,searches = referrals

        1. Create the referrals according to the current parameter
        2. Wait enough time so they get detected
        3. For each search base dn, in the current parameter, perform the two following steps
        4. In 3. loop: Perform a search with provided base dn
        5. In 3. loop: Check that the number of returned referrals is the expected one.

    :expectedresults:
        all steps succeeds
    """
    inst = topo.standalone

    def fin():
        log.info('Deleting all referrals')
        for ref in Referrals(inst, PARENT_SUFFIX).list():
            ref.delete()

    # Set cleanup callback
    if DEBUGGING:
        request.addfinalizer(fin)

    # Remove all referrals
    fin()
    # Add requested referrals
    for dn in parameters[0]:
        refs = Referral(inst, dn=dn)
        refs.create(basedn=dn, properties={ 'cn': 'ref', 'ref': f'ldap://remote/{dn}'})
    # Wait that the internal search detects the referrals
    time.sleep(REFERRAL_CHECK_PEDIOD + 1)
    # Open a test connection
    ldc = ldap.initialize(f"ldap://{HOST_STANDALONE}:{PORT_STANDALONE}")
    ldc.set_option(ldap.OPT_REFERRALS,0)
    ldc.simple_bind_s(DN_DM,PW_DM)

    # For each search base dn:
    for basedn,nbref in parameters[1].items():
        log.info(f"Referrals are: {parameters[0]}")
        # Perform a search with provided base dn
        result = ldc.search_s(basedn, ldap.SCOPE_SUBTREE, filterstr="(ou=People)")
        found_dns = [ dn for dn,entry in result if dn is not None ]
        found_refs = [ entry for dn,entry in result if dn is None ]
        log.info(f"Search on {basedn} returned {found_dns} and {found_refs}")
        # Check that the number of returned referrals is the expected one.
        log.info(f"Search returned {len(found_refs)} referrals. {nbref} are expected.")
        assert len(found_refs) == nbref
    ldc.unbind()
