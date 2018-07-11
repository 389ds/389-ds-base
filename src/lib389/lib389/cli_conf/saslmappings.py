# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.saslmap import SaslMapping, SaslMappings
from lib389.utils import ensure_str
from lib389.cli_base import (
    populate_attr_arguments,
    _generic_list,
    _generic_get,
    _generic_create,
    _generic_delete,
    _get_arg,
    _get_attributes,
    _warn,
    )

SINGULAR = SaslMapping
MANY = SaslMappings
RDN = 'cn'


def sasl_map_list(inst, basedn, log, args):
    _generic_list(inst, basedn, log.getChild('sasl_map_list'), MANY, args)


def sasl_map_get(inst, basedn, log, args):
    rdn = _get_arg(args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('sasl_map_get'), MANY, rdn, args)


def sasl_map_create(inst, basedn, log, args):
    kwargs = _get_attributes(args, SaslMapping._must_attributes)
    if kwargs['nssaslmappriority'] == '':
        kwargs['nssaslmappriority'] = '100'  # Default
    _generic_create(inst, basedn, log.getChild('sasl_map_create'), MANY, kwargs, args)


def sasl_map_delete(inst, basedn, log, args, warn=True):
    found = False
    mappings = SaslMappings(inst).list()
    map_name = args.map_name.lower()

    for saslmap in mappings:
        cn = ensure_str(saslmap.get_attr_val('cn')).lower()
        if cn == map_name:
            dn = saslmap.dn
            found = True
            break
    if not found:
        raise ValueError("Unable to find a SASL mapping with the name: ({})".format(args.map_name))

    if warn and args.json is False:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))
    _generic_delete(inst, basedn, log.getChild('sasl_map_delete'), SINGULAR, dn, args)


def create_parser(subparsers):
    sasl_parser = subparsers.add_parser('sasl', help='Query and manipulate sasl mappings')

    subcommands = sasl_parser.add_subparsers(help='sasl')

    list_mappings_parser = subcommands.add_parser('list', help='List avaliable SASL mappings')
    list_mappings_parser.set_defaults(func=sasl_map_list)

    get_parser = subcommands.add_parser('get', help='get')
    get_parser.set_defaults(func=sasl_map_get)
    get_parser.add_argument('selector', nargs='?', help='SASL mapping name to get')

    create_parser = subcommands.add_parser('create', help='create')
    create_parser.set_defaults(func=sasl_map_create)
    populate_attr_arguments(create_parser, SaslMapping._must_attributes)

    delete_parser = subcommands.add_parser('delete', help='deletes the object')
    delete_parser.set_defaults(func=sasl_map_delete)
    delete_parser.add_argument('map_name', help='The SASL Mapping name ("cn" value)')

