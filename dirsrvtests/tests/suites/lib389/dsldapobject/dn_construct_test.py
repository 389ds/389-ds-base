# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st

from lib389.idm.group import Groups, Group

pytestmark = pytest.mark.tier1

#################################################################################
# This is a series of test cases to assert that various DN construction scenarios
# work as expected in lib389.
#
# DSLdapObjects are designed to allow explicit control, or to "safely assume"
# so that ldap concepts aren't as confusing.
# You can thus construct an object with a DN that is:
# * defined by you expliticly
# * derived from properties of the object automatically
#
# There are also two paths to construction: from the pluralised factory style
# builder, or from the singular. The factory style has very few extra parts
# but it's worth testing anyway.
#
# In no case do we derive a multi value rdn due to their complexity.
#

def test_mul_explicit_rdn(topology_st):
    """Test that with multiple cn and an explicit rdn, we use the rdn

    :id: b39ef204-45c0-4a74-9b59-b4ac1199d78c

    :setup: standalone instance

    :steps: 1. Create with mulitple cn and rdn

    :expectedresults: 1. Create success
    """
    # Create with an explicit rdn value, given to the properties/rdn
    gps = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    gp = gps.create('cn=test_mul_explicit_rdn',
        properties={
            'cn': ['test_mul_explicit_rdn', 'other_cn_test_mul_explicit_rdn'],
        })
    assert gp.dn.lower() == f'cn=test_mul_explicit_rdn,ou=groups,{DEFAULT_SUFFIX}'.lower()
    gp.delete()

def test_mul_derive_single_dn(topology_st):
    """Test that with single cn we derive rdn correctly.

    :id: f34f271a-ca57-4aa0-905a-b5392ce06c79

    :setup: standalone instance

    :steps: 1. Create with single cn

    :expectedresults: 1. Create success
    """
    gps = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    gp = gps.create(properties={
            'cn': ['test_mul_derive_single_dn'],
        })
    assert gp.dn.lower() == f'cn=test_mul_derive_single_dn,ou=groups,{DEFAULT_SUFFIX}'.lower()
    gp.delete()

def test_mul_derive_mult_dn(topology_st):
    """Test that with multiple cn we derive rdn correctly.

    :id: 1e1f5483-bfad-4f73-9dfb-aec54d08b268

    :setup: standalone instance

    :steps: 1. Create with multiple cn

    :expectedresults: 1. Create success
    """
    gps = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    gp = gps.create(properties={
            'cn': ['test_mul_derive_mult_dn', 'test_mul_derive_single_dn'],
        })
    assert gp.dn.lower() == f'cn=test_mul_derive_mult_dn,ou=groups,{DEFAULT_SUFFIX}'.lower()
    gp.delete()

def test_sin_explicit_dn(topology_st):
    """Test explicit dn with create

    :id: 2d812225-243b-4f87-85ad-d403a4ae0267

    :setup: standalone instance

    :steps: 1. Create with explicit dn

    :expectedresults: 1. Create success
    """
    expect_dn = f'cn=test_sin_explicit_dn,ou=groups,{DEFAULT_SUFFIX}'
    gp = Group(topology_st.standalone, dn=expect_dn)
    gp.create(properties={
            'cn': ['test_sin_explicit_dn'],
        })
    assert gp.dn.lower() == expect_dn.lower()
    gp.delete()

def test_sin_explicit_rdn(topology_st):
    """Test explicit rdn with create.

    :id: a2c14e50-8086-4edb-9088-3f4a8e875c3a

    :setup: standalone instance

    :steps: 1. Create with explicit rdn

    :expectedresults: 1. Create success
    """
    gp = Group(topology_st.standalone)
    gp.create(rdn='cn=test_sin_explicit_rdn',
        basedn=f'ou=groups,{DEFAULT_SUFFIX}',
        properties={
            'cn': ['test_sin_explicit_rdn'],
        })
    assert gp.dn.lower() == f'cn=test_sin_explicit_rdn,ou=groups,{DEFAULT_SUFFIX}'.lower()
    gp.delete()

