# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.idm.posixgroup import PosixGroup, PosixGroups, MUST_ATTRIBUTES
from lib389.cli_base import populate_attr_arguments
from lib389.cli_idm import (
    _generic_list,
    _generic_get,
    _generic_get_dn,
    _generic_create,
    _generic_delete,
    _get_arg,
    _get_attributes,
    _warn,
    )

SINGULAR = PosixGroup
MANY = PosixGroups
RDN = 'cn'

# These are a generic specification, try not to tamper with them

def list(inst, basedn, log, args):
    _generic_list(inst, basedn, log.getChild('_generic_list'), MANY, args)

def get(inst, basedn, log, args):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('_generic_get'), MANY, rdn, args)

def get_dn(inst, basedn, log, args):
    dn = lambda args: _get_arg( args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('_generic_get_dn'), MANY, dn, args)

def create(inst, basedn, log, args):
    kwargs = _get_attributes(args, MUST_ATTRIBUTES)
    _generic_create(inst, basedn, log.getChild('_generic_create'), MANY, kwargs, args)

def delete(inst, basedn, log, args):
    dn = _get_arg( args, msg="Enter dn to delete")
    _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))
    _generic_delete(inst, basedn, log.getChild('_generic_delete'), SINGULAR, dn, args)

def create_parser(subparsers):
    posixgroup_parser = subparsers.add_parser('posixgroup', help='Manage posix groups')

    subcommands = posixgroup_parser.add_subparsers(help='action')

    list_parser = subcommands.add_parser('list', help='list')
    list_parser.set_defaults(func=list)

    get_parser = subcommands.add_parser('get', help='get')
    get_parser.set_defaults(func=get)
    get_parser.add_argument('selector', nargs='?', help='The term to search for')

    get_dn_parser = subcommands.add_parser('get_dn', help='get_dn')
    get_dn_parser.set_defaults(func=get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The dn to get')

    create_parser = subcommands.add_parser('create', help='create')
    create_parser.set_defaults(func=create)
    populate_attr_arguments(create_parser, MUST_ATTRIBUTES)

    delete_parser = subcommands.add_parser('delete', help='deletes the object')
    delete_parser.set_defaults(func=delete)
    delete_parser.add_argument('dn', nargs='?', help='The dn to delete')


# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
