# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
from lib389._constants import *
from lib389.schema import Schema, OBJECT_MODEL_PARAMS
from ldap.schema.models import ObjectClass
from test389.topologies import topology_st as topo

log = logging.getLogger(__name__)


pytestmark = pytest.mark.tier1

def create_custom_attributetype(name):
    """Create a custom attribute type definition"""
    return {
        'names': (name,),
        'oid': name + "-oid",
        'desc': f'Test attribute',
        'sup': (),
        'syntax': '1.3.6.1.4.1.1466.115.121.1.15',
        'syntax_len': None,
        'x_ordered': None,
        'collective': None,
        'obsolete': None,
        'single_value': None,
        'no_user_mod': None,
        'equality': None,
        'substr': None,
        'ordering': None,
        'usage': None,
        'x_origin': ('Test Schema Overflow',)
    }

def test_heap_overflow_oc_sup(topo):
    """Test heap overflow of oc superior

    :id: 10446952-2b0c-482f-9b60-524836a5760b
    :setup: Standalone instance
    :steps:
        1. Create large attribute types
        2. Create objectclass with large name
        3. Create objectclass with large superior and large must attributes
    :expectedresults:
        1. The attributes are created successfully
        2. The objectclass is created successfully
        3. The objectclass is created and does not crash the server
    """

    inst = topo.standalone

    # We need very large/valid attributes in the schema as well as a very large
    # superior objectclass to exceed the max buffer size and trigger the buffer
    # overflow
    large_sup = 'a' * 1000
    large_attr1 = 'b' * 3950
    large_attr2 = 'c' * 3950

    schema = Schema(inst)

    # Create attributes with large names
    custom_at = create_custom_attributetype(large_attr1)
    schema.add_attributetype(custom_at)
    custom_at = create_custom_attributetype(large_attr2)
    schema.add_attributetype(custom_at)

    # Create objectclass with large name
    params = OBJECT_MODEL_PARAMS[ObjectClass].copy()
    params.update({
        'names': (large_sup,),
        'oid': 'test-overflow-sup-oid',
        'desc': 'superior oc',
        'must': ('cn', 'sn'),
        'sup': ('top',),
    })
    schema.add_objectclass(params)

    # Create objectclass with large superior and large must attributes
    params = OBJECT_MODEL_PARAMS[ObjectClass].copy()
    params.update({
        'names': ('largeSupObjectclass',),
        'oid': 'test-overflow-oid',
        'desc': 'To test superior heap overflow',
        'must': (large_attr1, large_attr2),
        'may': ('displayName',),
        'sup': (large_sup,),
    })
    schema.add_objectclass(params)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
