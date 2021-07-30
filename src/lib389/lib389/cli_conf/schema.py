# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from json import dumps as dump_json
from lib389.cli_base import _get_arg
from lib389.schema import Schema, AttributeUsage, ObjectclassKind
from lib389.migrate.openldap.config import olSchema
from lib389.migrate.plan import Migration


def _validate_dual_args(enable_arg, disable_arg):
    if enable_arg and disable_arg:
        raise ValueError('Only one of the flags should be specified: %s and %s' % (enable_arg, disable_arg))

    if enable_arg:
        return 1
    elif disable_arg:
        return 0


def list_all(inst, basedn, log, args):
    log = log.getChild('list_all')
    schema = Schema(inst)
    json = False
    if args is not None and args.json:
        json = True

    objectclass_elems = schema.get_objectclasses(json=json)
    attributetype_elems = schema.get_attributetypes(json=json)
    matchingrule_elems = schema.get_matchingrules(json=json)

    if json:
        print(dump_json({'type': 'schema',
                         'objectclasses': objectclass_elems,
                         'attributetypes': attributetype_elems,
                         'matchingrules': matchingrule_elems}, indent=4))
    else:
        separator_line = "".join(["-" for _ in range(50)])
        print("Objectclasses:\n", separator_line)
        for oc in objectclass_elems:
            print(oc)
        print("AttributeTypes:\n", separator_line)
        for at in attributetype_elems:
            print(at)
        print("MathingRules:\n", separator_line)
        for mr in matchingrule_elems:
            print(mr)


def list_attributetypes(inst, basedn, log, args):
    log = log.getChild('list_attributetypes')
    schema = Schema(inst)
    if args is not None and args.json:
        print(dump_json(schema.get_attributetypes(json=True), indent=4))
    else:
        for attributetype in schema.get_attributetypes():
            print(attributetype)


def list_objectclasses(inst, basedn, log, args):
    log = log.getChild('list_objectclasses')
    schema = Schema(inst)
    if args is not None and args.json:
        print(dump_json(schema.get_objectclasses(json=True), indent=4))
    else:
        for oc in schema.get_objectclasses():
            print(oc)


def list_matchingrules(inst, basedn, log, args):
    log = log.getChild('list_matchingrules')
    schema = Schema(inst)
    if args is not None and args.json:
        print(dump_json(schema.get_matchingrules(json=True), indent=4))
    else:
        for mr in schema.get_matchingrules():
            print(mr)


def query_attributetype(inst, basedn, log, args):
    log = log.getChild('query_attributetype')
    schema = Schema(inst)
    # Need the query type
    attr = _get_arg(args.name, msg="Enter attribute to query")
    if args.json:
        print(dump_json(schema.query_attributetype(attr, json=args.json), indent=4))
    else:
        attributetype, must, may = schema.query_attributetype(attr, json=args.json)
        print(attributetype)
        print("")
        print("MUST")
        for oc in must:
            print(oc)
        print("")
        print("MAY")
        for oc in may:
            print(oc)


def query_objectclass(inst, basedn, log, args):
    log = log.getChild('query_objectclass')
    schema = Schema(inst)
    # Need the query type
    oc = _get_arg(args.name, msg="Enter objectclass to query")
    result = schema.query_objectclass(oc, json=args.json)
    if args.json:
        print(dump_json(result, indent=4))
    else:
        print(result)


def query_matchingrule(inst, basedn, log, args):
    log = log.getChild('query_matchingrule')
    schema = Schema(inst)
    # Need the query type
    attr = _get_arg(args.name, msg="Enter attribute to query")
    result = schema.query_matchingrule(attr, json=args.json)
    if args.json:
        print(dump_json(result, indent=4))
    else:
        print(result)


def add_attributetype(inst, basedn, log, args):
    log = log.getChild('add_attributetype')
    schema = Schema(inst)
    parameters = _get_parameters(args, 'attributetypes')
    aliases = parameters.pop("aliases", None)
    if aliases is not None and aliases != [""]:
        parameters["names"].extend(aliases)

    schema.add_attributetype(parameters)
    print("Successfully added the attributeType")


def add_objectclass(inst, basedn, log, args):
    log = log.getChild('add_objectclass')
    schema = Schema(inst)

    parameters = _get_parameters(args, 'objectclasses')
    schema.add_objectclass(parameters)
    print("Successfully added the objectClass")


