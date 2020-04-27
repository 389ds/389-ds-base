# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.idm.user import nsUserAccount, nsUserAccounts
from lib389.cli_base import populate_attr_arguments, _generic_modify
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

SINGULAR = nsUserAccount
MANY = nsUserAccounts
RDN = 'uid'

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

def status(inst, basedn, log, args):
    uid = _get_arg( args.uid, msg="Enter %s to check" % RDN)
    uas = MANY(inst, basedn)
    acct = uas.get(uid)
    acct_str = "locked: %s" % acct.is_locked()
    log.info('uid: %s' % uid)
    log.info(acct_str)

def lock(inst, basedn, log, args):
    uid = _get_arg( args.uid, msg="Enter %s to check" % RDN)
    accounts = MANY(inst, basedn)
    acct = accounts.get(uid)
    acct.lock()
    log.info('locked %s' % uid)

def unlock(inst, basedn, log, args):
    uid = _get_arg( args.uid, msg="Enter %s to check" % RDN)
    accounts = MANY(inst, basedn)
    acct = accounts.get(uid)
    acct.unlock()
    log.info('unlocked %s' % uid)

def create_parser(subparsers):
    user_parser = subparsers.add_parser('user', help='Manage posix users')

    subcommands = user_parser.add_subparsers(help='action')

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
    populate_attr_arguments(create_parser, SINGULAR._must_attributes)

    modify_parser = subcommands.add_parser('modify', help='modify <add|delete|replace>:<attribute>:<value> ...')
    modify_parser.set_defaults(func=modify)
    modify_parser.add_argument('selector', nargs=1, help='The uid to modify')
    modify_parser.add_argument('changes', nargs='+', help="A list of changes to apply in format: <add|delete|replace>:<attribute>:<value>")

    delete_parser = subcommands.add_parser('delete', help='deletes the object')
    delete_parser.set_defaults(func=delete)
    delete_parser.add_argument('dn', nargs='?', help='The dn to delete')

    lock_parser = subcommands.add_parser('lock', help='lock')
    lock_parser.set_defaults(func=lock)
    lock_parser.add_argument('uid', nargs='?', help='The uid to lock')

    status_parser = subcommands.add_parser('status', help='status')
    status_parser.set_defaults(func=status)
    status_parser.add_argument('uid', nargs='?', help='The uid to check')

    unlock_parser = subcommands.add_parser('unlock', help='unlock')
    unlock_parser.set_defaults(func=unlock)
    unlock_parser.add_argument('uid', nargs='?', help='The uid to unlock')


# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
