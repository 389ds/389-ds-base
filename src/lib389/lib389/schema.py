# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
   You will access this from:
   DirSrv.schema.methodName()
"""
import glob
import ldap
from json import dumps as dump_json
from operator import itemgetter
from ldap.schema.models import AttributeType, ObjectClass, MatchingRule

from lib389._constants import *
from lib389._constants import DN_SCHEMA
from lib389.utils import ds_is_newer
from lib389._mapped_object import DSLdapObject
from lib389.tasks import SchemaReloadTask


class Schema(DSLdapObject):
    def __init__(self, instance):
        super(Schema, self).__init__(instance=instance)
        self._dn = DN_SCHEMA
        self._rdn_attribute = 'cn'

    def reload(self, schema_dir=None):
        task = SchemaReloadTask(self._instance)

        task_properties = {}
        if schema_dir is not None:
            task_properties['schemadir'] = schema_dir

        task.create(properties=task_properties)

        return task


class SchemaLegacy(object):

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
        file_list = []
        file_list += glob.glob(self.conn.schemadir + "/*.ldif")
        if ds_is_newer('1.3.6.0'):
            file_list += glob.glob(self.conn.ds_paths.system_schema_dir + "/*.ldif")
        return file_list

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


    def get_objectclasses(self, json=False):
        """Returns a list of ldap.schema.models.ObjectClass objects for all
        objectClasses supported by this instance.
        """
        attrs = ['objectClasses']
        results = self.conn.search_s(DN_SCHEMA, ldap.SCOPE_BASE,
                                     'objectclass=*', attrs)[0]
        if json:
            objectclasses = [vars(ObjectClass(oc)) for oc in
                results.getValues('objectClasses')]
            for oc in objectclasses:
                # Add normalized name for sorting
                oc['name'] = oc['names'][0].lower()
            objectclasses = sorted(objectclasses, key=itemgetter('name'))
            result = {'type': 'list', 'items': objectclasses}
            return dump_json(result)
        else:
            objectclasses = [ObjectClass(oc) for oc in
                             results.getValues('objectClasses')]
            return objectclasses

    def get_attributetypes(self, json=False):
        """Returns a list of ldap.schema.models.AttributeType objects for all
        attributeTypes supported by this instance.
        """
        attrs = ['attributeTypes']
        results = self.conn.search_s(DN_SCHEMA, ldap.SCOPE_BASE,
                                     'objectclass=*', attrs)[0]

        if json:
            attributetypes = [vars(AttributeType(at)) for at in
                results.getValues('attributeTypes')]
            for attr in attributetypes:
                # Add normalized name for sorting
                attr['name'] = attr['names'][0].lower()
            attributetypes = sorted(attributetypes, key=itemgetter('name'))
            result = {'type': 'list', 'items': attributetypes}
            return dump_json(result)
        else:
            attributetypes = [AttributeType(at) for at in
                results.getValues('attributeTypes')]
            return attributetypes


    def get_matchingrules(self, json=False):
        """Return a list of the server defined matching rules"""
        attrs = ['matchingrules']
        results = self.conn.search_s(DN_SCHEMA, ldap.SCOPE_BASE,
                                     'objectclass=*', attrs)[0]
        if json:
            matchingRules = [vars(MatchingRule(mr)) for mr in
                results.getValues('matchingRules')]
            for mr in matchingRules:
                # Add normalized name for sorting
                if mr['names']:
                    mr['name'] = mr['names'][0].lower()
                else:
                    mr['name'] = ""
            matchingRules = sorted(matchingRules, key=itemgetter('name'))
            result = {'type': 'list', 'items': matchingRules}
            return dump_json(result)
        else:
            matchingRules = [MatchingRule(mr) for mr in
                         results.getValues('matchingRules')]
            return matchingRules


    def query_matchingrule(self, mr_name, json=False):
        """Returns a single matching rule instance that matches the mr_name.
        Returns None if the matching rule doesn't exist.

        @param mr_name - The name of the matching rule you want to query.

        return MatchingRule or None

        <ldap.schema.models.MatchingRule instance>
        """
        matchingRules = self.get_matchingrules()
        matchingRule = [mr for mr in matchingRules if mr_name.lower() in
                        list(map(str.lower, mr.names))]
        if len(matchingRule) != 1:
            # This is an error.
            if json:
                raise ValueError('Could not find matchingrule: ' + objectclassname)
            else:
                return None
        matchingRule = matchingRule[0]
        if json:
            result = {'type': 'schema', 'mr': vars(matchingRule)}
            return dump_json(result)
        else:
            return matchingRule

    def query_objectclass(self, objectclassname, json=False):
        """Returns a single ObjectClass instance that matches objectclassname.
        Returns None if the objectClass doesn't exist.

        @param objectclassname - The name of the objectClass you want to query.

        return ObjectClass or None

        ex. query_objectclass('account')
        <ldap.schema.models.ObjectClass instance>
        """
        objectclasses = self.get_objectclasses()

        objectclass = [oc for oc in objectclasses if objectclassname.lower() in
                       list(map(str.lower, oc.names))]
        if len(objectclass) != 1:
            # This is an error.
            if json:
                raise ValueError('Could not find objectcass: ' + objectclassname)
            else:
                return None
        objectclass = objectclass[0]
        if json:
            result = {'type': 'schema', 'oc': vars(objectclass)}
            return dump_json(result)
        else:
            return objectclass

    def query_attributetype(self, attributetypename, json=False):
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
            if json:
                raise ValueError('Could not find attribute: ' + attributetypename)
            else:
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

        if json:
            # convert Objectclass class to dict, then sort each list
            may = [vars(oc) for oc in may]
            must = [vars(oc) for oc in must]
            # Add normalized 'name' for sorting
            for oc in may:
                oc['name'] = oc['names'][0].lower()
            for oc in must:
                oc['name'] = oc['names'][0].lower()
            may = sorted(may, key=itemgetter('name'))
            must = sorted(must, key=itemgetter('name'))
            result = {'type': 'schema',
                      'at': vars(attributetype),
                      'may': may,
                      'must': must}
            return dump_json(result)
        else:
           return (attributetype, must, may)

