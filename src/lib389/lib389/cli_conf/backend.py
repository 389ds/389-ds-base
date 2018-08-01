# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.backend import Backend, Backends
from lib389.utils import ensure_str
from lib389.cli_base import (
    populate_attr_arguments,
    _generic_list,
    _generic_get,
    _generic_get_dn,
    _generic_create,
    _generic_delete,
    _get_arg,
    _get_args,
    _get_attributes,
    _warn,
    )
import json

SINGULAR = Backend
MANY = Backends
RDN = 'cn'


def backend_list(inst, basedn, log, args):
    if 'suffix' in args:
        result = {"type": "list", "items": []}
        be_insts = Backends(inst).list()
        for be in be_insts:
            if args.json:
                result['items'].append(ensure_str(be.get_attr_val_utf8_l('nsslapd-suffix')).lower())
            else:
                print(ensure_str(be.get_attr_val_utf8_l('nsslapd-suffix')).lower())
        if args.json:
            print(json.dumps(result))

    else:
        _generic_list(inst, basedn, log.getChild('backend_list'), MANY, args)


def backend_get(inst, basedn, log, args):
    rdn = _get_arg(args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('backend_get'), MANY, rdn, args)


def backend_get_dn(inst, basedn, log, args):
    dn = _get_arg(args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('backend_get_dn'), MANY, dn, args)


def backend_create(inst, basedn, log, args):
    kwargs = _get_attributes(args, Backend._must_attributes)
    _generic_create(inst, basedn, log.getChild('backend_create'), MANY, kwargs, args)


def backend_delete(inst, basedn, log, args, warn=True):
    found = False
    be_insts = Backends(inst).list()
    for be in be_insts:
        cn = ensure_str(be.get_attr_val('cn')).lower()
        suffix = ensure_str(be.get_attr_val('nsslapd-suffix')).lower()
        del_be_name = args.be_name.lower()
        if cn == del_be_name or suffix == del_be_name:
            dn = be.dn
            found = True
            break
    if not found:
        raise ValueError("Unable to find a backend with the name: ({})".format(args.be_name))

    if warn and args.json is False:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))
    _generic_delete(inst, basedn, log.getChild('backend_delete'), SINGULAR, dn, args)


def create_parser(subparsers):
    backend_parser = subparsers.add_parser('backend', help="Manage database suffixes and backends")

    subcommands = backend_parser.add_subparsers(help="action")

    list_parser = subcommands.add_parser('list', help="List current active backends and suffixes")
    list_parser.set_defaults(func=backend_list)
    list_parser.add_argument('--suffix', action='store_true', help='Display the suffix for each backend')

    get_parser = subcommands.add_parser('get', help='get')
    get_parser.set_defaults(func=backend_get)
    get_parser.add_argument('selector', nargs='?', help='The backend to search for')

    get_dn_parser = subcommands.add_parser('get_dn', help='get_dn')
    get_dn_parser.set_defaults(func=backend_get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The backend dn to get')

    create_parser = subcommands.add_parser('create', help='create')
    create_parser.set_defaults(func=backend_create)
    populate_attr_arguments(create_parser, Backend._must_attributes)

    delete_parser = subcommands.add_parser('delete', help='deletes the object')
    delete_parser.set_defaults(func=backend_delete)
    delete_parser.add_argument('be_name', help='The backend name or suffix to delete')


