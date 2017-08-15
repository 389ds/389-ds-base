# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016-2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap

from lib389.plugins import ReferentialIntegrityPlugin
from lib389.cli_conf.plugin import add_generic_plugin_parsers


def manage_update_delay(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    if args.value is None:
        val = plugin.get_update_delay_formatted()
        log.info(val)
    else:
        plugin.set_update_delay(args.value)
        log.info('referint-update-delay set to "{}"'.format(args.value))

def display_membership_attr(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    log.info(plugin.get_membership_attr_formatted())

def add_membership_attr(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    try:
        plugin.add_membership_attr(args.value)
    except ldap.TYPE_OR_VALUE_EXISTS:
        log.info('Value "{}" already exists.'.format(args.value))
    else:
        log.info('successfully added membership attribute "{}"'.format(args.value))

def remove_membership_attr(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    try:
        plugin.remove_membership_attr(args.value)
    except ldap.OPERATIONS_ERROR:
        log.error("Error: Failed to delete. At least one value for membership attribute should exist.")
    except ldap.NO_SUCH_ATTRIBUTE:
        log.error('Error: Failed to delete. No value "{0}" found.'.format(args.value))
    else:
        log.info('successfully removed membership attribute "{}"'.format(args.value))

def display_scope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    val = plugin.get_entryscope_formatted()
    if not val:
        log.info("nsslapd-pluginEntryScope is not set")
    else:
        log.info(val)

def add_scope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    try:
        plugin.add_entryscope(args.value)
    except ldap.TYPE_OR_VALUE_EXISTS:
        log.info('Value "{}" already exists.'.format(args.value))
    else:
        log.info('successfully added nsslapd-pluginEntryScope value "{}"'.format(args.value))

def remove_scope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    try:
        plugin.remove_entryscope(args.value)
    except ldap.NO_SUCH_ATTRIBUTE:
        log.error('Error: Failed to delete. No value "{0}" found.'.format(args.value))
    else:
        log.info('successfully removed nsslapd-pluginEntryScope value "{}"'.format(args.value))

def remove_all_scope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    plugin.remove_all_entryscope()
    log.info('successfully removed all nsslapd-pluginEntryScope values')

def display_excludescope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    val = plugin.get_excludescope_formatted()
    if not val:
        log.info("nsslapd-pluginExcludeEntryScope is not set")
    else:
        log.info(val)

def add_excludescope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    try:
        plugin.add_excludescope(args.value)
    except ldap.TYPE_OR_VALUE_EXISTS:
        log.info('Value "{}" already exists.'.format(args.value))
    else:
        log.info('successfully added nsslapd-pluginExcludeEntryScope value "{}"'.format(args.value))

def remove_excludescope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    try:
        plugin.remove_excludescope(args.value)
    except ldap.NO_SUCH_ATTRIBUTE:
        log.error('Error: Failed to delete. No value "{0}" found.'.format(args.value))
    else:
        log.info('successfully removed nsslapd-pluginExcludeEntryScope value "{}"'.format(args.value))

def remove_all_excludescope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    plugin.remove_all_excludescope()
    log.info('successfully removed all nsslapd-pluginExcludeEntryScope values')

def display_container_scope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    val = plugin.get_container_scope_formatted()
    if not val:
        log.info("nsslapd-pluginContainerScope is not set")
    else:
        log.info(val)

def add_container_scope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    try:
        plugin.add_container_scope(args.value)
    except ldap.TYPE_OR_VALUE_EXISTS:
        log.info('Value "{}" already exists.'.format(args.value))
    else:
        log.info('successfully added nsslapd-pluginContainerScope value "{}"'.format(args.value))

def remove_container_scope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    try:
        plugin.remove_container_scope(args.value)
    except ldap.NO_SUCH_ATTRIBUTE:
        log.error('Error: Failed to delete. No value "{0}" found.'.format(args.value))
    else:
        log.info('successfully removed nsslapd-pluginContainerScope value "{}"'.format(args.value))

def remove_all_container_scope(inst, basedn, log, args):
    plugin = ReferentialIntegrityPlugin(inst)
    plugin.remove_all_container_scope()
    log.info('successfully removed all nsslapd-pluginContainerScope values')


def create_parser(subparsers):
    referint_parser = subparsers.add_parser('referint', help='Manage and configure Referential Integrity plugin')

    subcommands = referint_parser.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, ReferentialIntegrityPlugin)

    delay_parser = subcommands.add_parser('delay', help='get or set update delay')
    delay_parser.set_defaults(func=manage_update_delay)
    delay_parser.add_argument('value', nargs='?', help='The value to set as update delay')

    attr_parser = subcommands.add_parser('attrs', help='get or manage membership attributes')
    attr_parser.set_defaults(func=display_membership_attr)
    attr_subcommands = attr_parser.add_subparsers(help='action')
    add_attr_parser = attr_subcommands.add_parser('add', help='add membership attribute')
    add_attr_parser.set_defaults(func=add_membership_attr)
    add_attr_parser.add_argument('value', help='membership attribute to add')
    del_attr_parser = attr_subcommands.add_parser('del', help='remove membership attribute')
    del_attr_parser.set_defaults(func=remove_membership_attr)
    del_attr_parser.add_argument('value', help='membership attribute to remove')

    scope_parser = subcommands.add_parser('scope', help='get or manage referint scope')
    scope_parser.set_defaults(func=display_scope)
    scope_subcommands = scope_parser.add_subparsers(help='action')
    add_scope_parser = scope_subcommands.add_parser('add', help='add entry scope value')
    add_scope_parser.set_defaults(func=add_scope)
    add_scope_parser.add_argument('value', help='The value to add in referint entry scope')
    del_scope_parser = scope_subcommands.add_parser('del', help='remove entry scope value')
    del_scope_parser.set_defaults(func=remove_scope)
    del_scope_parser.add_argument('value', help='The value to remove from entry scope')
    delall_scope_parser = scope_subcommands.add_parser('delall', help='remove all entry scope values')
    delall_scope_parser.set_defaults(func=remove_all_scope)

    exclude_parser = subcommands.add_parser('exclude', help='get or manage referint exclude scope')
    exclude_parser.set_defaults(func=display_excludescope)
    exclude_subcommands = exclude_parser.add_subparsers(help='action')
    add_exclude_parser = exclude_subcommands.add_parser('add', help='add exclude scope value')
    add_exclude_parser.set_defaults(func=add_excludescope)
    add_exclude_parser.add_argument('value', help='The value to add in exclude scope')
    del_exclude_parser = exclude_subcommands.add_parser('del', help='remove exclude scope value')
    del_exclude_parser.set_defaults(func=remove_excludescope)
    del_exclude_parser.add_argument('value', help='The value to remove from exclude scope')
    delall_exclude_parser = exclude_subcommands.add_parser('delall', help='remove all exclude scope values')
    delall_exclude_parser.set_defaults(func=remove_all_excludescope)

    container_parser = subcommands.add_parser('container', help='get or manage referint container scope')
    container_parser.set_defaults(func=display_container_scope)
    container_subcommands = container_parser.add_subparsers(help='action')
    add_container_parser = container_subcommands.add_parser('add', help='add container scope value')
    add_container_parser.set_defaults(func=add_container_scope)
    add_container_parser.add_argument('value', help='The value to add in container scope')
    del_container_parser = container_subcommands.add_parser('del', help='remove container scope value')
    del_container_parser.set_defaults(func=remove_container_scope)
    del_container_parser.add_argument('value', help='The value to remove from container scope')
    delall_container_parser = container_subcommands.add_parser('delall', help='remove all container scope values')
    delall_container_parser.set_defaults(func=remove_all_container_scope)
