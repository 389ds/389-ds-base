# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import pytest
from lib389.topologies import topology_st as topo
from lib389.backend import Backends
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def setup_subsuffix_with_entry_parent(topo, request):
    """Setup a sub-suffix whose parent-suffix points to an entry, not a suffix."""
    inst = topo.standalone

    # Create ou=people entry under the root suffix
    log.info("Creating ou=people,dc=example,dc=com entry")
    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    ou_people = ous.get('people')

    # Create sub-suffix with parent-suffix pointing to the entry
    log.info("Creating sub-suffix ou=foo,ou=people,dc=example,dc=com")
    backends = Backends(inst)
    subsuffix_dn = 'ou=foo,ou=people,dc=example,dc=com'
    parent_suffix_dn = 'ou=people,dc=example,dc=com'

    foo_backend = backends.create(properties={
        'cn': 'foo',
        'nsslapd-suffix': subsuffix_dn,
        'parent': parent_suffix_dn,
    })

    # Create the suffix entry
    foo_ous = OrganizationalUnits(inst, parent_suffix_dn)
    foo_ou = foo_ous.create(properties={'ou': 'foo'})

    def cleanup():
        log.info("Cleaning up test backends and entries")
        try:
            foo_ou.delete()
        except Exception as e:
            log.warning(f"Failed to delete foo_ou: {e}")
        try:
            foo_backend.delete()
        except Exception as e:
            log.warning(f"Failed to delete foo_backend: {e}")

    request.addfinalizer(cleanup)

    return {
        'instance': inst,
        'backends': backends,
        'foo_backend': foo_backend,
        'ou_people': ou_people,
        'subsuffix_dn': subsuffix_dn,
        'parent_suffix_dn': parent_suffix_dn,
    }


def test_subsuffix_with_entry_parent_in_tree(topo, setup_subsuffix_with_entry_parent):
    """Test that a sub-suffix with parent pointing to an entry is visible in the tree.

    :id: 256f36f5-76ad-4043-ad8d-1f9e2afc4e1d
    :setup: Standalone instance with sub-suffix whose parent is an entry
    :steps:
        1. Verify the sub-suffix backend exists
        2. Get sub-suffixes of the root backend
        3. Verify the sub-suffix appears in the list
    :expectedresults:
        1. Backend should exist
        2. Sub-suffixes should be retrievable
        3. Sub-suffix should be visible (this is where the bug manifested)
    """
    backends = setup_subsuffix_with_entry_parent['backends']
    foo_backend = setup_subsuffix_with_entry_parent['foo_backend']
    subsuffix_dn = setup_subsuffix_with_entry_parent['subsuffix_dn']

    # Step 1: Verify the sub-suffix backend exists
    assert foo_backend.exists(), "The foo backend should exist"

    # Step 2: Get sub-suffixes of the root backend
    root_backend = backends.get(DEFAULT_SUFFIX)
    sub_suffixes = root_backend.get_sub_suffixes()
    log.info(f"Sub-suffixes found: {[s.get_attr_val_utf8('nsslapd-suffix') for s in sub_suffixes]}")

    # Step 3: Verify sub-suffix is in the list
    sub_suffix_found = any(
        s.get_attr_val_utf8_l('nsslapd-suffix') == subsuffix_dn.lower()
        for s in sub_suffixes
    )

    assert sub_suffix_found, (
        f"Sub-suffix {subsuffix_dn} should be visible in get_sub_suffixes(). "
        "The parent-suffix points to an entry, not a backend suffix."
    )


def test_subsuffix_in_backend_list(topo, setup_subsuffix_with_entry_parent):
    """Test that the sub-suffix appears in the backend list.

    :id: 0ccc49af-91bb-4e8f-b0e1-1bd0b75c041b
    :setup: Standalone instance with sub-suffix configuration
    :steps:
        1. Get all backends
        2. Verify both root suffix and sub-suffix are present
    :expectedresults:
        1. Should retrieve all backends
        2. Both suffixes should be listed
    """
    backends = setup_subsuffix_with_entry_parent['backends']
    subsuffix_dn = setup_subsuffix_with_entry_parent['subsuffix_dn']

    be_list = backends.list()
    suffixes = [be.get_attr_val_utf8_l('nsslapd-suffix') for be in be_list]

    assert DEFAULT_SUFFIX.lower() in suffixes, \
        f"Root suffix {DEFAULT_SUFFIX} should be in the list"
    assert subsuffix_dn.lower() in suffixes, \
        f"Sub-suffix {subsuffix_dn} should be in the list"


