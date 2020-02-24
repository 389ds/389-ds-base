# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
   You will access this from:
   schema = Schema(instance)
"""
import glob
import ldap
import ldif
import re
from itertools import count
from json import dumps as dump_json
from operator import itemgetter
from ldap.schema.models import AttributeType, ObjectClass, MatchingRule
from lib389._constants import *
from lib389._constants import DN_SCHEMA
from lib389.utils import ds_is_newer
from lib389._mapped_object import DSLdapObject
from lib389.tasks import SchemaReloadTask, SyntaxValidateTask

# Count should start with 0 because of the python-ldap API
ObjectclassKind = Enum("Objectclass kind",
                       zip(["STRUCTURAL", "ABSTRACT", "AUXILIARY"], count()))
AttributeUsage = Enum("Attribute usage",
                      zip(["userApplications", "directoryOperation", "distributedOperation", "dSAOperation"], count()))

OBJECT_MODEL_PARAMS = {ObjectClass: {'names': (), 'oid': None, 'desc': None, 'x_origin': None, 'obsolete': 0,
                                     'kind': ObjectclassKind.STRUCTURAL.value, 'sup': (), 'must': (), 'may': ()},
                       AttributeType: {'names': (), 'oid': None, 'desc': None, 'sup': (), 'obsolete': 0,
                                       'equality': None, 'ordering': None, 'substr': None, 'collective': 0,
                                       'syntax': None, 'syntax_len': None, 'single_value': 0,
                                       'no_user_mod': 0, 'usage': AttributeUsage.userApplications.value,
                                       'x_origin': None, 'x_ordered': None}}
ATTR_SYNTAXES = {"1.3.6.1.4.1.1466.115.121.1.5": "Binary",
                 "1.3.6.1.4.1.1466.115.121.1.6": "Bit String",
                 "1.3.6.1.4.1.1466.115.121.1.7": "Boolean",
                 "1.3.6.1.4.1.1466.115.121.1.11": "Country String",
                 "1.3.6.1.4.1.1466.115.121.1.12": "DN",
                 "1.3.6.1.4.1.1466.115.121.1.14": "Delivery Method",
                 "1.3.6.1.4.1.1466.115.121.1.15": "Directory String",
                 "1.3.6.1.4.1.1466.115.121.1.21": "Enhanced Guide",
                 "1.3.6.1.4.1.1466.115.121.1.22": "Facsimile",
                 "1.3.6.1.4.1.1466.115.121.1.23": "Fax",
                 "1.3.6.1.4.1.1466.115.121.1.24": "Generalized Time",
                 "1.3.6.1.4.1.1466.115.121.1.25": "Guide",
                 "1.3.6.1.4.1.1466.115.121.1.26": "IA5 String",
                 "1.3.6.1.4.1.1466.115.121.1.27": "Integer",
                 "1.3.6.1.4.1.1466.115.121.1.28": "JPEG",
                 "1.3.6.1.4.1.1466.115.121.1.34": "Name and Optional UID",
                 "1.3.6.1.4.1.1466.115.121.1.36": "Numeric String",
                 "1.3.6.1.4.1.1466.115.121.1.40": "OctetString",
                 "1.3.6.1.4.1.1466.115.121.1.37": "Object Class Description",
                 "1.3.6.1.4.1.1466.115.121.1.38": "OID",
                 "1.3.6.1.4.1.1466.115.121.1.41": "Postal Address",
                 "1.3.6.1.4.1.1466.115.121.1.44": "Printable String",
                 "2.16.840.1.113730.3.7.1": "Space-Insensitive String",
                 "1.3.6.1.4.1.1466.115.121.1.50": "TelephoneNumber",
                 "1.3.6.1.4.1.1466.115.121.1.51": "Teletex Terminal Identifier",
                 "1.3.6.1.4.1.1466.115.121.1.52": "Telex Number"}


X_ORIGIN_REGEX = r'\'(.*?)\''


class Schema(DSLdapObject):
    """An object that represents the schema entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance):
        super(Schema, self).__init__(instance=instance)
        self._dn = DN_SCHEMA
        self._rdn_attribute = 'cn'

    @staticmethod
    def _get_attr_name_by_model(object_model):
        # Validate the model and return its attribute name
        if object_model in (ObjectClass, AttributeType, MatchingRule):
            return object_model.schema_attribute
        else:
            raise ValueError("Wrong object model was specified")

    @staticmethod
    def _validate_ldap_schema_value(value):
        """Validate the values that we supply to ldap.schema.models
        because it expects some exact values.
        It should be tuple, not list.
        It should be None or () if we don't want """

        if type(value) == list:
            value = tuple(value)
        elif value == "":
            value = None
        if value == ("",):
            value = ()
        return value

    @staticmethod
    def get_attr_syntaxes(json=False):
        """Get a list of available attribute syntaxes"""

        if json:
            attr_syntaxes_list = []
            for id, name in ATTR_SYNTAXES.items():
                attr_syntaxes_list.append({'label': name, 'id': id})
            result = {'type': 'list', 'items': attr_syntaxes_list}
        else:
            result = ATTR_SYNTAXES
        return result

    def _get_schema_objects(self, object_model, json=False):
        """Get all the schema objects for a specific model: Attribute, Objectclass,
        or Matchingreule.
        """
        attr_name = self._get_attr_name_by_model(object_model)
        results = self.get_attr_vals_utf8(attr_name)

        if json:
            object_insts = []
            for obj in results:
                obj_i = vars(object_model(obj))
                if len(obj_i["names"]) == 1:
                    obj_i['name'] = obj_i['names'][0].lower()
                    obj_i['aliases'] = None
                elif len(obj_i["names"]) > 1:
                    obj_i['name'] = obj_i['names'][0].lower()
                    obj_i['aliases'] = obj_i['names'][1:]
                else:
                    obj_i['name'] = ""

                # Temporary workaround for X-ORIGIN in ObjectClass objects.
                # It should be removed after https://github.com/python-ldap/python-ldap/pull/247 is merged
                if " X-ORIGIN " in obj and obj_i['names'] == vars(object_model(obj))['names']:
                    remainder = obj.split(" X-ORIGIN ")[1]
                    if remainder[:1] == "(":
                        # Have multiple values
                        end = remainder.find(')')
                        vals = remainder[1:end]
                        vals = re.findall(X_ORIGIN_REGEX, vals)
                        # For now use the first value, but this should be a set (another bug in python-ldap)
                        obj_i['x_origin'] = vals[0]
                    else:
                        # Single X-ORIGIN value
                        obj_i['x_origin'] = obj.split(" X-ORIGIN ")[1].split("'")[1]
                object_insts.append(obj_i)

            object_insts = sorted(object_insts, key=itemgetter('name'))
            # Ensure that the string values are in list so we can use React filter component with it
            for obj_i in object_insts:
                for key, value in obj_i.items():
                    if isinstance(value, str):
                        obj_i[key] = (value, )

            return {'type': 'list', 'items': object_insts}
        else:
            object_insts = [object_model(obj_i) for obj_i in results]
            return sorted(object_insts, key=lambda x: x.names, reverse=False)

    def _get_schema_object(self, name, object_model, json=False):
        objects = self._get_schema_objects(object_model, json=json)
        if json:
            schema_object = [obj_i for obj_i in objects["items"] if name.lower() in
                             list(map(str.lower, obj_i["names"]))]
        else:
            schema_object = [obj_i for obj_i in objects if name.lower() in
                             list(map(str.lower, obj_i.names))]

        if len(schema_object) != 1:
            # This is an error.
            if json:
                raise ValueError('Could not find: %s' % name)
            else:
                return None

        return schema_object[0]

    def _add_schema_object(self, parameters, object_model):
        attr_name = self._get_attr_name_by_model(object_model)

        if len(parameters) == 0:
            raise ValueError('Parameters should be specified')

        # Validate args
        if "names" not in parameters.keys():
            raise ValueError('%s name should be specified' % attr_name)
        for name in parameters["names"]:
            schema_object_old = self._get_schema_object(name, object_model)
            if schema_object_old is not None:
                raise ValueError('The %s with the name %s already exists' % (attr_name, name))

        # Default structure. We modify it later with the specified arguments
        schema_object = object_model()

        for oc_param, value in parameters.items():
            if oc_param.lower() not in OBJECT_MODEL_PARAMS[object_model].keys():
                raise ValueError('Wrong parameter name was specified: %s' % oc_param)
            if value is not None:
                value = self._validate_ldap_schema_value(value)
                setattr(schema_object, oc_param.lower(), value)

        # Set other not defined arguments so objectClass model work correctly
        # all 'None', but OBSOLETE and KIND are '0' (STRUCTURAL)
        # It is automatically assigned to 'SUP top'
        parameters_none = {k.lower(): v for k, v in parameters.items() if v is None}
        for k, v in parameters_none.items():
            setattr(schema_object, k, OBJECT_MODEL_PARAMS[object_model][k])
        return self.add(attr_name, str(schema_object))

    def _remove_schema_object(self, name, object_model):
        attr_name = self._get_attr_name_by_model(object_model)
        schema_object = self._get_schema_object(name, object_model)

        return self.remove(attr_name, str(schema_object))

    def _edit_schema_object(self, name, parameters, object_model):
        attr_name = self._get_attr_name_by_model(object_model)
        schema_object = self._get_schema_object(name, object_model)
        schema_object_str_old = str(schema_object)

        if len(parameters) == 0:
            raise ValueError('Parameters should be specified')

        for oc_param, value in parameters.items():
            if oc_param.lower() not in OBJECT_MODEL_PARAMS[object_model].keys():
                raise ValueError('Wrong parameter name was specified: %s' % oc_param)
            if value is not None:
                value = self._validate_ldap_schema_value(value)
                setattr(schema_object, oc_param, value)
            else:
                if getattr(schema_object, oc_param, False):
                    # Need to set the correct "type" for the empty value
                    if oc_param in ['may', 'must',  'x-origin', 'sup']:
                        # Expects tuple
                        setattr(schema_object, oc_param, ())
                    elif oc_param in ['desc', 'oid']:
                        # Expects None
                        setattr(schema_object, oc_param, None)
                    elif oc_param in ['obsolete', 'kind']:
                        # Expects numberic
                        setattr(schema_object, oc_param, 0)

        schema_object_str = str(schema_object)
        if schema_object_str == schema_object_str_old:
            raise ValueError('ObjectClass is already in the required state. Nothing to change')

        self.remove(attr_name, schema_object_str_old)
        return self.add(attr_name, schema_object_str)

    def reload(self, schema_dir=None):
        """Reload the schema"""

        task = SchemaReloadTask(self._instance)

        task_properties = {}
        if schema_dir is not None:
            task_properties['schemadir'] = schema_dir

        task.create(properties=task_properties)

        return task

    def list_files(self):
        """Return a list of the schema files in the instance schemadir"""

        file_list = []
        file_list += glob.glob(os.path.join(self.conn.schemadir, "*.ldif"))
        if ds_is_newer('1.3.6.0'):
            file_list += glob.glob(os.path.join(self.conn.ds_paths.system_schema_dir, "*.ldif"))
        return file_list

    def file_to_ldap(self, filename):
        """Convert the given schema file name to its python-ldap format
        suitable for passing to ldap.schema.SubSchema()

        :param filename: the full path and filename of a schema file in ldif format
        :type filename: str
        """

        with open(filename, 'r') as f:
            ldif_parser = ldif.LDIFRecordList(f, max_entries=1)
        if not ldif_parser:
            return None
        ldif_parser.parse()
        if not ldif_parser.all_records:
            return None
        return ldif_parser.all_records[0][1]

    def file_to_subschema(self, filename):
        """Convert the given schema file name to its python-ldap format
        ldap.schema.SubSchema object

        :param filename: the full path and filename of a schema file in ldif format
        :type filename: str
        """

        ent = self.file_to_ldap(filename)
        if not ent:
            return None
        return ldap.schema.SubSchema(ent)

    def get_schema_csn(self):
        """Return the schema nsSchemaCSN attribute"""

        return self.get_attr_val_utf8('nsSchemaCSN')

    def add_attributetype(self, parameters):
        """Add an attribute type definition to the schema.

        :param parameters: an attribute type definition to add
        :type parameters: str
        """

        return self._add_schema_object(parameters, AttributeType)

    def add_objectclass(self, parameters):
        """Add an object class definition to the schema.

        :param parameters: an objectClass definition to add
        :type parameters: str
        """

        return self._add_schema_object(parameters, ObjectClass)

    def remove_attributetype(self, name):
        """Remove the attribute type definition from the schema.

        :param name: the name of the attributeType you want to remove.
        :type name: str
        """

        return self._remove_schema_object(name, AttributeType)

    def remove_objectclass(self, name):
        """Remove an objectClass definition from the schema.

        :param name: the name of the objectClass you want to remove.
        :type name: str
        """

        return self._remove_schema_object(name, ObjectClass)

    def edit_attributetype(self, name, parameters):
        """Edit the attribute type definition in the schema

        :param name: the name of the attribute type you want to edit.
        :type name: str
        :param parameters: an attribute type definition to edit
        :type parameters: str
        """

        return self._edit_schema_object(name, parameters, AttributeType)

    def edit_objectclass(self, name, parameters):
        """Edit an objectClass definition in the schema.

        :param name: the name of the objectClass you want to edit.
        :type name: str
        :param parameters: an objectClass definition to edit
        :type parameters: str
        """

        return self._edit_schema_object(name, parameters, ObjectClass)

    def get_objectclasses(self, json=False):
        """Returns a list of ldap.schema.models.ObjectClass objects for all
        objectClasses supported by this instance.

        :param json: return the result in JSON format
        :type json: bool
        """

        return self._get_schema_objects(ObjectClass, json=json)

    def get_attributetypes(self, json=False):
        """Returns a list of ldap.schema.models.AttributeType objects for all
        attributeTypes supported by this instance.

        :param json: return the result in JSON format
        :type json: bool
        """

        return self._get_schema_objects(AttributeType, json=json)

    def get_matchingrules(self, json=False):
        """Return a list of the server defined matching rules

        :param json: return the result in JSON format
        :type json: bool
        """

        return self._get_schema_objects(MatchingRule, json=json)

    def query_matchingrule(self, mr_name, json=False):
        """Returns a single matching rule instance that matches the mr_name.
        Returns None if the matching rule doesn't exist.

        :param mr_name: the name of the matching rule you want to query.
        :type mr_name: str
        :param json: return the result in JSON format
        :type json: bool

        :returns: MatchingRule or None

        <ldap.schema.models.MatchingRule instance>
        """

        matching_rule = self._get_schema_object(mr_name, MatchingRule, json=json)

        if json:
            result = {'type': 'schema', 'mr': matching_rule}
            return result
        else:
            return matching_rule

    def query_objectclass(self, objectclassname, json=False):
        """Returns a single ObjectClass instance that matches objectclassname.
        Returns None if the objectClass doesn't exist.

        :param objectclassname: The name of the objectClass you want to query.
        :type objectclassname: str
        :param json: return the result in JSON format
        :type json: bool

        :returns: ObjectClass or None

        ex. query_objectclass('account')
        <ldap.schema.models.ObjectClass instance>
        """

        objectclass = self._get_schema_object(objectclassname, ObjectClass, json=json)

        if json:
            result = {'type': 'schema', 'oc': objectclass}
            return result
        else:
            return objectclass

    def query_attributetype(self, attributetypename, json=False):
        """Returns a tuple of the AttributeType, and what objectclasses may or
        must take this attributeType. Returns None if attributetype doesn't
        exist.

        :param attributetypename: The name of the attributeType you want to query
        :type attributetypename: str
        :param json: return the result in JSON format
        :type json: bool

        :returns: (AttributeType, Must, May) or None

        ex. query_attributetype('uid')
        ( <ldap.schema.models.AttributeType instance>,
         [<ldap.schema.models.ObjectClass instance>, ...],
         [<ldap.schema.models.ObjectClass instance>, ...] )
        """

        # First, get the attribute that matches name. We need to consider
        # alternate names. There is no way to search this, so we have to
        # filter our set of all attribute types.
        attributetype = self._get_schema_object(attributetypename, AttributeType, json=json)
        objectclasses = self.get_objectclasses()

        # Get the primary name of this attribute
        if json:
            attributetypenames = attributetype["names"]
        else:
            attributetypenames = attributetype.names

        # Build a set if they have may.
        may = []
        for attributetypename in attributetypenames:
            may.extend([oc for oc in objectclasses if attributetypename.lower() in
                        list(map(str.lower, oc.may))])
        # Build a set if they have must.
        must = []
        for attributetypename in attributetypenames:
            must.extend([oc for oc in objectclasses if attributetypename.lower() in
                         list(map(str.lower, oc.must))])

        if json:
            # convert Objectclass class to dict, then sort each list
            may = [vars(oc) for oc in may]
            must = [vars(oc) for oc in must]
            # Add normalized 'name' for sorting
            for oc in may:
                oc['name'] = oc['names'][0]
            for oc in must:
                oc['name'] = oc['names'][0]
            may = sorted(may, key=itemgetter('name'))
            must = sorted(must, key=itemgetter('name'))
            result = {'type': 'schema',
                      'at': attributetype,
                      'may': may,
                      'must': must}
            return result
        else:
            return str(attributetype), may, must

    def validate_syntax(self, basedn, _filter=None):
        """Create a validate syntax task

        :param basedn: Basedn to validate
        :type basedn: str
        :param _filter: a filter for entries to validate
        :type _filter: str

        :returns: an instance of Task(DSLdapObject)
        """

        task = SyntaxValidateTask(self._instance)
        task_properties = {'basedn': basedn}
        if _filter is not None:
            task_properties['filter'] = _filter
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

        with open(filename, 'r') as f:
            ldif_parser = ldif.LDIFRecordList(f, max_entries=1)
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

class Resolver(object):
    def __init__(self, schema_attrs):
        self.attr_map = {}
        for attr in schema_attrs:
            for name in attr.names:
                self.attr_map[name.lower()] = attr
        # done

    def resolve(self, attr_in):
        attr_in_l = attr_in.lower()
        if attr_in_l in self.attr_map:
            return self.attr_map[attr_in_l].names[0]
        else:
            return attr_in_l
