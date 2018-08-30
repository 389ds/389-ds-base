# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.cli_base import _get_arg
from lib389.schema import Schema


def _validate_dual_args(enable_arg, disable_arg):
    if enable_arg and disable_arg:
        raise ValueError('Only one of the flags should be specified: %s and %s' % (enable_arg, disable_arg))

    if enable_arg:
        return 1
    elif disable_arg:
        return 0


def list_attributetypes(inst, basedn, log, args):
    log = log.getChild('list_attributetypes')
    schema = Schema(inst)
    if args is not None and args.json:
        print(schema.get_attributetypes(json=True))
    else:
        for attributetype in schema.get_attributetypes():
            log.info(attributetype)


def list_objectclasses(inst, basedn, log, args):
    log = log.getChild('list_objectclasses')
    schema = Schema(inst)
    if args is not None and args.json:
        print(schema.get_objectclasses(json=True))
    else:
        for oc in schema.get_objectclasses():
            log.info(oc)


def list_matchingrules(inst, basedn, log, args):
    log = log.getChild('list_matchingrules')
    schema = Schema(inst)
    if args is not None and args.json:
        print(schema.get_matchingrules(json=True))
    else:
        for mr in schema.get_matchingrules():
            log.info(mr)


def query_attributetype(inst, basedn, log, args):
    log = log.getChild('query_attributetype')
    schema = Schema(inst)
    # Need the query type
    attr = _get_arg(args.attr, msg="Enter attribute to query")
    if args.json:
        print(schema.query_attributetype(attr, json=args.json))
    else:
        attributetype, must, may = schema.query_attributetype(attr, json=args.json)
        log.info(attributetype)
        log.info("")
        log.info("MUST")
        for oc in must:
            log.info(oc)
        log.info("")
        log.info("MAY")
        for oc in may:
            log.info(oc)


def query_objectclass(inst, basedn, log, args):
    log = log.getChild('query_objectclass')
    schema = Schema(inst)
    # Need the query type
    oc = _get_arg(args.attr, msg="Enter objectclass to query")
    result = schema.query_objectclass(oc, json=args.json)
    if args.json:
        print(result)
    else:
        log.info(result)


def query_matchingrule(inst, basedn, log, args):
    log = log.getChild('query_matchingrule')
    schema = Schema(inst)
    # Need the query type
    attr = _get_arg(args.attr, msg="Enter attribute to query")
    result = schema.query_matchingrule(attr, json=args.json)
    if args.json:
        print(result)
    else:
        log.info(result)


def add_attributetype(inst, basedn, log, args):
    log = log.getChild('add_attributetype')
    schema = Schema(inst)
    parameters = _get_parameters(args, 'attributetypes')
    schema.add_attributetype(parameters)
    log.info("Successfully added the attributeType")


def add_objectclass(inst, basedn, log, args):
    log = log.getChild('add_objectclass')
    schema = Schema(inst)

    parameters = _get_parameters(args, 'objectclasses')
    schema.add_objectclass(parameters)
    log.info("Successfully added the objectClass")


def edit_attributetype(inst, basedn, log, args):
    log = log.getChild('edit_attributetype')
    schema = Schema(inst)
    parameters = _get_parameters(args, 'attributetypes')
    schema.edit_attributetype(args.name, parameters)
    log.info("Successfully changed the attributetype")


def remove_attributetype(inst, basedn, log, args):
    log = log.getChild('remove_attributetype')
    attr = _get_arg(args.name, msg="Enter attribute to remove")
    schema = Schema(inst)
    schema.remove_attributetype(attr)
    log.info("Successfully removed the attributetype")


def edit_objectclass(inst, basedn, log, args):
    log = log.getChild('edit_objectclass')
    schema = Schema(inst)
    parameters = _get_parameters(args, 'objectclasses')
    schema.edit_objectclass(args.name, parameters)
    log.info("Successfully changed the objectClass")


def remove_objectclass(inst, basedn, log, args):
    log = log.getChild('remove_objectclass')
    attr = _get_arg(args.name, msg="Enter objectClass to remove")
    schema = Schema(inst)
    schema.remove_objectclass(attr)
    log.info("Successfully removed the objectClass")


