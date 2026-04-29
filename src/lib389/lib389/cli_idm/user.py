# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.idm.user import (
    nsUserAccount,
    nsUserAccounts,
    BasicUserAccount,
    BasicUserAccounts,
    TraditionalUserAccount,
    TraditionalUserAccounts,
)
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

SINGULAR = nsUserAccount
MANY = nsUserAccounts

BASIC_SINGULAR = BasicUserAccount
BASIC_MANY = BasicUserAccounts
TRADITIONAL_SINGULAR = TraditionalUserAccount
TRADITIONAL_MANY = TraditionalUserAccounts
SERVICE_SINGULAR = ServiceAccount
SERVICE_MANY = ServiceAccounts

RDN = 'uid'


# These are a generic specification, try not to tamper with them

def list(inst, basedn, log, args):
    if args.user_type == 'basic':
        _generic_list(inst, basedn, log.getChild('_generic_list'), BASIC_MANY, args)
    elif args.user_type == 'traditional':
        _generic_list(inst, basedn, log.getChild('_generic_list'), TRADITIONAL_MANY, args)
    elif args.user_type == 'service':
        _generic_list(inst, basedn, log.getChild('_generic_list'), SERVICE_MANY, args)
    else:
        _generic_list(inst, basedn, log.getChild('_generic_list'), MANY, args)


def get(inst, basedn, log, args):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    if args.user_type == 'basic':
        _generic_get(inst, basedn, log.getChild('_generic_get'), BASIC_MANY, rdn, args)
    elif args.user_type == 'traditional':
        _generic_get(inst, basedn, log.getChild('_generic_get'), TRADITIONAL_MANY, rdn, args)
    elif args.user_type == 'service':
        _generic_get(inst, basedn, log.getChild('_generic_get'), SERVICE_MANY, rdn, args)
    else:
        _generic_get(inst, basedn, log.getChild('_generic_get'), MANY, rdn, args)


def get_dn(inst, basedn, log, args):
    dn = _get_arg( args.dn, msg="Enter dn to retrieve")
    if args.user_type == 'basic':
        _generic_get_dn(inst, basedn, log.getChild('_generic_get_dn'), BASIC_MANY, dn, args)
    elif args.user_type == 'traditional':
        _generic_get_dn(inst, basedn, log.getChild('_generic_get_dn'), TRADITIONAL_MANY, dn, args)
    elif args.user_type == 'service':
        _generic_get_dn(inst, basedn, log.getChild('_generic_get_dn'), SERVICE_MANY, dn, args)
    else:
        _generic_get_dn(inst, basedn, log.getChild('_generic_get_dn'), MANY, dn, args)


def create(inst, basedn, log, args):
    if args.user_type == 'basic':
        kwargs = _get_attributes(args, BASIC_SINGULAR._must_attributes)
        _generic_create(inst, basedn, log.getChild('_generic_create'), BASIC_MANY, kwargs, args)
    elif args.user_type == 'traditional':
        kwargs = _get_attributes(args, TRADITIONAL_SINGULAR._must_attributes)
        _generic_create(inst, basedn, log.getChild('_generic_create'), TRADITIONAL_MANY, kwargs, args)
    elif args.user_type == 'service':
        kwargs = _get_attributes(args, SERVICE_SINGULAR._must_attributes)
        _generic_create(inst, basedn, log.getChild('_generic_create'), SERVICE_MANY, kwargs, args)
    else:
        kwargs = _get_attributes(args, SINGULAR._must_attributes)
        _generic_create(inst, basedn, log.getChild('_generic_create'), MANY, kwargs, args)


def delete(inst, basedn, log, args, warn=True):
    dn = _get_arg( args.dn, msg="Enter dn to delete")
    if warn:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))
    if args.user_type == 'basic':
        _generic_delete(inst, basedn, log.getChild('_generic_delete'), BASIC_SINGULAR, dn, args)
    elif args.user_type == 'traditional':
        _generic_delete(inst, basedn, log.getChild('_generic_delete'), TRADITIONAL_SINGULAR, dn, args)
    elif args.user_type == 'service':
        _generic_delete(inst, basedn, log.getChild('_generic_delete'), SERVICE_SINGULAR, dn, args)
    else:
        _generic_delete(inst, basedn, log.getChild('_generic_delete'), SINGULAR, dn, args)