def test_subsuffix_dn_boundary_matching():
    """Test that suffix matching respects DN component boundaries.

    :id: 0b856e26-c394-4c36-b9ba-d7894aa2ed11
    :setup: None (unit test)
    :steps:
        1. Test exact suffix match
        2. Test proper DN ancestor match (ends with ,suffix)
        3. Test that partial string matches are rejected
    :expectedresults:
        1. Exact match should return True
        2. Proper ancestor should return True
        3. Partial string match should return False
    """
    from lib389.backend import is_subsuffix_of

    all_suffixes = {'dc=com', 'dc=example,dc=com', 'ou=dept,dc=example,dc=com'}

    # Test 1: Exact match
    assert is_subsuffix_of('dc=example,dc=com', 'dc=example,dc=com', all_suffixes), \
        "Exact match should return True"

    # Test 2: Parent is an entry under the suffix (not itself a suffix)
    assert is_subsuffix_of('ou=people,dc=example,dc=com', 'dc=example,dc=com', all_suffixes), \
        "Parent entry under suffix should return True"

    # Test 3: Parent IS a suffix - should return False (handled separately)
    assert not is_subsuffix_of('ou=dept,dc=example,dc=com', 'dc=example,dc=com', all_suffixes), \
        "Parent that is itself a suffix should return False"

    # Test 4: Edge case - wrong DN boundary (string ends with suffix but wrong boundary)
    edge_suffixes = {'dc=com', 'st,dc=com'}
    assert is_subsuffix_of('dc=test,dc=com', 'dc=com', edge_suffixes), \
        "dc=test,dc=com should match dc=com"
    assert not is_subsuffix_of('dc=test,dc=com', 'st,dc=com', edge_suffixes), \
        "dc=test,dc=com should NOT match st,dc=com (wrong DN boundary)"

    # Test 5: None input
    assert not is_subsuffix_of(None, 'dc=com', all_suffixes), \
        "None parent should return False"

    # Test 6: Closest ancestor - should only match the nearest suffix
    # Hierarchy: dc=com -> dc=example,dc=com -> ou=branch,dc=example,dc=com (suffix)
    #            -> ou=dept,ou=branch,dc=example,dc=com (entry) -> subsuffix
    # The subsuffix should only appear under ou=branch, not under dc=example,dc=com
    nested_suffixes = {'dc=com', 'dc=example,dc=com', 'ou=branch,dc=example,dc=com'}
    entry_parent = 'ou=dept,ou=branch,dc=example,dc=com'
    # Should match ou=branch (closest)
    assert is_subsuffix_of(entry_parent, 'ou=branch,dc=example,dc=com', nested_suffixes), \
        "Should match closest ancestor suffix (ou=branch)"
    # Should NOT match dc=example,dc=com (not closest)
    assert not is_subsuffix_of(entry_parent, 'dc=example,dc=com', nested_suffixes), \
        "Should NOT match distant ancestor (dc=example) - ou=branch is closer"
    # Should NOT match dc=com (not closest)
    assert not is_subsuffix_of(entry_parent, 'dc=com', nested_suffixes), \
        "Should NOT match distant ancestor (dc=com) - ou=branch is closer"

    log.info("All DN boundary edge cases passed")


