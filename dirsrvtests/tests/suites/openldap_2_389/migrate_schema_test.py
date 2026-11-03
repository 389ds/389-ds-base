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
import tempfile

import ldap
import pytest

from lib389.schema import Schema
from lib389.migrate.openldap.schema import (
    apply_schema_ldif_online,
    convert_openldap_schema_text,
    install_schema_ldif_to_schemadir,
    write_schema_ldif,
)
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

SCHEMADIR_TEST_LDIF = '98openldap-migrate-schemadir-test.ldif'

SAMPLE_SCHEMA = """
attributetype ( 1.3.6.1.4.1.6152.10.3.1.26
\tNAME 'sendmailMTAAliasSearch'
\tDESC 'recursive search for values of an alias'
\tEQUALITY caseExactMatch
\tSYNTAX 1.3.6.1.4.1.1466.115.121.1.15 SINGLE-VALUE )
\t
objectclass ( 1.3.6.1.4.1.6152.10.3.2.14
\tNAME 'sendmailMTAAliasObject'
\tSUP sendmailMTAAlias STRUCTURAL
\tDESC 'Sendmail MTA alias object'
\tMUST sendmailMTAKey
\tMAY ( sendmailMTAAliasGrouping $ sendmailMTAAliasCluster $
\t\tsendmailMTAHost $ sendmailMTAAliasValue $
\t\tsendmailMTAAliasSearch $ sendmailMTAAliasURL $ Description ) )
"""

SCHEMADIR_OPENLDAP_SCHEMA = """
attributetype ( 1.3.6.1.4.1.52498.10.1.1
\tNAME 'olMigrateSchemadirAttr'
\tDESC 'Attribute for openldap .schema schemadir migration test'
\tEQUALITY caseIgnoreMatch
\tSYNTAX 1.3.6.1.4.1.1466.115.121.1.15
\tX-ORIGIN 'openldap migrate test' )

objectclass ( 1.3.6.1.4.1.52498.10.2.1
\tNAME 'olMigrateSchemadirObject'
\tSUP top STRUCTURAL
\tDESC 'Objectclass for openldap .schema schemadir migration test'
\tMUST ( objectclass $ cn )
\tMAY ( olMigrateSchemadirAttr )
\tX-ORIGIN 'openldap migrate test' )
"""

INHERITED_ATTR_OPENLDAP_SCHEMA = """
attributetype ( 1.3.6.1.4.1.52498.99.1
\tNAME 'olInheritedName'
\tSUP name )
"""

ONLINE_OPENLDAP_SCHEMA = """
attributetype ( 1.3.6.1.4.1.52498.10.1.2
\tNAME 'olMigrateOnlineAttr'
\tDESC 'Attribute for openldap .schema online migration test'
\tEQUALITY caseIgnoreMatch
\tSYNTAX 1.3.6.1.4.1.1466.115.121.1.15
\tX-ORIGIN 'openldap migrate test' )

objectclass ( 1.3.6.1.4.1.52498.10.2.2
\tNAME 'olMigrateOnlineObject'
\tSUP top STRUCTURAL
\tDESC 'Objectclass for openldap .schema online migration test'
\tMUST ( objectclass $ cn )
\tMAY ( olMigrateOnlineAttr )
\tX-ORIGIN 'openldap migrate test' )
"""


@pytest.fixture(scope="module")
def schemadir_schema_file(topo, request):
    """Remove schemadir test LDIF when the module finishes (unless DEBUGGING)."""
    inst = topo.standalone
    path = os.path.join(inst.schemadir, SCHEMADIR_TEST_LDIF)

    def fin():
        if DEBUGGING:
            return
        if os.path.isfile(path):
            os.remove(path)
            task = Schema(inst).reload(schema_dir=inst.schemadir)
            task.wait()
            assert task.get_exit_code() == 0

    request.addfinalizer(fin)
    return SCHEMADIR_TEST_LDIF


def test_convert_openldap_schema_example():
    """Convert multi-line OpenLDAP .schema definitions to 389 LDIF

    :id: 8f3c2a1e-5b4d-4e91-9c0a-1d2e3f4a5b6c
    :setup: None
    :steps:
        1. Convert sample sendmail schema text
    :expectedresults:
        1. LDIF contains dn, single-line attributeTypes and objectClasses
    """
    result = convert_openldap_schema_text(SAMPLE_SCHEMA)
    assert 'dn: cn=schema\n' in result
    assert (
        "attributeTypes: ( 1.3.6.1.4.1.6152.10.3.1.26 NAME 'sendmailMTAAliasSearch' "
        "DESC 'recursive search for values of an alias' EQUALITY caseExactMatch "
        "SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 SINGLE-VALUE )"
    ) in result
    assert (
        "objectClasses: ( 1.3.6.1.4.1.6152.10.3.2.14 NAME 'sendmailMTAAliasObject' "
        "SUP sendmailMTAAlias STRUCTURAL DESC 'Sendmail MTA alias object' "
        "MUST sendmailMTAKey MAY ( sendmailMTAAliasGrouping $ sendmailMTAAliasCluster $ "
        "sendmailMTAHost $ sendmailMTAAliasValue $ sendmailMTAAliasSearch $ "
        "sendmailMTAAliasURL $ Description ) )"
    ) in result


def test_convert_preserves_comments_and_blank_lines():
    """Comment and blank lines survive conversion

    :id: 7e2b1a0d-4c3e-4d80-8b9f-0c1d2e3f4a5b
    :setup: None
    :steps:
        1. Convert schema text with comments and blank lines
    :expectedresults:
        1. Comments unchanged and blank lines become #
    """
    text = "# custom schema\n\nattributetype ( 1.2.3 NAME 'x' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 )\n"
    result = convert_openldap_schema_text(text)
    assert result.startswith('# custom schema\n#\n')
    assert "attributeTypes: ( 1.2.3 NAME 'x' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 )" in result


