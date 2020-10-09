# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import pytest
import time, ldap, re, os
from lib389.schema import Schema
from lib389.utils import ensure_bytes
from lib389.topologies import topology_st as topo
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PW_DM
from lib389._mapped_object import DSLdapObjects
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

INVALID_SCHEMA = 'givenName $ cn $ MoZiLLaATTRiBuTe'


def test_schema_reload_with_searches(topo):
    """Test that during the schema reload task there is a small window where the new schema is not loaded
       into the asi hashtables - this results in searches not returning entries.

    :id: 375f1fdc-a9ef-45de-984d-0b79a40ff219
    :setup: Standalone instance
    :steps:
        1. Create a test user
        2. Run a schema_reload task while searching for our user
        3. While we wait for the task to complete search for our user
        4. Check the user is still being returned and if task is complete
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. Operation should be successful
    """

    log.info('Test the searches still work as expected during schema reload tasks')

    # Add a user
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user(uid=1)

    # Run a schema_reload tasks while searching for our user.Since
    # this is a race condition, run it several times.
    schema = Schema(topo.standalone)
    task = schema.reload(schema_dir=topo.standalone.schemadir)

    # While we wait for the task to complete search for our user
    search_count = 0
    while search_count < 10:
    # Now check the user is still being returned
    # Check if task is complete
        assert user.exists()
        if task.get_exit_code() == 0:
            break
        time.sleep(1)
        search_count += 1


def test_schema_operation(topo):
    """Test that the cases in original schema are preserved.
       Test that duplicated schema except cases are not loaded
       Test to use a custom schema

    :id: e7448863-ac62-4b49-b013-4efa412c0455
    :setup: Standalone instance
    :steps:
        1. Create a test schema with cases
        2. Run a schema_reload task
        3. Check the attribute is present
        4. Case 2: Check duplicated schema except cases are not loaded
        5. Case 2-1: Use the custom schema

    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
        3. Operation should be successful
        4. Operation should be successful
        5. Operation should be successful
    """

    log.info('case 1: Test the cases in the original schema are preserved.')

    schema_filename = topo.standalone.schemadir + '/98test.ldif'
    try:
        with open(schema_filename, "w") as schema_file:
            schema_file.write("dn: cn=schema\n")
            schema_file.write("attributetypes: ( 8.9.10.11.12.13.14 NAME " +
                              "'MoZiLLaaTTRiBuTe' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 " +
                              " X-ORIGIN 'Mozilla Dummy Schema' )\n")
            schema_file.write("objectclasses: ( 1.2.3.4.5.6.7 NAME 'MozillaObject' " +
                              "SUP top MUST ( objectclass $ cn ) MAY ( MoZiLLaaTTRiBuTe )" +
                              " X-ORIGIN 'user defined' )')\n")

    except OSError as e:
        log.fatal("Failed to create schema file: " +
                  "{} Error: {}".format(schema_filename, str(e)))


    # run the schema reload task with the default schemadir
    schema = Schema(topo.standalone)
    task = schema.reload(schema_dir=topo.standalone.schemadir)
    task.wait()

    subschema = topo.standalone.schema.get_subschema()
    at_obj = subschema.get_obj(ldap.schema.AttributeType, 'MoZiLLaaTTRiBuTe')

    assert at_obj is not None, "The attribute was not found on server"

    log.info('Case 2: Duplicated schema except cases are not loaded.')

    schema_filename = topo.standalone.schemadir + '/97test.ldif'
    try:
        with open(schema_filename, "w") as schema_file:
            Mozattr1 = "MOZILLAATTRIBUTE"
            schema_file.write("dn: cn=schema\n")
            schema_file.write("attributetypes: ( 8.9.10.11.12.13.14 NAME " +
                              "'MOZILLAATTRIBUTE' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 " +
                              "X-ORIGIN 'Mozilla Dummy Schema' )\n")
            schema_file.write("objectclasses: ( 1.2.3.4.5.6.7 NAME 'MozillaObject' "+
                              "SUP top MUST ( objectclass $ cn ) MAY ( MOZILLAATTRIBUTE ) "+
                              "X-ORIGIN 'user defined' )')\n")

    except OSError as e:
        log.fatal("Failed to create schema file: " +
                  "{} Error: {}".format(schema_filename, str(e)))

    # run the schema reload task with the default schemadir
    task = schema.reload(schema_dir=topo.standalone.schemadir)
    task.wait()

    subschema_duplicate = topo.standalone.schema.get_subschema()
    at_obj_duplicate = subschema_duplicate.get_obj(ldap.schema.AttributeType, 'MOZILLAATTRIBUTE')

    moz = re.findall('MOZILLAATTRIBUTE',str(at_obj_duplicate))
    if moz:
        log.error('case 2: MOZILLAATTRIBUTE is in the objectclasses list -- FAILURE')
        assert False
    else:
        log.info('case 2: MOZILLAATTRIBUTE is not in the objectclasses list -- PASS')

    Mozattr2 = "mozillaattribute"
    log.info(f'Case 2-1: Use the custom schema with {Mozattr2}')
    name = "test_user"
    ld = ldap.initialize(topo.standalone.get_ldap_uri())
    ld.simple_bind_s(DN_DM,PW_DM)
    ld.add_s(f"cn={name},{DEFAULT_SUFFIX}",[('objectclass', [b'top', b'person', b'MozillaObject']),
                                            ('sn', [ensure_bytes(name)]),
                                            ('cn', [ensure_bytes(name)]),
                                            (Mozattr2, [ensure_bytes(name)])
                                            ])

    mozattrval = DSLdapObjects(topo.standalone,DEFAULT_SUFFIX).filter('(objectclass=mozillaobject)')[0]
    assert mozattrval.get_attr_val_utf8('mozillaattribute') == name


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
    except OSError as e:
        log.fatal("Failed to create schema file: " +
                  "{} Error: {}".format(schema_filename, str(e)))

    # Step 2 - Run the schema-reload task
    log.info("Run the schema-reload task...")
    schema = Schema(topo.standalone)
    task = schema.reload(schema_dir=topo.standalone.schemadir)
    task.wait()
    assert task.get_exit_code() == 0, "The schema reload task failed"

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
    except OSError as e:
        log.fatal("Failed to create schema file: " +
                  "{} Error: {}".format(schema_filename, str(e)))

    # Step 2 - Run the schema-reload task
    log.info("Run the schema-reload task, it should fail...")
    schema = Schema(topo.standalone)
    task = schema.reload(schema_dir=topo.standalone.schemadir)
    task.wait()
    assert task.get_exit_code() != 0, f"The schema reload task incorectly reported success{task.get_exit_code()}"

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