def edit_attributetype(inst, basedn, log, args):
    log = log.getChild('edit_attributetype')
    schema = Schema(inst)
    parameters = _get_parameters(args, 'attributetypes')
    aliases = parameters.pop("aliases", None)
    if aliases is not None and aliases != [""]:
        parameters["names"].extend(aliases)

    schema.edit_attributetype(args.name, parameters)
    print("Successfully changed the attributetype")


def remove_attributetype(inst, basedn, log, args):
    log = log.getChild('remove_attributetype')
    attr = _get_arg(args.name, msg="Enter attribute to remove")
    schema = Schema(inst)
    schema.remove_attributetype(attr)
    print("Successfully removed the attributetype")


def edit_objectclass(inst, basedn, log, args):
    log = log.getChild('edit_objectclass')
    schema = Schema(inst)
    parameters = _get_parameters(args, 'objectclasses')
    schema.edit_objectclass(args.name, parameters)
    print("Successfully changed the objectClass")


def remove_objectclass(inst, basedn, log, args):
    log = log.getChild('remove_objectclass')
    attr = _get_arg(args.name, msg="Enter objectClass to remove")
    schema = Schema(inst)
    schema.remove_objectclass(attr)
    print("Successfully removed the objectClass")


def reload_schema(inst, basedn, log, args):
    log = log.getChild('reload_schema')
    schema = Schema(inst)
    print('Attempting to add task entry... This will fail if Schema Reload plug-in is not enabled.')
    task = schema.reload(args.schemadir)
    if args.wait:
        task.wait()
        rc = task.get_exit_code()
        if rc == 0:
            print("Schema reload task ({}) successfully finished.".format(task.dn))
        else:
            raise ValueError("Schema reload task failed, please check the errors log for more information")
    else:
        print('Successfully added task entry {}'.format(task.dn))
        print("To verify that the schema reload operation was successful, please check the error logs.")


def validate_syntax(inst, basedn, log, args):
    schema = Schema(inst)
    log.info('Attempting to add task entry...')
    validate_task = schema.validate_syntax(args.DN, args.filter)
    validate_task.wait()
    exitcode = validate_task.get_exit_code()
    if exitcode != 0:
        log.error(f'Validate syntax task for {args.DN} has failed. Please, check logs')
    else:
        log.info('Successfully added task entry')


def get_syntaxes(inst, basedn, log, args):
    log = log.getChild('get_syntaxes')
    schema = Schema(inst)
    result = schema.get_attr_syntaxes(json=args.json)
    if args.json:
        print(dump_json(result, indent=4))
    else:
        for id, name in result.items():
            print("%s (%s)", name, id)


def import_openldap_schema_file(inst, basedn, log, args):
    log = log.getChild('import_openldap_schema_file')
    log.debug(f"Parsing {args.schema_file} ...")
    olschema = olSchema([args.schema_file], log)
    migration = Migration(inst, olschema)
    if args.confirm:
        migration.execute_plan(log)
        log.info("🎉 Schema migration complete!")
    else:
        migration.display_plan_review(log)
        log.info("No actions taken. To apply migration plan, use '--confirm'")


def _get_parameters(args, type):
    if type not in ('attributetypes', 'objectclasses'):
        raise ValueError("Wrong parser type: %s" % type)

    parameters = {'names': [args.name],
                  'oid': args.oid,
                  'desc': args.desc,
                  'x_origin': args.x_origin,
                  'obsolete': None}

    if type == 'attributetypes':
        if args.usage is not None:
            usage = args.usage
            if usage in [item.name for item in AttributeUsage]:
                usage = AttributeUsage[usage].value
            else:
                raise ValueError("Attribute usage should be one of the next: "
                                 "userApplications, directoryOperation, distributedOperation, dSAOperation")
        else:
            usage = None

        parameters.update({'single_value': _validate_dual_args(args.single_value, args.multi_value),
                           'aliases': args.aliases,
                           'syntax': args.syntax,
                           'syntax_len': None,  # We need it for
                           'x_ordered': None,   # the correct ldap.schema.models work
                           'collective': None,
                           'no_user_mod': _validate_dual_args(args.no_user_mod, args.user_mod),
                           'equality': args.equality,
                           'substr': args.substr,
                           'ordering': args.ordering,
                           'usage': usage,
                           'sup': args.sup})
    elif type == 'objectclasses':
        if args.kind is not None:
            kind = args.kind.upper()
            if kind in [item.name for item in ObjectclassKind]:
                kind = ObjectclassKind[kind].value
            else:
                raise ValueError("ObjectClass kind should be one of the next: STRUCTURAL, ABSTRACT, AUXILIARY")
        else:
            kind = None

        parameters.update({'must': args.must,
                           'may': args.may,
                           'kind': kind,
                           'sup': args.sup})

    return parameters