def test_convert_preserves_whitespace_inside_quotes():
    """Whitespace inside quoted strings is not collapsed

    :id: 6d4e8f0a-1b2c-4d5e-8f90-a1b2c3d4e5f6
    :setup: None
    :steps:
        1. Convert schema with multiple spaces inside NAME and DESC quotes
    :expectedresults:
        1. Inner spaces in quoted values are preserved
    """
    text = (
        "attributetype ( 1.2.3 NAME 'foo  bar' "
        "DESC 'a  b  c' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 )\n"
    )
    result = convert_openldap_schema_text(text)
    assert "NAME 'foo  bar'" in result
    assert "DESC 'a  b  c'" in result


def test_convert_rejects_unclosed_definition():
    """Unclosed schema definitions raise an error

    :id: e9213430-7e5b-4229-8a08-a72f25759a44
    :setup: None
    :steps:
        1. Convert schema text with an unclosed attributetype
    :expectedresults:
        1. ValueError is raised
    """
    text = "attributetype ( 1.2.3 NAME 'x' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15\n"
    with pytest.raises(ValueError, match='Unclosed'):
        convert_openldap_schema_text(text)


def test_write_schema_ldif_refuses_overwrite():
    """write_schema_ldif does not clobber an existing file

    :id: 2c8f4e91-0d3a-4b5c-9e7f-1a2b3c4d5e6f
    :setup: None
    :steps:
        1. Create a file at the target path
        2. Call write_schema_ldif with the same path
    :expectedresults:
        1. Success
        2. ValueError is raised
    """
    fd, path = tempfile.mkstemp(suffix='.ldif')
    os.close(fd)
    try:
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write('existing\n')
        with pytest.raises(ValueError, match='already exists'):
            write_schema_ldif(path, 'dn: cn=schema\n')
    finally:
        os.remove(path)


def test_install_converted_schema_via_schemadir(topo, schemadir_schema_file):
    """Converted schema LDIF in schemadir loads after reload and survives restart

    :id: c0f30ae8-97e4-43f5-a55e-acd4224ecb00
    :setup: Standalone instance
    :steps:
        1. Convert OpenLDAP .schema text to LDIF
        2. Install the LDIF in the instance schema directory and reload schema
        3. Verify the new attribute and objectclass are visible
        4. Restart the directory server
        5. Verify the server is running and schema is still present
    :expectedresults:
        1. Success
        2. Schema reload task completes successfully
        3. Success
        4. Success
        5. Success
    """
    inst = topo.standalone
    ldif_text = convert_openldap_schema_text(SCHEMADIR_OPENLDAP_SCHEMA)

    install_schema_ldif_to_schemadir(
        inst, ldif_text, filename=schemadir_schema_file, log=log)

    schema = Schema(inst)
    assert schema.query_attributetype('olMigrateSchemadirAttr') is not None
    assert schema.query_objectclass('olMigrateSchemadirObject') is not None

    inst.restart()
    assert inst.status()

    schema = Schema(inst)
    assert schema.query_attributetype('olMigrateSchemadirAttr') is not None
    assert schema.query_objectclass('olMigrateSchemadirObject') is not None

    subschema = inst.schema.get_subschema()
    at_obj = subschema.get_obj(ldap.schema.AttributeType, 'olMigrateSchemadirAttr')
    oc_obj = subschema.get_obj(ldap.schema.ObjectClass, 'olMigrateSchemadirObject')
    assert at_obj is not None
    assert oc_obj is not None


def test_apply_inherited_attributetype_online(topo):
    """SUP attribute types without explicit syntax can be reapplied online

    :id: c19607a8-dfb9-49e7-8e3a-5127f06da50d
    :setup: Standalone instance
    :steps:
        1. Convert OpenLDAP schema with attribute SUP name (no SYNTAX)
        2. Apply over LDAP twice
    :expectedresults:
        1. Success
        2. Success (existing definition skipped)
    """
    inst = topo.standalone
    ldif_text = convert_openldap_schema_text(INHERITED_ATTR_OPENLDAP_SCHEMA)

    apply_schema_ldif_online(inst, ldif_text, log)
    apply_schema_ldif_online(inst, ldif_text, log)

    schema = Schema(inst)
    assert schema.query_attributetype('olInheritedName') is not None


def test_apply_converted_schema_online(topo):
    """Converted schema can be added over LDAP while the server is running

    :id: 547d7d5b-709c-46f2-992a-31784fe7b49f
    :setup: Standalone instance
    :steps:
        1. Convert OpenLDAP .schema text to LDIF
        2. Add schema elements with an LDAP modify on cn=schema
        3. Apply the same schema again over LDAP
        4. Verify the new attribute and objectclass are visible
    :expectedresults:
        1. Success
        2. Success
        3. Success (existing definitions skipped)
        4. Success
    """
    inst = topo.standalone
    ldif_text = convert_openldap_schema_text(ONLINE_OPENLDAP_SCHEMA)

    apply_schema_ldif_online(inst, ldif_text, log)
    apply_schema_ldif_online(inst, ldif_text, log)

    schema = Schema(inst)
    assert schema.query_attributetype('olMigrateOnlineAttr') is not None
    assert schema.query_objectclass('olMigrateOnlineObject') is not None

    subschema = inst.schema.get_subschema()
    at_obj = subschema.get_obj(ldap.schema.AttributeType, 'olMigrateOnlineAttr')
    oc_obj = subschema.get_obj(ldap.schema.ObjectClass, 'olMigrateOnlineObject')
    assert at_obj is not None
    assert oc_obj is not None
