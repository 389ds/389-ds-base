# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017, Red Hat inc,
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import argparse

from lib389.idm.account import Account, Accounts
from lib389.cli_base import (
    _generic_list,
    _get_arg,
    )

MANY = Accounts

def list(inst, basedn, log, args):
    _generic_list(inst, basedn, log.getChild('_generic_list'), MANY)

def status(inst, basedn, log, args):
    dn = _get_arg( args.dn, msg="Enter dn to check")
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    acct_str = "locked: %s" % acct.is_locked()
    log.info('dn: %s' % dn)
    log.info(acct_str)

def lock(inst, basedn, log, args):
    dn = _get_arg( args.dn, msg="Enter dn to check")
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    acct.lock()
    log.info('locked %s' % dn)

def unlock(inst, basedn, log, args):
    dn = _get_arg( args.dn, msg="Enter dn to check")
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    acct.unlock()
    log.info('unlocked %s' % dn)


def create_parser(subparsers):
    account_parser = subparsers.add_parser('account', help='Manage generic accounts IE account locking and unlocking.')

    subcommands = account_parser.add_subparsers(help='action')

    list_parser = subcommands.add_parser('list', help='list')
    list_parser.set_defaults(func=list)

    lock_parser = subcommands.add_parser('lock', help='lock')
    lock_parser.set_defaults(func=lock)
    lock_parser.add_argument('dn', nargs='?', help='The dn to lock')

    status_parser = subcommands.add_parser('status', help='status')
    status_parser.set_defaults(func=status)
    status_parser.add_argument('dn', nargs='?', help='The dn to check')

    unlock_parser = subcommands.add_parser('unlock', help='unlock')
    unlock_parser.set_defaults(func=unlock)
    unlock_parser.add_argument('dn', nargs='?', help='The dn to unlock')