def test_sin_derive_single_dn(topology_st):
    """Derive the dn from a single cn

    :id: d7597016-214c-4fbd-8b48-71eb16ea9ede

    :setup: standalone instance

    :steps: 1. Create with a single cn (no dn, no rdn)

    :expectedresults: 1. Create success
    """
    gp = Group(topology_st.standalone)
    gp.create(basedn=f'ou=groups,{DEFAULT_SUFFIX}',
        properties={
            'cn': ['test_sin_explicit_dn'],
        })
    assert gp.dn.lower() == f'cn=test_sin_explicit_dn,ou=groups,{DEFAULT_SUFFIX}'.lower()
    gp.delete()

def test_sin_derive_mult_dn(topology_st):
    """Derive the dn from multiple cn

    :id: 0a1a7132-a08f-4b56-ae52-30c8ca59cfaf

    :setup: standalone instance

    :steps: 1. Create with multiple cn

    :expectedresults: 1. Create success
    """
    gp = Group(topology_st.standalone)
    gp.create(basedn=f'ou=groups,{DEFAULT_SUFFIX}',
        properties={
            'cn': ['test_sin_derive_mult_dn', 'other_test_sin_derive_mult_dn'],
        })
    assert gp.dn.lower() == f'cn=test_sin_derive_mult_dn,ou=groups,{DEFAULT_SUFFIX}'.lower()
    gp.delete()

def test_sin_invalid_no_basedn(topology_st):
    """Test that with insufficent data, create fails.

    :id: a710b81c-cb74-4632-97b3-bdbcccd40954

    :setup: standalone instance

    :steps: 1. Create with no basedn (no rdn derivation will work)

    :expectedresults: 1. Create fails
    """
    gp = Group(topology_st.standalone)
    # No basedn, so we can't derive the full dn from this.
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        gp.create(properties={
                'cn': ['test_sin_invalid_no_basedn'],
            })

def test_sin_invalid_no_rdn(topology_st):
    """Test that with no cn, rdn derivation fails.

    :id: c3bb28f8-db59-4d8a-8920-169879ef702b

    :setup: standalone instance

    :steps: 1. Create with no cn

    :expectedresults: 1. Create fails
    """
    gp = Group(topology_st.standalone)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        # Note lack of rdn derivable type (cn) AND no rdn
        gp.create(basedn=f'ou=groups,{DEFAULT_SUFFIX}',
            properties={
                'member': ['test_sin_explicit_dn'],
            })

def test_sin_non_present_rdn(topology_st):
    """Test that with an rdn not present in attributes, create succeeds in some cases.

    :id: a5d9cb24-8907-4622-ac85-90407a66e00a

    :setup: standalone instance

    :steps: 1. Create with an rdn not in properties

    :expectedresults: 1. Create success
    """
    # Test that creating something with an rdn not present in the properties works
    # NOTE: I think that this is 389-ds making this work, NOT lib389.
    gp1 = Group(topology_st.standalone)
    gp1.create(rdn='cn=test_sin_non_present_rdn',
        basedn=f'ou=groups,{DEFAULT_SUFFIX}',
        properties={
            'cn': ['other_test_sin_non_present_rdn'],
        })
    assert gp1.dn.lower() == f'cn=test_sin_non_present_rdn,ou=groups,{DEFAULT_SUFFIX}'.lower()
    gp1.delete()

    # Now, test where there is no cn. lib389 is blocking this today, but
    # 50259 will change this.
    gp2 = Group(topology_st.standalone)
    gp2.create(rdn='cn=test_sin_non_present_rdn',
        basedn=f'ou=groups,{DEFAULT_SUFFIX}',
        properties={})
    assert gp2.dn.lower() == f'cn=test_sin_non_present_rdn,ou=groups,{DEFAULT_SUFFIX}'.lower()
    gp2.delete()
