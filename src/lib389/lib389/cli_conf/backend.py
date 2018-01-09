# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.backend import Backend, Backends
import argparse

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

SINGULAR = Backend
MANY = Backends
RDN = 'cn'

def backend_list(inst, basedn, log, args):
    _generic_list(inst, basedn, log.getChild('backend_list'), MANY)

def backend_get(inst, basedn, log, args):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('backend_get'), MANY, rdn)

def backend_get_dn(inst, basedn, log, args):
    dn = _get_arg( args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('backend_get_dn'), MANY, dn)

def backend_create(inst, basedn, log, args):
    kwargs = _get_attributes(args, Backend._must_attributes)
    _generic_create(inst, basedn, log.getChild('backend_create'), MANY, kwargs)

def backend_delete(inst, basedn, log, args, warn=True):
    dn = _get_arg( args.dn, msg="Enter dn to delete")
    if warn:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))
    _generic_delete(inst, basedn, log.getChild('backend_delete'), SINGULAR, dn)


def create_parser(subparsers):
    backend_parser = subparsers.add_parser('backend', help="Manage database suffixes and backends")

    subcommands = backend_parser.add_subparsers(help="action")

    list_parser = subcommands.add_parser('list', help="List current active backends and suffixes")
    list_parser.set_defaults(func=backend_list)

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
    delete_parser.add_argument('dn', nargs='?', help='The dn to delete')


