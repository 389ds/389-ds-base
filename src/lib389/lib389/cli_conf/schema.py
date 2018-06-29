# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.cli_base import _get_arg
from lib389.schema import Schema


def list_attributetype(inst, basedn, log, args):
    if args is not None and args.json:
        print(inst.schema.get_attributetypes(json=True))
    else:
        for attributetype in inst.schema.get_attributetypes():
            print(attributetype)


def list_objectclasses(inst, basedn, log, args):
    if args is not None and args.json:
        print(inst.schema.get_objectclasses(json=True))
    else:
        for oc in inst.schema.get_objectclasses():
            print(oc)


def list_matchingrules(inst, basedn, log, args):
    if args is not None and args.json:
        print(inst.schema.get_matchingrules(json=True))
    else:
        for mr in inst.schema.matchingrules():
            print(mr)


def query_attributetype(inst, basedn, log, args):
    # Need the query type
    attr = _get_arg(args.attr, msg="Enter attribute to query")
    if args.json:
        print(inst.schema.query_attributetype(attr, json=True))
    else:
        attributetype, must, may = inst.schema.query_attributetype(attr)
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
    # Need the query type
    oc = _get_arg(args.attr, msg="Enter objectclass to query")
    if args.json:
        print(inst.schema.query_objectclass(oc, json=True))
    else:
        print("Not done")


def query_matchingrule(inst, basedn, log, args):
    # Need the query type
    attr = _get_arg(args.attr, msg="Enter attribute to query")
    if args.json:
        print(inst.schema.query_matchingrule(attr, json=True))
    else:
        print("Not done")


def reload_schema(inst, basedn, log, args):
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
        log.info('Successfully added task entry ' + task.dn)
        log.info("To verify that the schema reload operation was successful, please check the error logs.")


def create_parser(subparsers):
    schema_parser = subparsers.add_parser('schema', help='Query and manipulate schema')

    subcommands = schema_parser.add_subparsers(help='schema')

    list_attributetype_parser = subcommands.add_parser('list_attributetype', help='List avaliable attribute types on this system')
    list_attributetype_parser.set_defaults(func=list_attributetype)

    query_attributetype_parser = subcommands.add_parser('query_attributetype', help='Query an attribute to determine object classes that may or must take it')
    query_attributetype_parser.set_defaults(func=query_attributetype)
    query_attributetype_parser.add_argument('attr', nargs='?', help='Attribute type to query')

    list_objectclass_parser = subcommands.add_parser('list_objectclasses', help='List avaliable objectclasses on this system')
    list_objectclass_parser.set_defaults(func=list_objectclasses)

    query_objectclass_parser = subcommands.add_parser('query_objectclass', help='Query an objectclass')
    query_objectclass_parser.set_defaults(func=query_objectclass)
    query_objectclass_parser.add_argument('attr', nargs='?', help='Objectclass to query')

    reload_parser = subcommands.add_parser('reload', help='Dynamically reload schema while server is running')
    reload_parser.set_defaults(func=reload_schema)
    reload_parser.add_argument('-d', '--schemadir', help="directory where schema files are located")
    reload_parser.add_argument('--wait', action='store_true', default=False, help="Wait for the reload task to complete")

    list_matchingrules_parser = subcommands.add_parser('list_matchingrules', help='List avaliable matching rules on this system')
    list_matchingrules_parser.set_defaults(func=list_matchingrules)

    query_matchingrule_parser = subcommands.add_parser('query_matchingrule', help='Query a matchingrule')
    query_matchingrule_parser.set_defaults(func=query_matchingrule)
    query_matchingrule_parser.add_argument('attr', nargs='?', help='Matchingrule to query')

