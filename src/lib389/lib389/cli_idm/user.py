# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json

from lib389.pwpolicy import PwPolicyManager
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

DEFAULT_USER_TYPE = "posix"
USER_TYPE_CHOICES = ['basic', 'posix', 'service', 'traditional']
SINGULAR_DICT = {
    None: nsUserAccount,
    'posix': nsUserAccount,
    'basic': BasicUserAccount,
    'service': ServiceAccount,
    'traditional': TraditionalUserAccount,
}
MANY_DICT = {
    None: nsUserAccounts,
    'posix': nsUserAccounts,
    'basic': BasicUserAccounts,
    'service': ServiceAccounts,
    'traditional': TraditionalUserAccounts,
}

RDN = 'uid'


# These are a generic specification, try not to tamper with them

def list(inst, basedn, log, args):
    _generic_list(inst, basedn, log.getChild('_generic_list'), MANY_DICT[args.user_type], args)


def get(inst, basedn, log, args):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('_generic_get'), MANY_DICT[args.user_type], rdn, args)


def get_dn(inst, basedn, log, args):
    dn = _get_arg( args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('_generic_get_dn'), MANY_DICT[args.user_type], dn, args)


def create(inst, basedn, log, args):
    user_type = getattr(args, 'user_type', None) or DEFAULT_USER_TYPE
    kwargs = _get_attributes(args, SINGULAR_DICT[user_type]._must_attributes)
    _generic_create(inst, basedn, log.getChild('_generic_create'), MANY_DICT[args.user_type], kwargs, args)


def delete(inst, basedn, log, args, warn=True):
    dn = _get_arg( args.dn, msg="Enter dn to delete")
    if warn:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR_DICT[args.user_type].__name__, dn))
    _generic_delete(inst, basedn, log.getChild('_generic_delete'), SINGULAR_DICT[args.user_type], dn, args)


def modify(inst, basedn, log, args, warn=True):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_modify(inst, basedn, log.getChild('_generic_modify'), MANY_DICT[args.user_type], rdn, args)


def rename(inst, basedn, log, args, warn=True):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_rename(inst, basedn, log.getChild('_generic_rename'), MANY_DICT[args.user_type], rdn, args)


def get_pwp(inst, basedn, log, args):
    """Show the effective password policy for a user account."""
    log = log.getChild('get_pwp')
    selector = _get_arg(args.selector, msg="Enter username to look up password policy for")
    user_type = getattr(args, 'user_type', None) or DEFAULT_USER_TYPE
    pwp_manager = PwPolicyManager(inst)
    report = pwp_manager.get_effective_policy(basedn, user_type, selector)
    if args.json:
        log.info(json.dumps(pwp_manager.format_report_json(report), indent=4))
    else:
        log.info(pwp_manager.format_report_text(report))


def create_parser(subparsers, user_type=DEFAULT_USER_TYPE):
    user_parser = subparsers.add_parser('user',
                                        help='Manage posix users.  The organizationalUnit (by default "ou=people") needs '
                                             'to exist prior to managing users.')
    subcommands = user_parser.add_subparsers(help='action')
    user_parser.add_argument('--user-type',
                             choices=USER_TYPE_CHOICES,
                             default=DEFAULT_USER_TYPE,
                             help=f'''The type of user to manage.
                                  "basic" is an entry based off of objectclasses:
                                      nsPerson, nsAccount, and nsOrgPerson.
                                  "posix" is based off of objectclasses:
                                      nsPerson, nsAccount, nsOrgPerson, and posixAccount.
                                  "service" is based off of objectclasses:
                                      nsAccount and applicationProcess.
                                  "traditional", aka legacy, is based off of objectclasses:
                                      person, organizationalPerson, and inetOrgPerson.
                                  The default user type is "{DEFAULT_USER_TYPE}"''')

    list_parser = subcommands.add_parser('list', help='list', formatter_class=CustomHelpFormatter)
    list_parser.set_defaults(func=list)
    list_parser.add_argument('--full-dn', action='store_true',
                             help="Return the full DN of the entry instead of the RDN value")

    get_parser = subcommands.add_parser('get', help='get', formatter_class=CustomHelpFormatter)
    get_parser.set_defaults(func=get)
    get_parser.add_argument('selector', nargs='?', help='The term to search for')

    get_pwp_parser = subcommands.add_parser(
        'get-pwp',
        help='Show the effective password policy for a user',
        formatter_class=CustomHelpFormatter,
        description='Resolve and display the password policy (global, user local, or subtree '
                    'local) that applies to the user, including all effective settings.',
    )
    get_pwp_parser.set_defaults(func=get_pwp)
    get_pwp_parser.add_argument(
        'selector',
        nargs='?',
        help='The user identifier to look up (same as user get, typically uid)',
    )

    get_dn_parser = subcommands.add_parser('get_dn', help='get_dn', formatter_class=CustomHelpFormatter)
    get_dn_parser.set_defaults(func=get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The dn to get')

    create_user_parser = subcommands.add_parser('create', help='create', formatter_class=CustomHelpFormatter)
    create_user_parser.set_defaults(func=create)
    populate_attr_arguments(create_user_parser, SINGULAR_DICT[user_type]._must_attributes)

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
