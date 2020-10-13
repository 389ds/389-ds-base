# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import pytest
import time
from lib389.topologies import topology_st
from lib389.backend import Backends, Backend
from lib389.mappingTree import MappingTrees
from lib389.idm.domain import Domain
from lib389.configurations.sample import create_base_domain

@pytest.fixture(scope="function")
def topology(topology_st):
    bes = Backends(topology_st.standalone)
    bes.delete_all_dangerous()
    mts = MappingTrees(topology_st.standalone)
    assert len(mts.list()) == 0
    return topology_st


def create_backend(inst, rdn, suffix):
    # We only support dc= in this test.
    assert suffix.startswith('dc=')
    be1 = Backend(inst)
    be1.create(properties={
            'cn': rdn,
            'nsslapd-suffix': suffix,
        },
        create_mapping_tree=False
    )

    # Now we temporarily make the MT for this node so we can add the base entry.
    mts = MappingTrees(inst)
    mt = mts.create(properties={
        'cn': suffix,
        'nsslapd-state': 'backend',
        'nsslapd-backend': rdn,
    })

    # Create the domain entry
    create_base_domain(inst, suffix)
    # Now delete the mt
    mt.delete()

    return be1

def test_mapping_tree_inverted(topology):
    """Test the results of an inverted parent suffix definition in the configuration.

    For more details see:
    https://www.port389.org/docs/389ds/design/mapping_tree_assembly.html

    :id: 024c4960-3aac-4d05-bc51-963dfdeb16ca

    :setup: Standalone instance (no backends)

    :steps:
        1. Add two backends without mapping trees.
        2. Add the mapping trees with inverted parent-suffix definitions.
        3. Attempt to search the definitions

    :expectedresults:
        1. Success
        2. Success
        3. The search suceed and can see validly arranged entries.
    """
    inst = topology.standalone
    # First create two Backends, without mapping trees.
    be1 = create_backend(inst, 'userRootA', 'dc=example,dc=com')
    be2 = create_backend(inst, 'userRootB', 'dc=straya,dc=example,dc=com')
    # Okay, now we create the mapping trees for these backends, and we *invert* them in the parent config setting
    mts = MappingTrees(inst)
    mtb = mts.create(properties={
        'cn': 'dc=straya,dc=example,dc=com',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootB',
    })
    mta = mts.create(properties={
        'cn': 'dc=example,dc=com',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootA',
        'nsslapd-parent-suffix': 'dc=straya,dc=example,dc=com'
    })

    dc_ex = Domain(inst, dn='dc=example,dc=com')
    assert dc_ex.exists()

    dc_st = Domain(inst, dn='dc=straya,dc=example,dc=com')
    assert dc_st.exists()

    # Restart and check again
    inst.restart()
    assert dc_ex.exists()
    assert dc_st.exists()


def test_mapping_tree_nonexist_parent(topology):
    """Test a backend whos mapping tree definition has a non-existant parent-suffix

    For more details see:
    https://www.port389.org/docs/389ds/design/mapping_tree_assembly.html

    :id: 7a9a09bd-7604-48f7-93cb-abff9e0d0131

    :setup: Standalone instance (no backends)

    :steps:
        1. Add one backend without mapping tree
        2. Configure the mapping tree with a non-existant parent suffix
        3. Attempt to search the backend

    :expectedresults:
        1. Success
        2. Success
        3. The search suceed and can see validly entries.
    """
    inst = topology.standalone
    be1 = create_backend(inst, 'userRootC', 'dc=test,dc=com')
    mts = MappingTrees(inst)
    mta = mts.create(properties={
        'cn': 'dc=test,dc=com',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootC',
        'nsslapd-parent-suffix': 'dc=com'
    })
    # In this case the MT is never joined properly to the hierachy because the parent suffix
    # doesn't exist. The config is effectively ignored. That means that it can't be searched!
    dc_ex = Domain(inst, dn='dc=test,dc=com')
    assert dc_ex.exists()
    # Restart and check again.
    inst.restart()
    assert dc_ex.exists()


