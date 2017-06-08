# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016-2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap

from lib389.plugins import MemberOfPlugin
from lib389.cli_conf.plugin import generic_enable, generic_disable, generic_show


def manage_attr(inst, basedn, log, args):
    if args.value is not None:
        set_attr(inst, basedn, log, args)
    else:
        display_attr(inst, basedn, log, args)

def display_attr(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    log.info(plugin.get_attr_formatted())

def set_attr(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    try:
        plugin.set_attr(args.value)
    except ldap.UNWILLING_TO_PERFORM:
        log.error('Error: Illegal value "{}". Failed to set.'.format(args.value))
    else:
        log.info('memberOfAttr set to "{}"'.format(args.value))

def display_groupattr(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    log.info(plugin.get_groupattr_formatted())

def add_groupattr(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    try:
        plugin.add_groupattr(args.value)
    except ldap.UNWILLING_TO_PERFORM:
        log.error('Error: Illegal value "{}". Failed to add.'.format(args.value))
    except ldap.TYPE_OR_VALUE_EXISTS:
        log.info('Value "{}" already exists.'.format(args.value))
    else:
        log.info('successfully added memberOfGroupAttr value "{}"'.format(args.value))

def remove_groupattr(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    try:
        plugin.remove_groupattr(args.value)
    except ldap.UNWILLING_TO_PERFORM:
        log.error("Error: Failed to delete. memberOfGroupAttr is required.")
    except ldap.NO_SUCH_ATTRIBUTE as ex:
        log.error('Error: Failed to delete. No value "{0}" found.'.format(args.value))
    else:
        log.info('successfully removed memberOfGroupAttr value "{}"'.format(args.value))

def display_allbackends(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    val = plugin.get_allbackends_formatted()
    if not val:
        log.info("memberOfAllBackends is not set")
    else:
        log.info(val)

def enable_allbackends(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    plugin.enable_allbackends()
    log.info("memberOfAllBackends enabled successfully")

def disable_allbackends(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    plugin.disable_allbackends()
    log.info("memberOfAllBackends disabled successfully")

def manage_autoaddoc(inst, basedn, log, args):
    if args.value == "del":
        remove_autoaddoc(inst, basedn, log, args)
    elif args.value is not None:
        set_autoaddoc(inst, basedn, log, args)
    else:
        display_autoaddoc(inst, basedn, log, args)

def display_autoaddoc(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    val = plugin.get_autoaddoc_formatted()
    if not val:
        log.info("memberOfAutoAddOc is not set")
    else:
        log.info(val)

def set_autoaddoc(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    d = {'nsmemberof': 'nsMemberOf', 'inetuser': 'inetUser', 'inetadmin': 'inetAdmin'}
    plugin.set_autoaddoc(d[args.value])
    log.info('memberOfAutoAddOc set to "{}"'.format(d[args.value]))

def remove_autoaddoc(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    if not plugin.get_autoaddoc():
        log.info("memberOfAutoAddOc was not set")
    else:
        plugin.remove_autoaddoc()
        log.info("memberOfAutoAddOc attribute deleted")

def display_scope(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    val = plugin.get_entryscope_formatted()
    if not val:
        log.info("memberOfEntryScope is not set")
    else:
        log.info(val)

def add_scope(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    try:
        plugin.add_entryscope(args.value)
    except ldap.UNWILLING_TO_PERFORM as ex:
        if "is also listed as an exclude suffix" in ex.args[0]['info']:
            log.error('Error: Include suffix ({0}) is also listed as an exclude suffix.'.format(args.value))
        else:
            log.error('Error: Invalid DN "{}". Failed to add.'.format(args.value))
    except ldap.TYPE_OR_VALUE_EXISTS:
        log.info('Value "{}" already exists.'.format(args.value))
    else:
        log.info('successfully added memberOfEntryScope value "{}"'.format(args.value))

def remove_scope(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    try:
        plugin.remove_entryscope(args.value)
    except ldap.NO_SUCH_ATTRIBUTE as ex:
        log.error('Error: Failed to delete. No value "{0}" found.'.format(args.value))
    else:
        log.info('successfully removed memberOfEntryScope value "{}"'.format(args.value))

def display_excludescope(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    val = plugin.get_excludescope_formatted()
    if not val:
        log.info("memberOfEntryScopeExcludeSubtree is not set")
    else:
        log.info(val)

def add_excludescope(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    try:
        plugin.add_excludescope(args.value)
    except ldap.UNWILLING_TO_PERFORM as ex:
        if "is also listed as an exclude suffix" in ex.args[0]['info']:
            log.error('Error: Suffix ({0}) is listed in entry scope.'.format(args.value))
        else:
            log.error('Error: Invalid DN "{}". Failed to add.'.format(args.value))
    except ldap.TYPE_OR_VALUE_EXISTS:
        log.info('Value "{}" already exists.'.format(args.value))
    else:
        log.info('successfully added memberOfEntryScopeExcludeSubtree value "{}"'.format(args.value))

def remove_excludescope(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    try:
        plugin.remove_excludescope(args.value)
    except ldap.NO_SUCH_ATTRIBUTE as ex:
        log.error('Error: Failed to delete. No value "{0}" found.'.format(args.value))
    else:
        log.info('successfully removed memberOfEntryScopeExcludeSubtree value "{}"'.format(args.value))

def fixup(inst, basedn, log, args):
    """Run the fix-up task for memberof plugin."""
    pass

def create_parser(subparsers):
    memberof_parser = subparsers.add_parser('memberof', help='Manage and configure MemberOf plugin')

    subcommands = memberof_parser.add_subparsers(help='action')

    show_parser = subcommands.add_parser('show', help='display memberof plugin configuration')
    show_parser.set_defaults(func=generic_show, plugin_cls=MemberOfPlugin)

    enable_parser = subcommands.add_parser('enable', help='enable memberof plugin')
    enable_parser.set_defaults(func=generic_enable, plugin_cls=MemberOfPlugin)

    disable_parser = subcommands.add_parser('disable', help='disable memberof plugin')
    disable_parser.set_defaults(func=generic_disable, plugin_cls=MemberOfPlugin)

    attr_parser = subcommands.add_parser('attr', help='get or set memberofattr')
    attr_parser.set_defaults(func=manage_attr)
    attr_parser.add_argument('value', nargs='?', help='The value to set as memberofattr')

    groupattr_parser = subcommands.add_parser('groupattr', help='get or manage memberofgroupattr')
    groupattr_parser.set_defaults(func=display_groupattr)
    # argparse doesn't support optional subparsers in python2!
    groupattr_subcommands = groupattr_parser.add_subparsers(help='action')
    add_groupattr_parser = groupattr_subcommands.add_parser('add', help='add memberofgroupattr value')
    add_groupattr_parser.set_defaults(func=add_groupattr)
    add_groupattr_parser.add_argument('value', help='The value to add in memberofgroupattr')
    del_groupattr_parser = groupattr_subcommands.add_parser('del', help='remove memberofgroupattr value')
    del_groupattr_parser.set_defaults(func=remove_groupattr)
    del_groupattr_parser.add_argument('value', help='The value to remove from memberofgroupattr')

    allbackends_parser = subcommands.add_parser('allbackends', help='get or manage memberofallbackends')
    allbackends_parser.set_defaults(func=display_allbackends)
    # argparse doesn't support optional subparsers in python2!
    allbackends_subcommands = allbackends_parser.add_subparsers(help='action')
    on_allbackends_parser = allbackends_subcommands.add_parser('on', help='enable all backends for memberof')
    on_allbackends_parser.set_defaults(func=enable_allbackends)
    off_allbackends_parser = allbackends_subcommands.add_parser('off', help='disable all backends for memberof')
    off_allbackends_parser.set_defaults(func=disable_allbackends)

    autoaddoc_parser = subcommands.add_parser('autoaddoc', help='get or set memberofautoaddoc')
    autoaddoc_parser.set_defaults(func=manage_autoaddoc)
    autoaddoc_parser.add_argument('value', nargs='?', choices=['nsmemberof', 'inetuser', 'inetadmin', 'del'],
                                   type=str.lower, help='The value to set as memberofautoaddoc or del to remove the attribute')

    scope_parser = subcommands.add_parser('scope', help='get or manage memberofentryscope')
    scope_parser.set_defaults(func=display_scope)
    # argparse doesn't support optional subparsers in python2!
    scope_subcommands = scope_parser.add_subparsers(help='action')
    add_scope_parser = scope_subcommands.add_parser('add', help='add memberofentryscope value')
    add_scope_parser.set_defaults(func=add_scope)
    add_scope_parser.add_argument('value', help='The value to add in memberofentryscope')
    del_scope_parser = scope_subcommands.add_parser('del', help='remove memberofentryscope value')
    del_scope_parser.set_defaults(func=remove_scope)
    del_scope_parser.add_argument('value', help='The value to remove from memberofentryscope')

    exclude_parser = subcommands.add_parser('exclude', help='get or manage memberofentryscopeexcludesubtree')
    exclude_parser.set_defaults(func=display_excludescope)
    # argparse doesn't support optional subparsers in python2!
    exclude_subcommands = exclude_parser.add_subparsers(help='action')
    add_exclude_parser = exclude_subcommands.add_parser('add', help='add memberofentryscopeexcludesubtree value')
    add_exclude_parser.set_defaults(func=add_excludescope)
    add_exclude_parser.add_argument('value', help='The value to add in memberofentryscopeexcludesubtree')
    del_exclude_parser = exclude_subcommands.add_parser('del', help='remove memberofentryscopeexcludesubtree value')
    del_exclude_parser.set_defaults(func=remove_excludescope)
    del_exclude_parser.add_argument('value', help='The value to remove from memberofentryscopeexcludesubtree')

    fixup_parser = subcommands.add_parser('fixup', help='run the fix-up task for memberof plugin')
    fixup_parser.set_defaults(func=fixup)
