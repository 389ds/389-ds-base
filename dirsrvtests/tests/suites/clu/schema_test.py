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
        'x_origin': X_ORG_VAL,
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
    assert attr_result['at']['x_origin'][0][0] == X_ORG_VAL


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