# Two same length (dc=example,dc=com    dc=abcdefg,dc=abc)
def test_mapping_tree_same_length(topology):
    inst = topology.standalone
    # First create two Backends, without mapping trees.
    be1 = create_backend(inst, 'userRootA', 'dc=example,dc=com')
    be2 = create_backend(inst, 'userRootB', 'dc=abcdefg,dc=hij')
    # Okay, now we create the mapping trees for these backends, and we *invert* them in the parent config setting
    mts = MappingTrees(inst)
    mtb = mts.create(properties={
        'cn': 'dc=example,dc=com',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootA',
    })
    mta = mts.create(properties={
        'cn': 'dc=abcdefg,dc=hij',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootB',
    })

    dc_ex = Domain(inst, dn='dc=example,dc=com')
    assert dc_ex.exists()

    dc_ab = Domain(inst, dn='dc=abcdefg,dc=hij')
    assert dc_ab.exists()

    # Restart and check again
    inst.restart()
    assert dc_ex.exists()
    assert dc_ab.exists()

# Flipped DC comps (dc=exmaple,dc=com  dc=com,dc=example)
def test_mapping_tree_flipped_components(topology):
    inst = topology.standalone
    # First create two Backends, without mapping trees.
    be1 = create_backend(inst, 'userRootA', 'dc=example,dc=com')
    be2 = create_backend(inst, 'userRootB', 'dc=com,dc=example')
    # Okay, now we create the mapping trees for these backends, and we *invert* them in the parent config setting
    mts = MappingTrees(inst)
    mtb = mts.create(properties={
        'cn': 'dc=example,dc=com',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootA',
    })
    mta = mts.create(properties={
        'cn': 'dc=com,dc=example',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootB',
    })

    dc_ex = Domain(inst, dn='dc=example,dc=com')
    assert dc_ex.exists()

    dc_ab = Domain(inst, dn='dc=com,dc=example')
    assert dc_ab.exists()

    # Restart and check again
    inst.restart()
    assert dc_ex.exists()
    assert dc_ab.exists()

# Weirdnesting (dc=exmaple,dc=com, dc=com,dc=example, dc=com,dc=example,dc=com)
def test_mapping_tree_weird_nesting(topology):
    inst = topology.standalone
    # First create two Backends, without mapping trees.
    be1 = create_backend(inst, 'userRootA', 'dc=example,dc=com')
    be2 = create_backend(inst, 'userRootB', 'dc=com,dc=example')
    be3 = create_backend(inst, 'userRootC', 'dc=com,dc=example,dc=com')
    # Okay, now we create the mapping trees for these backends, and we *invert* them in the parent config setting
    mts = MappingTrees(inst)
    mtb = mts.create(properties={
        'cn': 'dc=example,dc=com',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootA',
    })
    mta = mts.create(properties={
        'cn': 'dc=com,dc=example',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootB',
    })
    mtc = mts.create(properties={
        'cn': 'dc=com,dc=example,dc=com',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootC',
    })

    dc_ex = Domain(inst, dn='dc=example,dc=com')
    assert dc_ex.exists()

    dc_ab = Domain(inst, dn='dc=com,dc=example')
    assert dc_ab.exists()

    dc_ec = Domain(inst, dn='dc=com,dc=example,dc=com')
    assert dc_ec.exists()

    # Restart and check again
    inst.restart()
    assert dc_ex.exists()
    assert dc_ab.exists()
    assert dc_ec.exists()

