# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017, Red Hat inc,
# Copyright (C) 2018, William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import math
from datetime import datetime
from lib389.idm.account import Account, Accounts, AccountState
from lib389.cli_base import (
    _generic_get_dn,
    _generic_list,
    _generic_delete,
    _generic_modify_dn,
    _get_arg,
    _get_dn_arg,
    _warn,
    )
from lib389.cli_idm import _generic_rename_dn

MANY = Accounts
SINGULAR = Account


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
    _generic_modify_dn(inst, basedn, log.getChild('_generic_modify_dn'), MANY, dn, args)


def rename(inst, basedn, log, args, warn=True):
    dn = _get_dn_arg(args.dn, msg="Enter dn to modify")
    _generic_rename_dn(inst, basedn, log.getChild('_generic_rename_dn'), MANY, dn, args)


def _print_entry_status(status, dn, log):
    log.info(f'Entry DN: {dn}')
    for name, value in status["params"].items():
        if "Time" in name and value is not None:
            inactivation_date = datetime.fromtimestamp(status["calc_time"] + value)
            log.info(f"Entry {name}: {int(math.fabs(value))} seconds ({inactivation_date.strftime('%Y-%m-%d %H:%M:%S')})")
        elif "Date" in name and value is not None:
            log.info(f"Entry {name}: {value.strftime('%Y%m%d%H%M%SZ')} ({value.strftime('%Y-%m-%d %H:%M:%S')})")
    log.info(f'Entry State: {status["state"].describe(status["role_dn"])}\n')


def entry_status(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to check")
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    status = acct.status()
    _print_entry_status(status, dn, log)


def subtree_status(inst, basedn, log, args):
    basedn = _get_dn_arg(args.basedn, msg="Enter basedn to check")
    filter = ""
    scope = ldap.SCOPE_SUBTREE
    epoch_inactive_time = None
    if args.scope == "one":
        scope = ldap.SCOPE_ONELEVEL
    if args.filter:
        filter = args.filter
    if args.become_inactive_on:
        datetime_inactive_time = datetime.strptime(args.become_inactive_on, '%Y-%m-%dT%H:%M:%S')
        epoch_inactive_time = datetime.timestamp(datetime_inactive_time)

    account_list = Accounts(inst, basedn).filter(filter, scope)
    if not account_list:
        raise ValueError(f"No entries were found under {basedn}")

    for entry in account_list:
        status = entry.status()
        state = status["state"]
        params = status["params"]
        if args.inactive_only and state == AccountState.ACTIVATED:
            continue
        if args.become_inactive_on:
            if epoch_inactive_time is None or params["Time Until Inactive"] is None or \
               epoch_inactive_time <= (params["Time Until Inactive"] + status["calc_time"]):
                continue
        _print_entry_status(status, entry.dn, log)


def lock(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to lock")
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    acct.lock()
    log.info(f'Entry {dn} is locked')


def unlock(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to unlock")
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    acct.unlock()
    log.info(f'Entry {dn} is unlocked')


def reset_password(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to reset password")
    new_password = _get_arg(args.new_password, hidden=True, confirm=True, msg="Enter new password for %s" % dn)
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    acct.reset_password(new_password)
    log.info('reset password for %s' % dn)


def change_password(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to change password")
    cur_password = _get_arg(args.current_password, hidden=True, confirm=False, msg="Enter current password for %s" % dn)
    new_password = _get_arg(args.new_password, hidden=True, confirm=True, msg="Enter new password for %s" % dn)
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    acct.change_password(cur_password, new_password)
    log.info('changed password for %s' % dn)


def create_parser(subparsers):
    account_parser = subparsers.add_parser('account', help='''Manage generic accounts, with tasks
like modify, locking and unlocking. To create an account, see "user" subcommand instead.''')

    subcommands = account_parser.add_subparsers(help='action')

    list_parser = subcommands.add_parser('list', help='list accounts that could login to the directory')
    list_parser.set_defaults(func=list)

    get_dn_parser = subcommands.add_parser('get-by-dn', help='get-by-dn <dn>')
    get_dn_parser.set_defaults(func=get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The dn to get and display')

    modify_dn_parser = subcommands.add_parser('modify-by-dn', help='modify-by-dn <dn> <add|delete|replace>:<attribute>:<value> ...')
    modify_dn_parser.set_defaults(func=modify)
    modify_dn_parser.add_argument('dn', nargs=1, help='The dn to get and display')
    modify_dn_parser.add_argument('changes', nargs='+', help="A list of changes to apply in format: <add|delete|replace>:<attribute>:<value>")

    rename_dn_parser = subcommands.add_parser('rename-by-dn', help='rename the object')
    rename_dn_parser.set_defaults(func=rename)
    rename_dn_parser.add_argument('dn', help='The dn to rename')
    rename_dn_parser.add_argument('new_dn', help='A new role dn')
    rename_dn_parser.add_argument('--keep-old-rdn', action='store_true', help="Specify whether the old RDN (i.e. 'cn: old_role') should be kept as an attribute of the entry or not")

    delete_parser = subcommands.add_parser('delete', help='deletes the account')
    delete_parser.set_defaults(func=delete)
    delete_parser.add_argument('dn', nargs='?', help='The dn of the account to delete')

    lock_parser = subcommands.add_parser('lock', help='lock')
    lock_parser.set_defaults(func=lock)
    lock_parser.add_argument('dn', nargs='?', help='The dn to lock')

    unlock_parser = subcommands.add_parser('unlock', help='unlock')
    unlock_parser.set_defaults(func=unlock)
    unlock_parser.add_argument('dn', nargs='?', help='The dn to unlock')

    status_parser = subcommands.add_parser('entry-status', help='status of a single entry')
    status_parser.set_defaults(func=entry_status)
    status_parser.add_argument('dn', nargs='?', help='The single entry dn to check')
    status_parser.add_argument('-V', '--details', action='store_true', help="Print more account policy details about the entry")

    status_parser = subcommands.add_parser('subtree-status', help='status of a subtree')
    status_parser.set_defaults(func=subtree_status)
    status_parser.add_argument('basedn', help="Search base for finding entries")
    status_parser.add_argument('-V', '--details', action='store_true', help="Print more account policy details about the entries")
    status_parser.add_argument('-f', '--filter', help="Search filter for finding entries")
    status_parser.add_argument('-s', '--scope', choices=['one', 'sub'], help="Search scope (one, sub - default is sub")
    status_parser.add_argument('-i', '--inactive-only', action='store_true', help="Only display inactivated entries")
    status_parser.add_argument('-o', '--become-inactive-on',
                               help="Only display entries that will become inactive before specified date (in a format 2007-04-25T14:30)")

    reset_pw_parser = subcommands.add_parser('reset_password', help='Reset the password of an account. This should be performed by a directory admin.')
    reset_pw_parser.set_defaults(func=reset_password)
    reset_pw_parser.add_argument('dn', nargs='?', help='The dn to reset the password for')
    reset_pw_parser.add_argument('new_password', nargs='?', help='The new password to set')

    change_pw_parser = subcommands.add_parser('change_password', help='Change the password of an account. This can be performed by any user (with correct rights)')
    change_pw_parser.set_defaults(func=change_password)
    change_pw_parser.add_argument('dn', nargs='?', help='The dn to change the password for')
    change_pw_parser.add_argument('new_password', nargs='?', help='The new password to set')
    change_pw_parser.add_argument('current_password', nargs='?', help='The accounts current password')
