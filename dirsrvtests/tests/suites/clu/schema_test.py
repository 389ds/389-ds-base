# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
from lib389.topologies import topology_st as topo
from lib389.schema import Schema

pytestmark = pytest.mark.tier0
log = logging.getLogger(__name__)


def test_origins_with_extra_parenthesis(topo):
    """Test the custom schema with extra parenthesis in X-ORIGIN can be parsed
    into JSON

    :id: 4230f83b-0dc3-4bc4-a7a8-5ab0826a4f05
    :setup: Standalone Instance
    :steps:
        1. Add attribute with X-ORIGIN that contains extra parenthesis
        2. Querying for that attribute with JSON flag
    :expectedresults:
        1. Success
        2. Success
    """

    ATTR_NAME = 'testAttribute'
    X_ORG_VAL = 'test (TEST)'
    schema = Schema(topo.standalone)

    # Add new attribute
    parameters = {
        'names': [ATTR_NAME],
        'oid': '1.1.1.1.1.1.1.22222',
        'desc': 'Test extra parenthesis in X-ORIGIN',
        'x_origin': [X_ORG_VAL],
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
        'sup': None
    }
    schema.add_attributetype(parameters)

    # Search for attribute with JSON option
    attr_result = schema.query_attributetype(ATTR_NAME, json=True)

    # Verify the x-origin value is correct
    assert attr_result['at']['x_origin'][0] == X_ORG_VAL


schema_params = [
    ['attr1', '99999.1', None],
    ['attr2', '99999.2', 'test-str'],
    ['attr3', '99999.3', ['test-list']],
    ['attr4', '99999.4', ('test-tuple')],
]
@pytest.mark.parametrize("name, oid, xorg", schema_params)
def test_origins(topo, name, oid, xorg):
    """Test the various possibilities of x-origin

    :id: 3229f6f8-67c1-4558-9be5-71434283086a
    :setup: Standalone Instance
    :steps:
        1. Add an attribute with different x-origin values/types
    :expectedresults:
        1. Success
    """

    schema = Schema(topo.standalone)

    # Add new attribute
    parameters = {
        'names': [name],
        'oid': oid,
        'desc': 'Test X-ORIGIN',
        'x_origin': xorg,
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
        'sup': None
    }
    schema.add_attributetype(parameters)


def test_mrs(topo):
    """Test an attribute can be added with a matching rule

    :id: e4eb06e0-7f80-41fe-8868-08c2bafc7590
    :setup: Standalone Instance
    :steps:
        1. Add an attribute with a matching rule
    :expectedresults:
        1. Success
    """
    schema = Schema(topo.standalone)

    # Add new attribute
    parameters = {
        'names': ['test-mr'],
        'oid': '99999999',
        'desc': 'Test matching rule',
        'syntax': '1.3.6.1.4.1.1466.115.121.1.15',
        'syntax_len': None,
        'x_ordered': None,
        'collective': None,
        'obsolete': None,
        'single_value': None,
        'no_user_mod': None,
        'equality': None,
        'substr': 'numericstringsubstringsmatch',
        'ordering': None,
        'usage': None,
        'sup': None
    }
    schema.add_attributetype(parameters)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