# Diff lens (dc=myserver, dc=a,dc=b,dc=c,dc=d, dc=example,dc=com)
def test_mapping_tree_mixed_length(topology):
    inst = topology.standalone
    # First create two Backends, without mapping trees.
    be1 = create_backend(inst, 'userRootA', 'dc=myserver')
    be1 = create_backend(inst, 'userRootB', 'dc=m')
    be1 = create_backend(inst, 'userRootC', 'dc=a,dc=b,dc=c,dc=d,dc=e')
    be1 = create_backend(inst, 'userRootD', 'dc=example,dc=com')
    be1 = create_backend(inst, 'userRootE', 'dc=myldap')

    mts = MappingTrees(inst)
    mts.create(properties={
        'cn': 'dc=myserver',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootA',
    })
    mts.create(properties={
        'cn': 'dc=m',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootB',
    })
    mts.create(properties={
        'cn': 'dc=a,dc=b,dc=c,dc=d,dc=e',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootC',
    })
    mts.create(properties={
        'cn': 'dc=example,dc=com',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootD',
    })
    mts.create(properties={
        'cn': 'dc=myldap',
        'nsslapd-state': 'backend',
        'nsslapd-backend': 'userRootE',
    })

    dc_a = Domain(inst, dn='dc=myserver')
    assert dc_a.exists()
    dc_b = Domain(inst, dn='dc=m')
    assert dc_b.exists()
    dc_c = Domain(inst, dn='dc=a,dc=b,dc=c,dc=d,dc=e')
    assert dc_c.exists()
    dc_d = Domain(inst, dn='dc=example,dc=com')
    assert dc_d.exists()
    dc_e = Domain(inst, dn='dc=myldap')
    assert dc_e.exists()

    inst.restart()
    assert dc_a.exists()
    assert dc_b.exists()
    assert dc_c.exists()
    assert dc_d.exists()
    assert dc_e.exists()

# 50 suffixes, shallow nest (dc=example,dc=com, then dc=00 -> dc=50)
def test_mapping_tree_many_shallow(topology):
    inst = topology.standalone
    dcs = [ ('dc=x%s,dc=example,dc=com' % x, 'userRoot%s' % x) for x in range(0,50) ]

    for (dc, bename) in dcs:
        create_backend(inst, bename, dc)

    mts = MappingTrees(inst)
    for (dc, bename) in dcs:
        mts.create(properties={
            'cn': dc,
            'nsslapd-state': 'backend',
            'nsslapd-backend': bename,
        })

    dc_asserts = [ Domain(inst, dn=dc[0]) for dc in dcs ]
    for dc_a in dc_asserts:
        assert dc_a.exists()
    inst.restart()
    for dc_a in dc_asserts:
        assert dc_a.exists()

# 50 suffixes, deeper nesting (dc=example,dc=com, dc=00  -> dc=10 and dc=a,dc=b,dc=c,dc=d,dc=XX,dc=example,dc=com)
def test_mapping_tree_many_deep_nesting(topology):
    inst = topology.standalone
    be_count = 0
    dcs = []
    for x in range(0, 10):
        dcs.append(('dc=x%s,dc=example,dc=com' % x, 'userRoot%s' % be_count))
        be_count += 1

    # Now add some children.
    for x in range(0,10):
        dcs.append(('dc=nest,dc=x%s,dc=example,dc=com' % x, 'userRoot%s' % be_count))
        be_count += 1

    #  Now add nested children
    for x in range(0,10):
        for y in range(0,5):
            dcs.append(('dc=y%s,dc=nest,dc=x%s,dc=example,dc=com' % (y, x), 'userRoot%s' % be_count))
            be_count += 1

    for (dc, bename) in dcs:
        create_backend(inst, bename, dc)

    mts = MappingTrees(inst)
    for (dc, bename) in dcs:
        mts.create(properties={
            'cn': dc,
            'nsslapd-state': 'backend',
            'nsslapd-backend': bename,
        })

    dc_asserts = [ Domain(inst, dn=dc[0]) for dc in dcs ]
    for dc_a in dc_asserts:
        assert dc_a.exists()
    inst.restart()
    for dc_a in dc_asserts:
        assert dc_a.exists()