def test_deep_suffix_hierarchy(topo, request):
    """Test complex hierarchy: suffix -> suffix -> entry -> suffix -> suffix.

    :id: fd06491a-defa-4780-8472-78c077febdfb
    :setup: Standalone instance
    :steps:
        1. Create sub-suffix ou=branch (parent=dc=example,dc=com - a suffix)
        2. Create entry ou=dept,ou=branch (not a suffix)
        3. Create sub-suffix ou=team,ou=dept,ou=branch (parent=ou=dept - an entry)
        4. Create sub-suffix ou=sub,ou=team,ou=dept,ou=branch (parent=ou=team - a suffix)
        5. Verify all sub-suffixes are correctly placed in the tree
    :expectedresults:
        1. Sub-suffix created successfully
        2. Entry created successfully
        3. Sub-suffix with entry parent created successfully
        4. Sub-suffix with suffix parent created successfully
        5. Tree hierarchy is correct
    """
    inst = topo.standalone
    backends = Backends(inst)

    # Define the hierarchy
    branch_suffix = f'ou=branch,{DEFAULT_SUFFIX}'
    dept_entry = f'ou=dept,{branch_suffix}'  # This is an ENTRY, not a suffix
    team_suffix = f'ou=team,{dept_entry}'
    sub_suffix = f'ou=sub,{team_suffix}'

    created_backends = []
    created_entries = []

    def cleanup():
        log.info("Cleaning up deep hierarchy test")
        for entry in reversed(created_entries):
            try:
                entry.delete()
            except Exception as e:
                log.warning(f"Failed to delete entry: {e}")
        for be in reversed(created_backends):
            try:
                be.delete()
            except Exception as e:
                log.warning(f"Failed to delete backend: {e}")

    request.addfinalizer(cleanup)

    # Step 1: Create ou=branch sub-suffix (parent is root suffix)
    log.info(f"Creating sub-suffix {branch_suffix}")
    branch_be = backends.create(properties={
        'cn': 'branch',
        'nsslapd-suffix': branch_suffix,
        'parent': DEFAULT_SUFFIX,
    })
    created_backends.append(branch_be)
    branch_ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    branch_ou = branch_ous.create(properties={'ou': 'branch'})
    created_entries.append(branch_ou)

    # Step 2: Create ou=dept entry under branch (NOT a suffix)
    log.info(f"Creating entry {dept_entry}")
    dept_ous = OrganizationalUnits(inst, branch_suffix)
    dept_ou = dept_ous.create(properties={'ou': 'dept'})
    created_entries.append(dept_ou)

    # Step 3: Create ou=team sub-suffix (parent is dept ENTRY, not a suffix)
    log.info(f"Creating sub-suffix {team_suffix} with entry parent {dept_entry}")
    team_be = backends.create(properties={
        'cn': 'team',
        'nsslapd-suffix': team_suffix,
        'parent': dept_entry,  # Parent is an ENTRY!
    })
    created_backends.append(team_be)
    team_ous = OrganizationalUnits(inst, dept_entry)
    team_ou = team_ous.create(properties={'ou': 'team'})
    created_entries.append(team_ou)

    # Step 4: Create ou=sub sub-suffix (parent is team suffix)
    log.info(f"Creating sub-suffix {sub_suffix} with suffix parent {team_suffix}")
    sub_be = backends.create(properties={
        'cn': 'sub',
        'nsslapd-suffix': sub_suffix,
        'parent': team_suffix,  # Parent is a SUFFIX
    })
    created_backends.append(sub_be)
    sub_ous = OrganizationalUnits(inst, team_suffix)
    sub_ou = sub_ous.create(properties={'ou': 'sub'})
    created_entries.append(sub_ou)

    # Step 5: Verify the tree hierarchy
    log.info("Verifying tree hierarchy...")

    # Root should have branch as sub-suffix
    root_be = backends.get(DEFAULT_SUFFIX)
    root_subs = root_be.get_sub_suffixes()
    root_sub_suffixes = [s.get_attr_val_utf8_l('nsslapd-suffix') for s in root_subs]
    log.info(f"Root sub-suffixes: {root_sub_suffixes}")
    assert branch_suffix.lower() in root_sub_suffixes, \
        f"branch should be under root suffix"

    # Branch should have team as sub-suffix (even though team's parent is an entry)
    branch_be_obj = backends.get(branch_suffix)
    branch_subs = branch_be_obj.get_sub_suffixes()
    branch_sub_suffixes = [s.get_attr_val_utf8_l('nsslapd-suffix') for s in branch_subs]
    log.info(f"Branch sub-suffixes: {branch_sub_suffixes}")
    assert team_suffix.lower() in branch_sub_suffixes, \
        f"team should be under branch suffix (parent is entry under branch)"

    # Team should have sub as sub-suffix
    team_be_obj = backends.get(team_suffix)
    team_subs = team_be_obj.get_sub_suffixes()
    team_sub_suffixes = [s.get_attr_val_utf8_l('nsslapd-suffix') for s in team_subs]
    log.info(f"Team sub-suffixes: {team_sub_suffixes}")
    assert sub_suffix.lower() in team_sub_suffixes, \
        f"sub should be under team suffix"

    log.info("Deep hierarchy test passed!")


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