def reload_schema(inst, basedn, log, args):
    log = log.getChild('reload_schema')
    schema = Schema(inst)
    log.info('Attempting to add task entry... This will fail if Schema Reload plug-in is not enabled.')
    task = schema.reload(args.schemadir)
    if args.wait:
        task.wait()
        rc = task.get_exit_code()
        if rc == 0:
            log.info("Schema reload task ({}) successfully finished.".format(task.dn))
        else:
            raise ValueError("Schema reload task failed, please check the errors log for more information")
    else:
        log.info('Successfully added task entry {}'.format(task.dn))
        log.info("To verify that the schema reload operation was successful, please check the error logs.")


def _get_parameters(args, type):
    if type not in ('attributetypes', 'objectclasses'):
        raise ValueError("Wrong parser type: %s" % type)

    parameters = {'names': (args.name,),
                  'oid': args.oid,
                  'desc': args.desc,
                  'obsolete': _validate_dual_args(args.obsolete, args.not_obsolete)}

    if type == 'attributetypes':
        parameters.update({'single_value': _validate_dual_args(args.single_value, args.multi_value),
                           'syntax': args.syntax,
                           'syntax_len': None,  # We need it for
                           'x_ordered': None,   # the correct ldap.schema.models work
                           'no_user_mod': _validate_dual_args(args.no_user_mod, args.with_user_mod),
                           'equality': args.equality,
                           'substr': args.substr,
                           'ordering': args.ordering,
                           'x_origin': args.x_origin,
                           'collective': _validate_dual_args(args.collective, args.not_collective),
                           'usage': args.usage,
                           'sup': args.sup})
    elif type == 'objectclasses':
        parameters.update({'must': args.must,
                           'may': args.may,
                           'kind': args.kind,
                           'sup': args.sup})

    return parameters


def _add_parser_args(parser, type):
    if type not in ('attributetypes', 'objectclasses'):
        raise ValueError("Wrong parser type: %s" % type)

    parser.add_argument('name',  help='NAME of the object')
    parser.add_argument('--oid', help='OID assigned to the object')
    parser.add_argument('--desc', help='Description text(DESC) of the object')
    parser.add_argument('--obsolete', action='store_true',
                        help='True if the object is marked as OBSOLETE in the schema.'
                             'Only one of the flags this or --not-obsolete should be specified')
    parser.add_argument('--not-obsolete', action='store_true',
                        help='True if the OBSOLETE mark should be removed'
                             'object is marked as OBSOLETE in the schema'
                             'Only one of the flags this or --obsolete should be specified')
    if type == 'attributetypes':
        parser.add_argument('--syntax', required=True,
                            help='OID of the LDAP syntax assigned to the attribute')
        parser.add_argument('--single-value', action='store_true',
                            help='True if the matching rule must have only one value'
                                 'Only one of the flags this or --multi-value should be specified')
        parser.add_argument('--multi-value', action='store_true',
                            help='True if the matching rule may have multiple values (default)'
                                 'Only one of the flags this or --single-value should be specified')
        parser.add_argument('--no-user-mod', action='store_true',
                            help='True if the attribute is not modifiable by a client application'
                                 'Only one of the flags this or --with-user-mod should be specified')
        parser.add_argument('--with-user-mod', action='store_true',
                            help='True if the attribute is modifiable by a client application (default)'
                                 'Only one of the flags this or --no-user-mode should be specified')
        parser.add_argument('--equality',
                            help='NAME or OID of the matching rule used for checking'
                                 'whether attribute values are equal')
        parser.add_argument('--substr',
                            help='NAME or OID of the matching rule used for checking'
                                 'whether an attribute value contains another value')
        parser.add_argument('--ordering',
                            help='NAME or OID of the matching rule used for checking'
                                 'whether attribute values are lesser - equal than')
        parser.add_argument('--x-origin',
                            help='Provides information about where the attribute type is defined')
        parser.add_argument('--collective',
                            help='True if the attribute is assigned their values by virtue in their membership in some collection'
                                 'Only one of the flags this or --not-collective should be specified')
        parser.add_argument('--not-collective',
                            help='True if the attribute is not assigned their values by virtue in their membership in some collection (default)'
                                 'Only one of the flags this or --collective should be specified')
        parser.add_argument('--usage',
                            help='The flag indicates how the attribute type is to be used.'
                                 'userApplications - user, directoryOperation - directory operational,'
                                 'distributedOperation - DSA-shared operational, dSAOperation - DSA - specific operational')
        parser.add_argument('--sup', nargs='?', help='The list of NAMEs or OIDs of attribute types'
                                                     'this attribute type is derived from')
    elif type == 'objectclasses':
        parser.add_argument('--must', nargs='+', help='NAMEs or OIDs of all attributes an entry of the object must have')
        parser.add_argument('--may', nargs='+', help='NAMEs or OIDs of additional attributes an entry of the object may have')
        parser.add_argument('--kind', help='Kind of an object. 0 = STRUCTURAL (default), 1 = ABSTRACT, 2 = AUXILIARY')
        parser.add_argument('--sup', nargs='+', help='NAMEs or OIDs of object classes this object is derived from')
    else:
        raise ValueError("Wrong parser type: %s" % type)


