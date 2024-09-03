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
from ldap.schema.models import AttributeType, ObjectClass
from lib389.topologies import topology_st as topo
from lib389.schema import Schema, OBJECT_MODEL_PARAMS 

pytestmark = pytest.mark.tier0
log = logging.getLogger(__name__)

@pytest.fixture(scope="function")
def create_attribute(request, topo):
    schema = Schema(topo.standalone)
    attr_name = request.param['name']
    oid = request.param['oid']
    desc = request.param.get('desc', 'Test attribute')
    syntax = request.param.get('syntax', '1.3.6.1.4.1.1466.115.121.1.15')
    x_origin = request.param.get('x_origin', None)
    sup = request.param.get('sup', None)
    substr = request.param.get('substr', None)

    parameters = OBJECT_MODEL_PARAMS[AttributeType].copy()
    parameters.update({
        'names': (attr_name,),
        'oid': oid,
        'desc': desc,
        'syntax': syntax,
    })
    if x_origin:
        parameters['x_origin'] = x_origin
    if sup:
        parameters['sup'] = sup
    if substr:
        parameters['substr'] = substr

    schema.add_attributetype(parameters)

    def fin():
        schema.remove_attributetype(attr_name)

    request.addfinalizer(fin)
    return attr_name, schema


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
    parameters = OBJECT_MODEL_PARAMS[AttributeType].copy()
    parameters.update({
        'names': (ATTR_NAME,),
        'oid': '1.1.1.1.1.1.1.22222',
        'desc': 'Test extra parenthesis in X-ORIGIN',
        'syntax': '1.3.6.1.4.1.1466.115.121.1.15',
        'x_origin': (X_ORG_VAL,),
    })
    schema.add_attributetype(parameters)

    # Search for attribute with JSON option
    attr_result = schema.query_attributetype(ATTR_NAME, json=True)

    # Verify the x-origin value is correct
    assert attr_result['at']['x_origin'][0] == X_ORG_VAL

    # Clean up
    schema.remove_attributetype(ATTR_NAME)


schema_params = [
    {'name': 'attr1', 'oid': '99999.1', 'x_origin': None},
    {'name': 'attr2', 'oid': '99999.2', 'x_origin': 'test-str'},
    {'name': 'attr3', 'oid': '99999.3', 'x_origin': ['test-list']},
    {'name': 'attr4', 'oid': '99999.4', 'x_origin': ('test-tuple',)},
]
@pytest.mark.parametrize("create_attribute", schema_params, indirect=True)
def test_origins(create_attribute):
    """Test the various possibilities of x-origin

    :id: 3229f6f8-67c1-4558-9be5-71434283086a
    :setup: Standalone Instance
    :steps:
        1. Add an attribute with different x-origin values/types
    :expectedresults:
        1. Success
    """
    attr_name, schema = create_attribute
    attr_result = schema.query_attributetype(attr_name, json=True)
    assert attr_result['at']['names'][0] == attr_name


@pytest.mark.parametrize("create_attribute", [{'name': 'test-mr', 'oid': '99999999', 'desc': 'Test matching rule', 'substr': 'numericstringsubstringsmatch'}], indirect=True)
def test_mrs(create_attribute):
    """Test an attribute can be added with a matching rule

    :id: e4eb06e0-7f80-41fe-8868-08c2bafc7590
    :setup: Standalone Instance
    :steps:
        1. Add an attribute with a matching rule
    :expectedresults:
        1. Success
    """
    attr_name, schema = create_attribute
    attr_result = schema.query_attributetype(attr_name, json=True)
    assert attr_result['at']['names'][0] == attr_name


@pytest.mark.parametrize("create_attribute", [{'name': 'testEditAttr', 'oid': '1.2.3.4.5.6.7.8888', 'desc': 'Test edit attribute type'}], indirect=True)
def test_edit_attributetype(create_attribute):
    """Test editing an attribute type in the schema

    :id: 07c98f6a-89f8-44e5-9cc1-353d1f7bccf4
    :setup: Standalone Instance
    :steps:
        1. Add a new attribute type
        2. Edit the attribute type
        3. Verify the changes
    :expectedresults:
        1. Success
        2. Success
        3. Changes are reflected correctly
    """
    attr_name, schema = create_attribute

    # Edit the attribute type
    edit_parameters = {
        'desc': 'Updated description for test edit attribute type',
        'syntax': '1.3.6.1.4.1.1466.115.121.1.26',  # IA5String
    }
    schema.edit_attributetype(attr_name, edit_parameters)

    # Verify the changes
    edited_attr = schema.query_attributetype(attr_name, json=True)
    assert edited_attr['at']['desc'][0] == 'Updated description for test edit attribute type'
    assert edited_attr['at']['syntax'][0] == '1.3.6.1.4.1.1466.115.121.1.26'


