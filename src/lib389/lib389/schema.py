"""
   You will access this from:
   DirSrv.schema.methodName()
"""
import os
import re
import time
import glob
import six
import ldap
from ldap.schema.models import AttributeType, ObjectClass, MatchingRule

from lib389._constants import *
from lib389.utils import normalizeDN, escapeDNValue, suffixfilt
from lib389 import Entry


class Schema(object):

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def get_entry(self):
        """get the schema as an LDAP entry"""
        attrs = ['attributeTypes', 'objectClasses']
        return self.conn.search_s(DN_SCHEMA, ldap.SCOPE_BASE,
                                  'objectclass=*', attrs)[0]

    def get_subschema(self):
        """get the schema as a python-ldap SubSchema object"""
        return ldap.schema.SubSchema(self.get_entry().data)

    def list_files(self):
        """return a list of the schema files in the instance schemadir"""
        return glob.glob(self.conn.schemadir + "/*.ldif")

    def file_to_ldap(self, filename):
        """convert the given schema file name to its python-ldap format
        suitable for passing to ldap.schema.SubSchema()
        @param filename - the full path and filename of a schema file in ldif
        format"""
        import six.moves.urllib.request
        import six.moves.urllib.parse
        import ldif

        ldif_file = six.moves.urllib.request.urlopen('file://' + filename)
        ldif_parser = ldif.LDIFRecordList(ldif_file, max_entries=1)
        if not ldif_parser:
            return None
        ldif_parser.parse()
        if not ldif_parser.all_records:
            return None
        return ldif_parser.all_records[0][1]

    def file_to_subschema(self, filename):
        """convert the given schema file name to its python-ldap format
        ldap.schema.SubSchema object
        @param filename - the full path and filename of a schema file in ldif
        format"""
        ent = self.file_to_ldap(filename)
        if not ent:
            return None
        return ldap.schema.SubSchema(ent)

    def add_schema(self, attr, val):
        """Add a schema element to the schema.
        @param attr - the attribute type to use e.g. attributeTypes or
                      objectClasses
        @param val the schema element definition to add"""
        self.conn.modify_s(DN_SCHEMA, [(ldap.MOD_ADD, attr, val)])

    def del_schema(self, attr, val):
        """Delete a schema element from the schema.
        @param attr - the attribute type to use e.g. attributeTypes or
                      objectClasses
        @param val - the schema element definition to delete"""
        self.conn.modify_s(DN_SCHEMA, [(ldap.MOD_DELETE, attr, val)])

    def add_attribute(self, *attributes):
        """Add an attribute type definition to the schema.
        @param attributes a single or list of attribute type defintions to add
        """
        return self.add_schema('attributeTypes', attributes)

    def add_objectclass(self, *objectclasses):
        """Add an object class definition to the schema.
        @param objectclasses a single or list of object class defintions to add
        """
        return self.add_schema('objectClasses', objectclasses)

    def get_schema_csn(self):
        """return the schema nsSchemaCSN attribute"""
        ents = self.conn.search_s(DN_SCHEMA, ldap.SCOPE_BASE,
                                  "objectclass=*", ['nsSchemaCSN'])
        ent = ents[0]
        return ent.getValue('nsSchemaCSN')

    def get_objectclasses(self):
        """Returns a list of ldap.schema.models.ObjectClass objects for all
        objectClasses supported by this instance.
        """
        attrs = ['objectClasses']
        results = self.conn.search_s(DN_SCHEMA, ldap.SCOPE_BASE,
                                     'objectclass=*', attrs)[0]
        objectclasses = [ObjectClass(oc) for oc in
                         results.getValues('objectClasses')]
        return objectclasses

    def get_attributetypes(self):
        """Returns a list of ldap.schema.models.AttributeType objects for all
        attributeTypes supported by this instance.
        """
        attrs = ['attributeTypes']
        results = self.conn.search_s(DN_SCHEMA, ldap.SCOPE_BASE,
                                     'objectclass=*', attrs)[0]
        attributetypes = [AttributeType(at) for at in
                          results.getValues('attributeTypes')]
        return attributetypes

    def get_matchingrules(self):
        """Return a list of the server defined matching rules"""
        attrs = ['matchingrules']
        results = self.conn.search_s(DN_SCHEMA, ldap.SCOPE_BASE,
                                     'objectclass=*', attrs)[0]
        matchingRules = [MatchingRule(mr) for mr in
                         results.getValues('matchingRules')]
        return matchingRules

    def query_matchingrule(self, mr_name):
        """Returns a single matching rule instance that matches the mr_name.
        Returns None if the matching rule doesn't exist.

        @param mr_name - The name of the matching rule you want to query.

        return MatchingRule or None

        <ldap.schema.models.MatchingRule instance>
        """
        matchingRules = self.get_matchingrules()
        matchingRule = [mr for mr in matchingRules
                        if mr_name.lower() in
                            list(map(str.lower, mr.names))]
        if len(matchingRule) != 1:
            # This is an error.
            return None
        matchingRule = matchingRule[0]
        return matchingRule

    def query_objectclass(self, objectclassname):
        """Returns a single ObjectClass instance that matches objectclassname.
        Returns None if the objectClass doesn't exist.

        @param objectclassname - The name of the objectClass you want to query.

        return ObjectClass or None

        ex. query_objectclass('account')
        <ldap.schema.models.ObjectClass instance>
        """
        objectclasses = self.get_objectclasses()

        objectclass = [oc for oc in objectclasses
                       if objectclassname.lower() in
                           list(map(str.lower, oc.names))]
        if len(objectclass) != 1:
            # This is an error.
            return None
        objectclass = objectclass[0]
        return objectclass

    def query_attributetype(self, attributetypename):
        """Returns a tuple of the AttributeType, and what objectclasses may or
        must take this attributeType. Returns None if attributetype doesn't
        exist.

        @param attributetypename - The name of the attributeType you want to
        query

        return (AttributeType, Must, May) or None

        ex. query_attributetype('uid')
        ( <ldap.schema.models.AttributeType instance>,
         [<ldap.schema.models.ObjectClass instance>, ...],
         [<ldap.schema.models.ObjectClass instance>, ...] )
        """
        # First, get the attribute that matches name. We need to consider
        # alternate names. There is no way to search this, so we have to
        # filter our set of all attribute types.
        objectclasses = self.get_objectclasses()
        attributetypes = self.get_attributetypes()
        attributetypename = attributetypename.lower()

        attributetype = [at for at in attributetypes
                         if attributetypename.lower() in
                             list(map(str.lower, at.names))]
        if len(attributetype) != 1:
            # This is an error.
            return None
        attributetype = attributetype[0]
        # Get the primary name of this attribute
        attributetypename = attributetype.names[0]
        # Build a set if they have may.
        may = [oc for oc in objectclasses if attributetypename.lower() in
               list(map(str.lower, oc.may))]
        # Build a set if they have must.
        must = [oc for oc in objectclasses if attributetypename.lower() in
                list(map(str.lower, oc.must))]
        return (attributetype, must, may)