def _add_parser_args(parser, type):
    if type not in ('attributetypes', 'objectclasses'):
        raise ValueError("Wrong parser type: %s" % type)

    parser.add_argument('name', help='NAME of the object')
    parser.add_argument('--oid', help='OID assigned to the object')
    parser.add_argument('--desc', help='Description text(DESC) of the object')
    parser.add_argument('--x-origin',
                        help='Provides information about where the attribute type is defined')
    if type == 'attributetypes':
        parser.add_argument('--aliases', nargs='+', help='Additional NAMEs of the object.')
        parser.add_argument('--single-value', action='store_true',
                            help='True if the matching rule must have only one value'
                                 'Only one of the flags this or --multi-value should be specified')
        parser.add_argument('--multi-value', action='store_true',
                            help='True if the matching rule may have multiple values (default)'
                                 'Only one of the flags this or --single-value should be specified')
        parser.add_argument('--no-user-mod', action='store_true',
                            help='True if the attribute is not modifiable by a client application'
                                 'Only one of the flags this or --user-mod should be specified')
        parser.add_argument('--user-mod', action='store_true',
                            help='True if the attribute is modifiable by a client application (default)'
                                 'Only one of the flags this or --no-user-mode should be specified')
        parser.add_argument('--equality', nargs='+',
                            help='NAME or OID of the matching rules used for checking'
                                 'whether attribute values are equal')
        parser.add_argument('--substr', nargs='+',
                            help='NAME or OID of the matching rules used for checking'
                                 'whether an attribute value contains another value')
        parser.add_argument('--ordering', nargs='+',
                            help='NAME or OID of the matching rules used for checking'
                                 'whether attribute values are lesser - equal than')
        parser.add_argument('--usage',
                            help='The flag indicates how the attribute type is to be used. Choose from the list: '
                                 'userApplications (default), directoryOperation, distributedOperation, dSAOperation')
        parser.add_argument('--sup', nargs=1, help='The NAME or OID of attribute type this attribute type is derived from')
    elif type == 'objectclasses':
        parser.add_argument('--must', nargs='+', help='NAMEs or OIDs of all attributes an entry of the object must have')
        parser.add_argument('--may', nargs='+', help='NAMEs or OIDs of additional attributes an entry of the object may have')
        parser.add_argument('--kind', help='Kind of an object. STRUCTURAL (default), ABSTRACT, AUXILIARY')
        parser.add_argument('--sup', nargs='+', help='NAME or OIDs of object classes this object is derived from')
    else:
        raise ValueError("Wrong parser type: %s" % type)