def create_parser(subparsers):
    schema_parser = subparsers.add_parser('schema', help='Query and manipulate schema')

    schema_subcommands = schema_parser.add_subparsers(help='schema')

    attributetypes_parser = schema_subcommands.add_parser('attributetypes', help='Work with attribute types on this system')
    attributetypes_subcommands = attributetypes_parser.add_subparsers(help='schema')
    at_list_parser = attributetypes_subcommands.add_parser('list', help='List available attribute types on this system')
    at_list_parser.set_defaults(func=list_attributetypes)
    at_query_parser = attributetypes_subcommands.add_parser('query', help='Query an attribute to determine object classes that may or must take it')
    at_query_parser.set_defaults(func=query_attributetype)
    at_query_parser.add_argument('attr', nargs='?', help='Attribute type to query')
    at_add_parser = attributetypes_subcommands.add_parser('add', help='Add an attribute type to this system')
    at_add_parser.set_defaults(func=add_attributetype)
    _add_parser_args(at_add_parser, 'attributetypes')
    at_edit_parser = attributetypes_subcommands.add_parser('edit', help='Edit an attribute type on this system')
    at_edit_parser.set_defaults(func=edit_attributetype)
    _add_parser_args(at_edit_parser, 'attributetypes')
    at_remove_parser = attributetypes_subcommands.add_parser('remove', help='Remove an attribute type on this system')
    at_remove_parser.set_defaults(func=remove_attributetype)
    at_remove_parser.add_argument('name',  help='NAME of the object')

    objectclasses_parser = schema_subcommands.add_parser('objectclasses', help='Work with objectClasses on this system')
    objectclasses_subcommands = objectclasses_parser.add_subparsers(help='schema')
    oc_list_parser = objectclasses_subcommands.add_parser('list', help='List available objectClasses on this system')
    oc_list_parser.set_defaults(func=list_objectclasses)
    oc_query_parser = objectclasses_subcommands.add_parser('query', help='Query an objectClass')
    oc_query_parser.set_defaults(func=query_objectclass)
    oc_query_parser.add_argument('attr', nargs='?', help='ObjectClass to query')
    oc_add_parser = objectclasses_subcommands.add_parser('add', help='Add an objectClass to this system')
    oc_add_parser.set_defaults(func=add_objectclass)
    _add_parser_args(oc_add_parser, 'objectclasses')
    oc_edit_parser = objectclasses_subcommands.add_parser('edit', help='Edit an objectClass on this system')
    oc_edit_parser.set_defaults(func=edit_objectclass)
    _add_parser_args(oc_edit_parser, 'objectclasses')
    oc_remove_parser = objectclasses_subcommands.add_parser('remove', help='Remove an objectClass on this system')
    oc_remove_parser.set_defaults(func=remove_objectclass)
    oc_remove_parser.add_argument('name',  help='NAME of the object')

    matchingrules_parser = schema_subcommands.add_parser('matchingrules', help='Work with matching rules on this system')
    matchingrules_subcommands = matchingrules_parser.add_subparsers(help='schema')
    mr_list_parser = matchingrules_subcommands.add_parser('list', help='List available matching rules on this system')
    mr_list_parser.set_defaults(func=list_matchingrules)
    mr_query_parser = matchingrules_subcommands.add_parser('query', help='Query a matching rule')
    mr_query_parser.set_defaults(func=query_matchingrule)
    mr_query_parser.add_argument('attr', nargs='?', help='Matching rule to query')

    reload_parser = schema_subcommands.add_parser('reload', help='Dynamically reload schema while server is running')
    reload_parser.set_defaults(func=reload_schema)
    reload_parser.add_argument('-d', '--schemadir', help="directory where schema files are located")
    reload_parser.add_argument('--wait', action='store_true', default=False, help="Wait for the reload task to complete")


