# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.idm.services import ServiceAccount, ServiceAccounts
from lib389.cli_base import populate_attr_arguments, _generic_modify, CustomHelpFormatter
from lib389.cli_idm import (
    _generic_list,
    _generic_get,
    _generic_get_dn,
    _generic_create,
    _generic_rename,
    _generic_delete,
    _get_arg,
    _get_attributes,
    _warn,
    )

SINGULAR = ServiceAccount
MANY = ServiceAccounts
RDN = 'cn'

# These are a generic specification, try not to tamper with them

def list(inst, basedn, log, args):
    _generic_list(inst, basedn, log.getChild('_generic_list'), MANY, args)

def get(inst, basedn, log, args):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('_generic_get'), MANY, rdn, args)

def get_dn(inst, basedn, log, args):
    dn = _get_arg( args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('_generic_get_dn'), MANY, dn, args)

def create(inst, basedn, log, args):
    kwargs = _get_attributes(args, SINGULAR._must_attributes)
    _generic_create(inst, basedn, log.getChild('_generic_create'), MANY, kwargs, args)

def delete(inst, basedn, log, args, warn=True):
    dn = _get_arg( args.dn, msg="Enter dn to delete")
    if warn:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))
    _generic_delete(inst, basedn, log.getChild('_generic_delete'), SINGULAR, dn, args)

def modify(inst, basedn, log, args, warn=True):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_modify(inst, basedn, log.getChild('_generic_modify'), MANY, rdn, args)

def rename(inst, basedn, log, args, warn=True):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_rename(inst, basedn, log.getChild('_generic_rename'), MANY, rdn, args)

def create_parser(subparsers):
    service_parser = subparsers.add_parser('service',
                                           help='Manage service accounts. The organizationalUnit (by default "ou=Services") '
                                                'needs to exist prior to managing service accounts.', formatter_class=CustomHelpFormatter)

    subcommands = service_parser.add_subparsers(help='action')

    list_parser = subcommands.add_parser('list', help='list', formatter_class=CustomHelpFormatter)
    list_parser.set_defaults(func=list)
    list_parser.add_argument('--full-dn', action='store_true',
                             help="Return the full DN of the entry instead of the RDN value")

    get_parser = subcommands.add_parser('get', help='get', formatter_class=CustomHelpFormatter)
    get_parser.set_defaults(func=get)
    get_parser.add_argument('selector', nargs='?', help='The term to search for')

    get_dn_parser = subcommands.add_parser('get_dn', help='get_dn', formatter_class=CustomHelpFormatter)
    get_dn_parser.set_defaults(func=get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The dn to get')

    create_parser = subcommands.add_parser('create', help='create', formatter_class=CustomHelpFormatter)
    create_parser.set_defaults(func=create)
    populate_attr_arguments(create_parser, SINGULAR._must_attributes)

    modify_parser = subcommands.add_parser('modify', help='modify <add|delete|replace>:<attribute>:<value> ...', formatter_class=CustomHelpFormatter)
    modify_parser.set_defaults(func=modify)
    modify_parser.add_argument('selector', nargs=1, help='The %s to modify' % RDN)
    modify_parser.add_argument('changes', nargs='+', help="A list of changes to apply in format: <add|delete|replace>:<attribute>:<value>")

    rename_parser = subcommands.add_parser('rename', help='rename the object', formatter_class=CustomHelpFormatter)
    rename_parser.set_defaults(func=rename)
    rename_parser.add_argument('selector', help='The %s to modify' % RDN)
    rename_parser.add_argument('new_name', help='A new service name')
    rename_parser.add_argument('--keep-old-rdn', action='store_true', help="Specify whether the old RDN (i.e. 'cn: old_service') should be kept as an attribute of the entry or not")

    delete_parser = subcommands.add_parser('delete', help='deletes the object', formatter_class=CustomHelpFormatter)
    delete_parser.set_defaults(func=delete)
    delete_parser.add_argument('dn', nargs='?', help='The dn to delete')

# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