def create_parser(subparsers):
    schema_parser = subparsers.add_parser('schema', help='Query and manipulate schema')

    schema_subcommands = schema_parser.add_subparsers(help='schema')
    schema_list_parser = schema_subcommands.add_parser('list', help='List all schema objects on this system')
    schema_list_parser.set_defaults(func=list_all)

    attributetypes_parser = schema_subcommands.add_parser('attributetypes', help='Work with attribute types on this system')
    attributetypes_subcommands = attributetypes_parser.add_subparsers(help='schema')
    at_get_syntaxes_parser = attributetypes_subcommands.add_parser('get_syntaxes', help='List all available attribute type syntaxes')
    at_get_syntaxes_parser.set_defaults(func=get_syntaxes)
    at_list_parser = attributetypes_subcommands.add_parser('list', help='List available attribute types on this system')
    at_list_parser.set_defaults(func=list_attributetypes)
    at_query_parser = attributetypes_subcommands.add_parser('query', help='Query an attribute to determine object classes that may or must take it')
    at_query_parser.set_defaults(func=query_attributetype)
    at_query_parser.add_argument('name', nargs='?', help='Attribute type to query')
    at_add_parser = attributetypes_subcommands.add_parser('add', help='Add an attribute type to this system')
    at_add_parser.set_defaults(func=add_attributetype)
    _add_parser_args(at_add_parser, 'attributetypes')
    at_add_parser.add_argument('--syntax', required=True, help='OID of the LDAP syntax assigned to the attribute')
    at_edit_parser = attributetypes_subcommands.add_parser('replace', help='Replace an attribute type on this system')
    at_edit_parser.set_defaults(func=edit_attributetype)
    _add_parser_args(at_edit_parser, 'attributetypes')
    at_edit_parser.add_argument('--syntax', help='OID of the LDAP syntax assigned to the attribute')
    at_remove_parser = attributetypes_subcommands.add_parser('remove', help='Remove an attribute type on this system')
    at_remove_parser.set_defaults(func=remove_attributetype)
    at_remove_parser.add_argument('name', help='NAME of the object')

    objectclasses_parser = schema_subcommands.add_parser('objectclasses', help='Work with objectClasses on this system')
    objectclasses_subcommands = objectclasses_parser.add_subparsers(help='schema')
    oc_list_parser = objectclasses_subcommands.add_parser('list', help='List available objectClasses on this system')
    oc_list_parser.set_defaults(func=list_objectclasses)
    oc_query_parser = objectclasses_subcommands.add_parser('query', help='Query an objectClass')
    oc_query_parser.set_defaults(func=query_objectclass)
    oc_query_parser.add_argument('name', nargs='?', help='ObjectClass to query')
    oc_add_parser = objectclasses_subcommands.add_parser('add', help='Add an objectClass to this system')
    oc_add_parser.set_defaults(func=add_objectclass)
    _add_parser_args(oc_add_parser, 'objectclasses')
    oc_edit_parser = objectclasses_subcommands.add_parser('replace', help='Replace an objectClass on this system')
    oc_edit_parser.set_defaults(func=edit_objectclass)
    _add_parser_args(oc_edit_parser, 'objectclasses')
    oc_remove_parser = objectclasses_subcommands.add_parser('remove', help='Remove an objectClass on this system')
    oc_remove_parser.set_defaults(func=remove_objectclass)
    oc_remove_parser.add_argument('name', help='NAME of the object')

    matchingrules_parser = schema_subcommands.add_parser('matchingrules', help='Work with matching rules on this system')
    matchingrules_subcommands = matchingrules_parser.add_subparsers(help='schema')
    mr_list_parser = matchingrules_subcommands.add_parser('list', help='List available matching rules on this system')
    mr_list_parser.set_defaults(func=list_matchingrules)
    mr_query_parser = matchingrules_subcommands.add_parser('query', help='Query a matching rule')
    mr_query_parser.set_defaults(func=query_matchingrule)
    mr_query_parser.add_argument('name', nargs='?', help='Matching rule to query')

    reload_parser = schema_subcommands.add_parser('reload', help='Dynamically reload schema while server is running')
    reload_parser.set_defaults(func=reload_schema)
    reload_parser.add_argument('-d', '--schemadir', help="directory where schema files are located")
    reload_parser.add_argument('--wait', action='store_true', default=False, help="Wait for the reload task to complete")

    validate_parser = schema_subcommands.add_parser('validate-syntax',
                                                    help='Run a task to check every modification to attributes to make sure '
                                                         'that the new value has the required syntax for that attribute type')
    validate_parser.set_defaults(func=validate_syntax)
    validate_parser.add_argument('DN', help="Base DN that contains entries to validate")
    validate_parser.add_argument('-f', '--filter', help='Filter for entries to validate.\n'
                                                        'If omitted, all entries with filter "(objectclass=*)" are validated')

    import_oldap_schema_parser = schema_subcommands.add_parser('import-openldap-file',
                                                    help='Import an openldap formatted dynamic schema ldifs. These will contain values like olcAttributeTypes and olcObjectClasses.')
    import_oldap_schema_parser.set_defaults(func=import_openldap_schema_file)
    import_oldap_schema_parser.add_argument('schema_file', help="Path to the openldap dynamic schema ldif to import")
    import_oldap_schema_parser.add_argument('--confirm',
                                            default=False, action='store_true',
                                            help="Confirm that you want to apply these schema migration actions to the 389-ds instance. By default no actions are taken.")