def modify(inst, basedn, log, args, warn=True):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    if args.user_type == 'basic':
        _generic_modify(inst, basedn, log.getChild('_generic_modify'), BASIC_MANY, rdn, args)
    elif args.user_type == 'traditional':
        _generic_modify(inst, basedn, log.getChild('_generic_modify'), TRADITIONAL_MANY, rdn, args)
    elif args.user_type == 'service':
        _generic_modify(inst, basedn, log.getChild('_generic_modify'), SERVICE_MANY, rdn, args)
    else:
        _generic_modify(inst, basedn, log.getChild('_generic_modify'), MANY, rdn, args)


def rename(inst, basedn, log, args, warn=True):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    if args.user_type == 'basic':
        _generic_rename(inst, basedn, log.getChild('_generic_rename'), BASIC_MANY, rdn, args)
    elif args.user_type == 'traditional':
        _generic_rename(inst, basedn, log.getChild('_generic_rename'), TRADITIONAL_MANY, rdn, args)
    elif args.user_type == 'service':
        _generic_rename(inst, basedn, log.getChild('_generic_rename'), SERVICE_MANY, rdn, args)
    else:
        _generic_rename(inst, basedn, log.getChild('_generic_rename'), MANY, rdn, args)


def create_parser(subparsers):
    user_parser = subparsers.add_parser('user',
                                        help='Manage posix users.  The organizationalUnit (by default "ou=people") needs '
                                             'to exist prior to managing users.')

    subcommands = user_parser.add_subparsers(help='action')
    user_parser.add_argument('--user-type',
                             choices=[
                                'basic',
                                'posix',
                                'service',
                                'traditional'
                            ],
                             default='posix',
                             help='''The type of user to manage.
                                  "basic" is an entry based off of objectclasses:
                                      nsPerson, nsAccount, and nsOrgPerson.
                                  "posix" is based off of objectclasses:
                                      nsPerson, nsAccount, nsOrgPerson, and posixAccount.
                                  "service" is based off of objectclasses:
                                      nsAccount and applicationProcess.
                                  "traditional", aka legacy, is based off of objectclasses:
                                      person, organizationalPerson, and inetOrgPerson.
                                  The default user type is "posix"''')

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

    create_user_parser = subcommands.add_parser('create', help='create', formatter_class=CustomHelpFormatter)
    create_user_parser.set_defaults(func=create)
    populate_attr_arguments(create_user_parser, SINGULAR._must_attributes)

    modify_parser = subcommands.add_parser('modify', help='modify <add|delete|replace>:<attribute>:<value> ...', formatter_class=CustomHelpFormatter)
    modify_parser.set_defaults(func=modify)
    modify_parser.add_argument('selector', nargs=1, help='The %s to modify' % RDN)
    modify_parser.add_argument('changes', nargs='+', help="A list of changes to apply in format: <add|delete|replace>:<attribute>:<value>")

    rename_parser = subcommands.add_parser('rename', help='rename the object', formatter_class=CustomHelpFormatter)
    rename_parser.set_defaults(func=rename)
    rename_parser.add_argument('selector', help='The %s to modify' % RDN)
    rename_parser.add_argument('new_name', help='A new user name')
    rename_parser.add_argument('--keep-old-rdn', action='store_true', help="Specify whether the old RDN (i.e. 'cn: old_user') should be kept as an attribute of the entry or not")

    delete_parser = subcommands.add_parser('delete', help='deletes the object', formatter_class=CustomHelpFormatter)
    delete_parser.set_defaults(func=delete)
    delete_parser.add_argument('dn', nargs='?', help='The dn to delete')

# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
