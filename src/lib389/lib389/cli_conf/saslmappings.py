# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
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
    CustomHelpFormatter
    )

SINGULAR = SaslMapping
MANY = SaslMappings
RDN = 'cn'


def sasl_map_list(inst, basedn, log, args):
    if args.details:
        # List SASL mappings with details
        mappings = SaslMappings(inst).list()
        result = {"type": "list", "items": []}
        for sasl_map in mappings:
            if args.json:
                entry = sasl_map.get_all_attrs_json()
                # Append decoded json object, because we are going to dump it later
                result['items'].append(json.loads(entry))
            else:
                log.info(sasl_map.display())
        if args.json:
            log.info(json.dumps(result, indent=4))
    else:
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

def sasl_get_supported(inst, basedn, log, args):
    """Get a list of the supported sasl mechanisms"""
    mechs = inst.rootdse.supported_sasl()
    if args.json:
        result = {'type': 'list', 'items': mechs}
        log.info(json.dumps(result, indent=4, ))
    else:
        for mech in mechs:
            log.info(mech)


def sasl_get_available(inst, basedn, log, args):
    """Get a list of the available sasl mechanisms"""
    mechs = inst.rootdse.available_sasl()
    if args.json:
        result = {'type': 'list', 'items': mechs}
        log.info(json.dumps(result, indent=4, ))
    else:
        for mech in mechs:
            log.info(mech)


def create_parser(subparsers):
    sasl_parser = subparsers.add_parser('sasl', help='Manage SASL mappings', formatter_class=CustomHelpFormatter)

    subcommands = sasl_parser.add_subparsers(help='sasl')

    list_mappings_parser = subcommands.add_parser('list', help='Display available SASL mappings', formatter_class=CustomHelpFormatter)
    list_mappings_parser.set_defaults(func=sasl_map_list)
    list_mappings_parser.add_argument('--details', action='store_true', default=False,
        help="Displays each SASL mapping in detail")

    get_mech_parser= subcommands.add_parser('get-mechs', help='Display the SASL mechanisms that the server will accept', formatter_class=CustomHelpFormatter)
    get_mech_parser.set_defaults(func=sasl_get_supported)

    get_mech_parser= subcommands.add_parser('get-available-mechs', help='Display the SASL mechanisms that are available to the server', formatter_class=CustomHelpFormatter)
    get_mech_parser.set_defaults(func=sasl_get_available)

    get_parser = subcommands.add_parser('get', help='Displays SASL mappings', formatter_class=CustomHelpFormatter)
    get_parser.set_defaults(func=sasl_map_get)
    get_parser.add_argument('selector', nargs='?', help='The SASL mapping name to display')

    create_parser = subcommands.add_parser('create', help='Create a SASL mapping ', formatter_class=CustomHelpFormatter)
    create_parser.set_defaults(func=sasl_map_create)
    populate_attr_arguments(create_parser, SaslMapping._must_attributes)

    delete_parser = subcommands.add_parser('delete', help='Deletes the SASL object', formatter_class=CustomHelpFormatter)
    delete_parser.set_defaults(func=sasl_map_delete)
    delete_parser.add_argument('map_name', help='The SASL mapping name ("cn" value)')

