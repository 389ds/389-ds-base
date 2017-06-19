# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import Plugin, Plugins
import argparse

from lib389.cli_base import (
    _generic_list,
    _generic_get,
    _generic_get_dn,
    _generic_create,
    _generic_delete,
    _get_arg,
    _get_args,
    _get_attributes,
    _warn,
    )

SINGULAR = Plugin
MANY = Plugins
RDN = 'cn'


def plugin_list(inst, basedn, log, args):
    _generic_list(inst, basedn, log.getChild('plugin_list'), MANY)

def plugin_get(inst, basedn, log, args):
    rdn = _get_arg( args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('plugin_get'), MANY, rdn)

def plugin_get_dn(inst, basedn, log, args):
    dn = _get_arg( args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('plugin_get_dn'), MANY, dn)

# Plugin enable
def plugin_enable(inst, basedn, log, args):
    dn = _get_arg( args.dn, msg="Enter plugin dn to enable")
    mc = MANY(inst, basedn)
    o = mc.get(dn=dn)
    o.enable()
    o_str = o.display()
    log.info('Enabled %s', o_str)

# Plugin disable
def plugin_disable(inst, basedn, log, args, warn=True):
    dn = _get_arg( args.dn, msg="Enter plugin dn to disable")
    if warn:
        _warn(dn, msg="Disabling %s %s" % (SINGULAR.__name__, dn))
    mc = MANY(inst, basedn)
    o = mc.get(dn=dn)
    o.disable()
    o_str = o.display()
    log.info('Disabled %s', o_str)

# Plugin configure?
def plugin_configure(inst, basedn, log, args):
    pass

def generic_show(inst, basedn, log, args):
    """Display plugin configuration."""
    plugin = args.plugin_cls(inst)
    log.info(plugin.display())

def generic_enable(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    plugin.enable()
    log.info("Enabled %s", plugin.rdn)

def generic_disable(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    plugin.disable()
    log.info("Disabled %s", plugin.rdn)

def generic_status(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    if plugin.status() == True:
        log.info("%s is enabled", plugin.rdn)
    else:
        log.info("%s is disabled", plugin.rdn)

def create_parser(subparsers):
    plugin_parser = subparsers.add_parser('plugin', help="Manage plugins available on the server")

    subcommands = plugin_parser.add_subparsers(help="action")

    list_parser = subcommands.add_parser('list', help="List current configured (enabled and disabled) plugins")
    list_parser.set_defaults(func=plugin_list)

    get_parser = subcommands.add_parser('get', help='get')
    get_parser.set_defaults(func=plugin_get)
    get_parser.add_argument('selector', nargs='?', help='The plugin to search for')

    get_dn_parser = subcommands.add_parser('get_dn', help='get_dn')
    get_dn_parser.set_defaults(func=plugin_get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The plugin dn to get')

    enable_parser = subcommands.add_parser('enable', help='enable a plugin in the server')
    enable_parser.set_defaults(func=plugin_enable)
    enable_parser.add_argument('dn', nargs='?', help='The dn to enable')

    disable_parser = subcommands.add_parser('disable', help='disable the plugin configuration')
    disable_parser.set_defaults(func=plugin_disable)
    disable_parser.add_argument('dn', nargs='?', help='The dn to disable')


