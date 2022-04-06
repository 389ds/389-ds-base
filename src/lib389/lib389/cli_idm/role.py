# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019, Red Hat inc,
# Copyright (C) 2018, William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.idm.role import (
    Role,
    Roles,
    ManagedRoles,
    FilteredRoles,
    NestedRoles,
    MUST_ATTRIBUTES,
    MUST_ATTRIBUTES_NESTED
    )
from lib389.cli_base import (
    populate_attr_arguments,
    _get_arg,
    _get_attributes,
    _generic_get,
    _generic_get_dn,
    _generic_list,
    _generic_delete,
    _generic_modify_dn,
    _generic_create,
    _get_dn_arg,
    _warn,
    )
from lib389.cli_idm import _generic_rename_dn

MANY = Roles
SINGULAR = Role


def list(inst, basedn, log, args):
    _generic_list(inst, basedn, log.getChild('_generic_list'), MANY, args)


def get(inst, basedn, log, args):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('_generic_get'), MANY, rdn, args)


def get_dn(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('_generic_get_dn'), MANY, dn, args)


def create_managed(inst, basedn, log, args):
    kwargs = _get_attributes(args, MUST_ATTRIBUTES)
    _generic_create(inst, basedn, log.getChild('_generic_create'), ManagedRoles, kwargs, args)


def create_filtered(inst, basedn, log, args):
    kwargs = _get_attributes(args, MUST_ATTRIBUTES)
    _generic_create(inst, basedn, log.getChild('_generic_create'), FilteredRoles, kwargs, args)


def create_nested(inst, basedn, log, args):
    kwargs = _get_attributes(args, MUST_ATTRIBUTES_NESTED)
    _generic_create(inst, basedn, log.getChild('_generic_create'), NestedRoles, kwargs, args)


def delete(inst, basedn, log, args, warn=True):
    dn = _get_dn_arg(args.dn, msg="Enter dn to delete")
    if warn:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))
    _generic_delete(inst, basedn, log.getChild('_generic_delete'), SINGULAR, dn, args)


def modify(inst, basedn, log, args, warn=True):
    dn = _get_dn_arg(args.dn, msg="Enter dn to modify")
    _generic_modify_dn(inst, basedn, log.getChild('_generic_modify_dn'), MANY, dn, args)


def rename(inst, basedn, log, args, warn=True):
    dn = _get_dn_arg(args.dn, msg="Enter dn to modify")
    _generic_rename_dn(inst, basedn, log.getChild('_generic_rename_dn'), MANY, dn, args)


def entry_status(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to check")
    roles = Roles(inst, basedn)
    try:
        role = roles.get(dn=dn)
    except ldap.NO_SUCH_OBJECT:
        raise ValueError("Role \"{}\" is not found or the entry is not a role.".format(dn))

    status = role.status()
    info_dict = {}
    if args.json:
        info_dict["dn"] = dn
        info_dict["state"] = f'{status["state"].describe(status["role_dn"])}'
        log.info(json.dumps({"type": "status", "info": info_dict}, indent=4))
    else:
        log.info(f'Entry DN: {dn}')
        log.info(f'Entry State: {status["state"].describe(status["role_dn"])}\n')


def subtree_status(inst, basedn, log, args):
    basedn = _get_dn_arg(args.basedn, msg="Enter basedn to check")
    filter = ""
    scope = ldap.SCOPE_SUBTREE

    role_list = Roles(inst, basedn).filter(filter, scope)
    if not role_list:
        raise ValueError(f"No entries were found under {basedn} or the user doesn't have an access")

    for entry in role_list:
        status = entry.status()
        log.info(f'Entry DN: {entry.dn}')
        log.info(f'Entry State: {status["state"].describe(status["role_dn"])}\n')


def lock(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to check")
    role = Role(inst, dn=dn)
    role.lock()
    log.info(f'Entry {dn} is locked')


def unlock(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to check")
    role = Role(inst, dn=dn)
    role.unlock()
    log.info(f'Entry {dn} is unlocked')


def create_parser(subparsers):
    role_parser = subparsers.add_parser('role', help='''Manage roles.''')

    subcommands = role_parser.add_subparsers(help='action')

    list_parser = subcommands.add_parser('list', help='list roles that could login to the directory')
    list_parser.set_defaults(func=list)

    get_parser = subcommands.add_parser('get', help='get')
    get_parser.set_defaults(func=get)
    get_parser.add_argument('selector', nargs='?', help='The term to search for')

    get_dn_parser = subcommands.add_parser('get-by-dn', help='get-by-dn <dn>')
    get_dn_parser.set_defaults(func=get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The dn to get and display')

    create_managed_parser = subcommands.add_parser('create-managed', help='create')
    create_managed_parser.set_defaults(func=create_managed)
    populate_attr_arguments(create_managed_parser, MUST_ATTRIBUTES)

    create_filtered_parser = subcommands.add_parser('create-filtered', help='create')
    create_filtered_parser.set_defaults(func=create_filtered)
    populate_attr_arguments(create_filtered_parser, MUST_ATTRIBUTES)

    create_nested_parser = subcommands.add_parser('create-nested', help='create')
    create_nested_parser.set_defaults(func=create_nested)
    populate_attr_arguments(create_nested_parser, MUST_ATTRIBUTES_NESTED)

    modify_dn_parser = subcommands.add_parser('modify-by-dn', help='modify-by-dn <dn> <add|delete|replace>:<attribute>:<value> ...')
    modify_dn_parser.set_defaults(func=modify)
    modify_dn_parser.add_argument('dn', nargs=1, help='The dn to modify')
    modify_dn_parser.add_argument('changes', nargs='+', help="A list of changes to apply in format: <add|delete|replace>:<attribute>:<value>")

    rename_dn_parser = subcommands.add_parser('rename-by-dn', help='rename the object')
    rename_dn_parser.set_defaults(func=rename)
    rename_dn_parser.add_argument('dn', help='The dn to rename')
    rename_dn_parser.add_argument('new_dn', help='A new account dn')
    rename_dn_parser.add_argument('--keep-old-rdn', action='store_true', help="Specify whether the old RDN (i.e. 'cn: old_account') should be kept as an attribute of the entry or not")

    delete_parser = subcommands.add_parser('delete', help='deletes the role')
    delete_parser.set_defaults(func=delete)
    delete_parser.add_argument('dn', nargs='?', help='The dn of the role to delete')

    lock_parser = subcommands.add_parser('lock', help='lock')
    lock_parser.set_defaults(func=lock)
    lock_parser.add_argument('dn', nargs='?', help='The dn to lock')

    unlock_parser = subcommands.add_parser('unlock', help='unlock')
    unlock_parser.set_defaults(func=unlock)
    unlock_parser.add_argument('dn', nargs='?', help='The dn to unlock')

    status_parser = subcommands.add_parser('entry-status', help='status of a single entry')
    status_parser.set_defaults(func=entry_status)
    status_parser.add_argument('dn', nargs='?', help='The single entry dn to check')

    status_parser = subcommands.add_parser('subtree-status', help='status of a subtree')
    status_parser.set_defaults(func=subtree_status)
    status_parser.add_argument('basedn', help="Search base for finding entries")
    status_parser.add_argument('-f', '--filter', help="Search filter for finding entries")
    status_parser.add_argument('-s', '--scope', choices=['base', 'one', 'sub'], help="Search scope (base, one, sub - default is sub")
