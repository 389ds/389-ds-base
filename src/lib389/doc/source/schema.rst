Schema
==========

Usage example
--------------
::

    # Get the schema as an LDAP entry
    schema = standalone.schema.get_entry()
     
    # Get the schema as a python-ldap SubSchema object
    subschema = standalone.schema.get_subschema()
     
    # Get a list of the schema files in the instance schemadir
    schema_files = standalone.schema.list_files()
     
     
    # Convert the given schema file name to its python-ldap format suitable for passing to ldap.schema.SubSchema()
    parsed = standalone.schema.file_to_ldap('/full/path/to/file.ldif')
     
    # Convert the given schema file name to its python-ldap format ldap.schema.SubSchema object
    parsed = standalone.schema.file_to_subschema('/full/path/to/file.ldif')
     
    # Add a schema element to the schema
    standalone.schema.add_schema(attr, val)
     
    # Delete a schema element from the schema
    standalone.schema.del_schema(attr, val)
     
    # Add 'attributeTypes' definition to the schema
    standalone.schema.add_attribute(attributes)
     
    # Add 'objectClasses' definition to the schema
    standalone.schema.add_objectclass(objectclasses)
     
    # Get a schema nsSchemaCSN attribute
    schema_csn = standalone.schema.get_schema_csn()
     
    # Get a list of ldap.schema.models.ObjectClass objects for all objectClasses supported by this instance
    objectclasses = standalone.schema.get_objectclasses()
     
    # Get a list of ldap.schema.models.AttributeType objects for all attributeTypes supported by this instance
    attributetypes = standalone.schema.get_attributetypes()
     
    # Get a list of the server defined matching rules
    matchingrules = standalone.schema.get_matchingrules()
     
    # Get a single matching rule instance that matches the mr_name. Returns None if the matching rule doesn't exist
    matchingrule = standalone.schema.query_matchingrule(matchingrule_name)
     
    # Get a single ObjectClass instance that matches objectclassname. Returns None if the objectClass doesn't exist
    objectclass = standalone.schema.query_objectclass(objectclass_name)
         
    # Returns a tuple of the AttributeType, and what objectclasses may or must take this attributeType. Returns None if attributetype doesn't
    (attributetype, may, must) = standalone.schema.query_attributetype(attributetype_name)


Module documentation
-----------------------

.. autoclass:: lib389.schema.Schema
   :members:
