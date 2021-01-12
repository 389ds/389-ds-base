# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import pytest
import ldap
import os
from lib389.topologies import topology_st as topo
from lib389._constants import TASK_WAIT

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

INVALID_SCHEMA = 'givenName $ cn $ MoZiLLaATTRiBuTe'


def test_valid_schema(topo):
    """Test schema-reload task with valid schema

    :id: 2ab304c0-3e58-4d34-b23b-a14b5997c7a8
    :setup: Standalone instance
    :steps:
        1. Create schema file with valid schema
        2. Run schema-reload.pl script
        3. Run ldapsearch and check if schema was added
    :expectedresults:
        1. File creation should work
        2. The schema reload task should be successful
        3. Searching the server should return the new schema
    """

    log.info("Test schema-reload task with valid schema")

    # Step 1 - Create schema file
    log.info("Create valid schema file (99user.ldif)...")
    schema_filename = (topo.standalone.schemadir + "/99user.ldif")
    try:
        with open(schema_filename, 'w') as schema_file:
            schema_file.write("dn: cn=schema\n")
            schema_file.write("attributetypes: ( 8.9.10.11.12.13.13 NAME " +
                              "'ValidAttribute' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15" +
                              " X-ORIGIN 'Mozilla Dummy Schema' )\n")
            schema_file.write("objectclasses: ( 1.2.3.4.5.6.7.8 NAME 'TestObject' " +
                              "SUP top MUST ( objectclass $ cn ) MAY ( givenName $ " +
                              "sn $ ValidAttribute ) X-ORIGIN 'user defined' )')\n")
        os.chmod(schema_filename, 0o777)
    except OSError as e:
        log.fatal("Failed to create schema file: " +
                  "{} Error: {}".format(schema_filename, str(e)))

    # Step 2 - Run the schema-reload task
    log.info("Run the schema-reload task...")
    reload_result = topo.standalone.tasks.schemaReload(args={TASK_WAIT: True})
    if reload_result != 0:
        log.fatal("The schema reload task failed")
        assert False
    else:
        log.info("The schema reload task worked as expected")

    # Step 3 - Verify valid schema was added to the server
    log.info("Check cn=schema to verify the valid schema was added")
    subschema = topo.standalone.schema.get_subschema()

    oc_obj = subschema.get_obj(ldap.schema.ObjectClass, 'TestObject')
    assert oc_obj is not None, "The new objectclass was not found on server"

    at_obj = subschema.get_obj(ldap.schema.AttributeType, 'ValidAttribute')
    assert at_obj is not None, "The new attribute was not found on server"


def test_invalid_schema(topo):
    """Test schema-reload task with invalid schema

    :id: 2ab304c0-3e58-4d34-b23b-a14b5997c7a9
    :setup: Standalone instance
    :steps:
        1. Create schema files with invalid schema
        2. Run schema-reload.pl script
        3. Run ldapsearch and check if schema was added
    :expectedresults:
        1. File creation should work
        2. The schema reload task should return an error
        3. Searching the server should not return the invalid schema
    """
    log.info("Test schema-reload task with invalid schema")

    # Step 1 - Create schema files: one valid, one invalid
    log.info("Create valid schema file (98user.ldif)...")
    schema_filename = (topo.standalone.schemadir + "/98user.ldif")
    try:
        with open(schema_filename, 'w') as schema_file:
            schema_file.write("dn: cn=schema\n")
            schema_file.write("attributetypes: ( 8.9.10.11.12.13.14 NAME " +
                              "'MozillaAttribute' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15" +
                              " X-ORIGIN 'Mozilla Dummy Schema' )\n")
            schema_file.write("objectclasses: ( 1.2.3.4.5.6.7 NAME 'MoZiLLaOBJeCT' " +
                              "SUP top MUST ( objectclass $ cn ) MAY ( givenName $ " +
                              "sn $ MozillaAttribute ) X-ORIGIN 'user defined' )')\n")
        os.chmod(schema_filename, 0o777)
    except OSError as e:
        log.fatal("Failed to create schema file: " +
                  "{} Error: {}".format(schema_filename, str(e)))

    log.info("Create invalid schema file (99user.ldif)...")
    schema_filename = (topo.standalone.schemadir + "/99user.ldif")
    try:
        with open(schema_filename, 'w') as schema_file:
            schema_file.write("dn: cn=schema\n")
            # Same attribute/objclass names, but different OIDs and MAY attributes
            schema_file.write("attributetypes: ( 8.9.10.11.12.13.140 NAME " +
                              "'MozillaAttribute' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15" +
                              " X-ORIGIN 'Mozilla Dummy Schema' )\n")
            schema_file.write("objectclasses: ( 1.2.3.4.5.6.70 NAME 'MoZiLLaOBJeCT' " +
                              "SUP top MUST ( objectclass $ cn ) MAY ( givenName $ " +
                              "cn $ MoZiLLaATTRiBuTe ) X-ORIGIN 'user defined' )')\n")
        os.chmod(schema_filename, 0o777)
    except OSError as e:
        log.fatal("Failed to create schema file: " +
                  "{} Error: {}".format(schema_filename, str(e)))

    # Step 2 - Run the schema-reload task
    log.info("Run the schema-reload task, it should fail...")
    reload_result = topo.standalone.tasks.schemaReload(args={TASK_WAIT: True})
    if reload_result == 0:
        log.fatal("The schema reload task incorectly reported success")
        assert False
    else:
        log.info("The schema reload task failed as expected:" +
                 " error {}".format(reload_result))

    # Step 3 - Verify invalid schema was not added to the server
    log.info("Check cn=schema to verify the invalid schema was not added")
    subschema = topo.standalone.schema.get_subschema()
    oc_obj = subschema.get_obj(ldap.schema.ObjectClass, 'MoZiLLaOBJeCT')
    if oc_obj is not None and INVALID_SCHEMA in str(oc_obj):
        log.fatal("The invalid schema was returned from the server: " + str(oc_obj))
        assert False
    else:
        log.info("The invalid schema is not present on the server")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

