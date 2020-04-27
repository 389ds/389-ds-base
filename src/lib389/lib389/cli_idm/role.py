# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019, Red Hat inc,
# Copyright (C) 2018, William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from lib389.idm.role import Role, Roles
from lib389.cli_base import (
    _generic_get_dn,
    _generic_list,
    _generic_delete,
    _generic_modify_dn,
    _get_dn_arg,
    _warn,
    )

MANY = Roles
SINGULAR = Role


def list(inst, basedn, log, args):
    _generic_list(inst, basedn, log.getChild('_generic_list'), MANY, args)


def get_dn(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('_generic_get_dn'), MANY, dn, args)


def delete(inst, basedn, log, args, warn=True):
    dn = _get_dn_arg(args.dn, msg="Enter dn to delete")
    if warn:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))
    _generic_delete(inst, basedn, log.getChild('_generic_delete'), SINGULAR, dn, args)


def modify(inst, basedn, log, args, warn=True):
    dn = _get_dn_arg(args.dn, msg="Enter dn to modify")
    _generic_modify_dn(inst, basedn, log.getChild('_generic_modify'), MANY, dn, args)


def entry_status(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to check")
    roles = Roles(inst, basedn)
    role = roles.get(dn=dn)
    status = role.status()
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
    role_parser = subparsers.add_parser('role', help='''Manage generic roles, with tasks
like modify, locking and unlocking.''')

    subcommands = role_parser.add_subparsers(help='action')

    list_parser = subcommands.add_parser('list', help='list roles that could login to the directory')
    list_parser.set_defaults(func=list)

    get_dn_parser = subcommands.add_parser('get-by-dn', help='get-by-dn <dn>')
    get_dn_parser.set_defaults(func=get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The dn to get and display')

    modify_dn_parser = subcommands.add_parser('modify-by-dn', help='modify-by-dn <dn> <add|delete|replace>:<attribute>:<value> ...')
    modify_dn_parser.set_defaults(func=modify)
    modify_dn_parser.add_argument('dn', nargs=1, help='The dn to get and display')
    modify_dn_parser.add_argument('changes', nargs='+', help="A list of changes to apply in format: <add|delete|replace>:<attribute>:<value>")

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
