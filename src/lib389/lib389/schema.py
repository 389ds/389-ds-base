"""
   You will access this from:
   DirSrv.schema.methodName()
"""
import ldap
import os
import re
import time
import glob


from lib389._constants import *
from lib389 import Entry
from lib389.utils import normalizeDN, escapeDNValue, suffixfilt

class Schema(object):
    
    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log
    
    def get_entry(self):
        """get the schema as an LDAP entry"""
        attrs = ['attributeTypes', 'objectClasses']
        return self.conn.search_s(DN_SCHEMA, ldap.SCOPE_BASE, 'objectclass=*', attrs)[0]

    def get_subschema(self):
        """get the schema as a python-ldap SubSchema object"""
        return ldap.schema.SubSchema(self.get_entry().data)

    def list_files(self):
        """return a list of the schema files in the instance schemadir"""
        return glob.glob(self.conn.schemadir + "/*.ldif")

    def file_to_ldap(self, filename):
        """convert the given schema file name to its python-ldap format
        suitable for passing to ldap.schema.SubSchema()
        @param filename - the full path and filename of a schema file in ldif format"""
        import urllib,ldif
        ldif_file = urllib.urlopen(filename)
        ldif_parser = ldif.LDIFRecordList(ldif_file,max_entries=1)
        if not ldif_parser:
            return None
        ldif_parser.parse()
        if not ldif_parser.all_records:
            return None
        return ldif_parser.all_records[0][1]

    def file_to_subschema(self, filename):
        """convert the given schema file name to its python-ldap format
        ldap.schema.SubSchema object
        @param filename - the full path and filename of a schema file in ldif format"""
        ent = self.file_to_ldap(filename)
        if not ent:
            return None
        return ldap.schema.SubSchema(ent)

    def add_schema(self, attr, val):
        """Add a schema element to the schema.
        @param attr the attribute type to use e.g. attributeTypes or objectClasses
        @param val the schema element definition to add"""
        self.conn.modify_s(DN_SCHEMA, [(ldap.MOD_ADD, attr, val)])
        
    def del_schema(self, attr, val):
        """Delete a schema element from the schema.
        @param attr the attribute type to use e.g. attributeTypes or objectClasses
        @param val the schema element definition to delete"""
        self.conn.modify_s(DN_SCHEMA, [(ldap.MOD_DELETE, attr, val)])

    def add_attribute(self, *attributes):
        """Add an attribute type definition to the schema.
        @param attributes a single or list of attribute type defintions to add"""
        return self.add_schema('attributeTypes', attributes)

    def add_objectclass(self, *objectclasses):
        """Add an object class definition to the schema.
        @param objectclasses a single or list of object class defintions to add"""
        return self.add_schema('objectClasses', objectclasses)
    
    def get_schema_csn(self):
        """return the schema nsSchemaCSN attribute"""
        ents = self.conn.search_s(DN_SCHEMA, ldap.SCOPE_BASE, "objectclass=*", ['nsSchemaCSN'])
        ent = ents[0]
        return ent.getValue('nsSchemaCSN')