def test_edit_objectclass(topo):
    """Test editing an object class in the schema

    :id: b160cd33-c232-4f45-bc7c-1dd8ad20f857
    :setup: Standalone Instance
    :steps:
        1. Add a new object class
        2. Edit the object class
        3. Verify the changes
    :expectedresults:
        1. Success
        2. Success
        3. Changes are reflected correctly
    """

    schema = Schema(topo.standalone)

    # Add new object class
    oc_name = 'testEditOC'
    parameters = OBJECT_MODEL_PARAMS[ObjectClass].copy()
    parameters.update({
        'names': (oc_name,),
        'oid': '1.2.3.4.5.6.7.9999',
        'desc': 'Test edit object class',
        'must': ('cn',),
        'may': ('description',),
    })
    schema.add_objectclass(parameters)

    # Edit the object class
    edit_parameters = {
        'desc': 'Updated description for test edit object class',
        'must': ('cn', 'sn'),
        'may': ('description', 'telephoneNumber'),
    }
    schema.edit_objectclass(oc_name, edit_parameters)

    # Verify the changes
    edited_oc = schema.query_objectclass(oc_name, json=True)
    assert edited_oc['oc']['desc'][0] == 'Updated description for test edit object class'
    assert set(edited_oc['oc']['must']) == set(['cn', 'sn'])
    assert set(edited_oc['oc']['may']) == set(['description', 'telephoneNumber'])

    # Clean up
    schema.remove_objectclass(oc_name)


@pytest.mark.parametrize("create_attribute", [{'name': 'testEditAttrSup', 'oid': '1.2.3.4.5.6.7.7777', 'desc': 'Test edit attribute type with superior', 'sup': ('name',)}], indirect=True)
def test_edit_attributetype_remove_superior(create_attribute):
    """Test editing an attribute type to remove a parameter from it

    :id: bd6ae89f-9617-4620-adc2-465884ca568b
    :setup: Standalone Instance
    :steps:
        1. Add a new attribute type with a superior
        2. Edit the attribute type to remove the superior
        3. Verify the changes and the removal of inherited matching rules
    :expectedresults:
        1. Success
        2. Success
        3. Superior is removed and inherited matching rules are cleared
    """
    attr_name, schema = create_attribute

    # Verify the attribute was created with a superior
    initial_attr = schema.query_attributetype(attr_name, json=True)
    assert 'sup' in initial_attr['at'] and initial_attr['at']['sup'][0] == 'name', "Superior not set correctly"

    # Edit the attribute type to remove the superior
    edit_parameters = {
        'sup': None,
    }
    schema.edit_attributetype(attr_name, edit_parameters)

    # Verify the changes
    edited_attr = schema.query_attributetype(attr_name, json=True)
    assert 'sup' not in edited_attr['at'] or not edited_attr['at']['sup'], "Superior not removed"
    assert 'equality' not in edited_attr['at'] or not edited_attr['at']['equality'], "Equality matching rule not cleared"
    assert 'ordering' not in edited_attr['at'] or not edited_attr['at']['ordering'], "Ordering matching rule not cleared"
    assert 'substr' not in edited_attr['at'] or not edited_attr['at']['substr'], "Substring matching rule not cleared"


@pytest.mark.parametrize("create_attribute", [{'name': 'testCustomAttr', 'oid': '1.2.3.4.5.6.7.8888', 'desc': 'Initial description for custom attribute'}], indirect=True)
def test_edit_attribute_keep_custom_values(create_attribute):
    """Test editing a custom schema attribute keeps all custom values

    :id: 5b1e2e8b-28c2-4f77-9c03-07eff20f763d
    :setup: Standalone Instance
    :steps:
        1. Create a custom attribute
        2. Edit the attribute and change the description
        3. Verify that both the description and OID are kept
        4. Edit the attribute again and change the OID
        5. Verify that both the description and OID are kept
    :expectedresults:
        1. Success
        2. Success
        3. Both custom description and OID are preserved
        4. Success
        5. Both custom description and OID are preserved
    """
    attr_name, schema = create_attribute
    initial_oid = '1.2.3.4.5.6.7.8888'
    initial_desc = 'Initial description for custom attribute'

    # Verify the attribute was created correctly
    attr = schema.query_attributetype(attr_name, json=True)
    assert attr['at']['oid'][0] == initial_oid
    assert attr['at']['desc'][0] == initial_desc

    # Edit the attribute and change the description
    new_desc = 'Updated description for custom attribute'
    edit_parameters = {
        'desc': new_desc,
    }
    schema.edit_attributetype(attr_name, edit_parameters)

    # Verify that both the description and OID are kept
    edited_attr = schema.query_attributetype(attr_name, json=True)
    assert edited_attr['at']['oid'][0] == initial_oid, f"OID changed unexpectedly to {edited_attr['at']['oid'][0]}"
    assert edited_attr['at']['desc'][0] == new_desc, f"Description not updated, current value: {edited_attr['at']['desc'][0]}"

    # Edit the attribute again and change the OID
    new_oid = '1.2.3.4.5.6.7.9999'
    edit_parameters = {
        'oid': new_oid,
    }
    schema.edit_attributetype(attr_name, edit_parameters)

    # Verify that both the description and OID are kept
    re_edited_attr = schema.query_attributetype(attr_name, json=True)
    assert re_edited_attr['at']['oid'][0] == new_oid, f"OID not updated, current value: {re_edited_attr['at']['oid'][0]}"
    assert re_edited_attr['at']['desc'][0] == new_desc, f"Description changed unexpectedly to {re_edited_attr['at']['desc'][0]}"

    # Additional check: Ensure the attribute name wasn't used as the OID
    assert re_edited_attr['at']['oid'][0] != f"{attr_name}-oid", "OID was incorrectly set to '<ATTRIBUTE_NAME>-oid'"


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
