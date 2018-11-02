# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
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
from lib389.schema import Schema
from lib389.topologies import topology_st as topo

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_schema(topo):
    """Basic schema querying test

    :id: 995acc60-243b-45b0-9c1c-12bbe6a2882f
    :setup: Standalone Instance
    :steps:
        1. Get attribute info for 'uid'
        2. Check the attribute is found in the expected objectclasses (must, may)
        3. Check that the 'account' objectclass is found
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    must_expect = ['uidObject', 'account', 'posixAccount', 'shadowAccount', 'ipaUser',
                   'sambaSamAccount']
    may_expect = ['cosDefinition', 'inetOrgPerson', 'inetUser',
                  'mailRecipient', 'nsOrgPerson', 'ipaUserOverride']
    attrtype, must, may = topo.standalone.schema.query_attributetype('uid')
    assert attrtype.names == ('uid', 'userid')
    for oc in must:
        assert oc.names[0] in must_expect
    for oc in may:
        assert oc.names[0] in may_expect
    assert topo.standalone.schema.query_objectclass('account').names == \
        ('account', )


def test_schema_reload(topo):
    """Run a schema reload task

    :id: 995acc60-243b-45b0-9c1c-12bbe6a2882e
    :setup: Standalone Instance
    :steps:
        1. Add schema reload task
        2. Schema reload task succeeds
    :expectedresults:
        1. Success
        2. Success
    """

    schema = Schema(topo.standalone)
    task = schema.reload()
    assert task.exists()
    task.wait()
    assert task.get_exit_code() == 0


def test_x_origin(topo):
    """ Test that the various forms of X-ORIGIN are handled correctly

    :id: 995acc60-243b-45b0-9c1c-12bbe6a2882d
    :setup: Standalone Instance
    :steps:
        1. Create schema file with specific/unique formats for X-ORIGIN
        2. Reload the schema file (schema reload task)
        3. List all attributes without error
        4. Confirm the expected results
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    # Create a custom schema file so we can tests specific formats
    schema_file_name = topo.standalone.schemadir + '/98test.ldif'
    schema_file = open(schema_file_name, "w")
    testschema = ("dn: cn=schema\n" +
                  "attributetypes: ( 8.9.10.11.12.13.16 NAME 'testattr' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN 'USER_DEFINED' )\n" +
                  "attributetypes: ( 8.9.10.11.12.13.17 NAME 'testattrtwo' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN ( 'USER_DEFINED' 'should be not ignored!!' ) )\n")
    schema_file.write(testschema)
    schema_file.close()

    # Reload the schema
    myschema = Schema(topo.standalone)
    task = myschema.reload()
    assert task.exists()
    task.wait()
    assert task.get_exit_code() == 0

    # Now search attrs and make sure there are no errors
    myschema.get_attributetypes()
    myschema.get_objectclasses()

    # Check we have X-ORIGIN as expected
    assert " 'USER_DEFINED' " in str(myschema.query_attributetype("testattr"))
    assert " 'USER_DEFINED' " in str(myschema.query_attributetype("testattrtwo"))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

